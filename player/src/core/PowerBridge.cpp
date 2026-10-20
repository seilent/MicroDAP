#include "PowerBridge.h"

#include "Keep.h"
#include "OutputMode.h"
#include "OverlayBridge.h"
#include "Player.h"
#include "Settings.h"
#include "SleepInhibitor.h"
#include "State.h"

#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>

namespace microdap {

namespace {

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString programPath(const QString &name)
{
    const QString dir = qEnvironmentVariable("MICRODAP_VERB_DIR");
    if (dir.isEmpty() || name.contains(QLatin1Char('/')))
        return name;
    return dir + QLatin1Char('/') + name;
}

}

PowerBridgeAdaptor::PowerBridgeAdaptor(PowerBridge *bridge)
    : QDBusAbstractAdaptor(bridge)
    , m_bridge(bridge)
{
}

bool PowerBridgeAdaptor::screenOff() const
{
    return m_bridge->screenOff();
}

QString PowerBridgeAdaptor::playbackStatus() const
{
    return m_bridge->playbackStatus();
}

bool PowerBridgeAdaptor::overlayShown() const
{
    return m_bridge->overlayShown();
}

void PowerBridgeAdaptor::SetScreenOff(bool off)
{
    qInfo() << "microdap power: SetScreenOff" << off;
    m_bridge->requestScreenOff(off);
}

void PowerBridgeAdaptor::ToggleScreen()
{
    qInfo() << "microdap power: ToggleScreen";
    m_bridge->toggleScreen();
}

void PowerBridgeAdaptor::ShowPowerMenu()
{
    qInfo() << "microdap power: ShowPowerMenu";
    m_bridge->requestPowerMenu();
}

void PowerBridgeAdaptor::ShowOverlay()
{
    qInfo() << "microdap power: ShowOverlay";
    m_bridge->showOverlay();
}

void PowerBridgeAdaptor::HideOverlay()
{
    qInfo() << "microdap power: HideOverlay";
    m_bridge->hideOverlay();
}

void PowerBridgeAdaptor::ToggleOverlay()
{
    qInfo() << "microdap power: ToggleOverlay";
    m_bridge->toggleOverlay();
}

void PowerBridgeAdaptor::ShowSwitchPrompt()
{
    qInfo() << "microdap power: ShowSwitchPrompt";
    m_bridge->requestSwitchPrompt();
}

PowerBridge::PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_bus(bus)
    , m_powerd(powerdBus())
    , m_adaptor(new PowerBridgeAdaptor(this))
    , m_sessions(readSessions(sessionsDir(), QStringLiteral("microdap")))
    , m_imageVersion(readKey(osReleasePath(), QStringLiteral("IMAGE_VERSION")))
{
    m_switchTimer.setSingleShot(true);
    connect(&m_switchTimer, &QTimer::timeout, this, &PowerBridge::onSwitchTimeout);
    m_readyTimer.setSingleShot(true);
    m_readyTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_readyTimer, &QTimer::timeout, this, [this] {
        if (!m_awaitReady)
            return;
        qWarning() << "microdap power: no output release within" << m_readyTimer.interval() << "ms, switching";
        onSwitchReady();
    });
    connect(&m_updateWatch, &QFileSystemWatcher::directoryChanged, this, [this] {
        const QString before = m_updateStatus;
        if (!readUpdateFile() && before == QLatin1StringView("available"))
            setUpdate(QStringLiteral("none"), {});
        watchUpdate();
    });
    readUpdateFile();
    watchUpdate();
    m_idleTimer.setInterval(idleCheckMs());
    connect(&m_idleTimer, &QTimer::timeout, this, &PowerBridge::checkIdle);
    m_sleepTimer.setSingleShot(true);
    m_sleepTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_sleepTimer, &QTimer::timeout, this, &PowerBridge::sleepTick);
    m_sleepEnd.setSingleShot(true);
    m_sleepEnd.setInterval(sleepEndDelayMs);
    connect(&m_sleepEnd, &QTimer::timeout, this, [this] {
        qInfo() << "microdap power: sleep timer end, suspending";
        logind(QStringLiteral("Suspend"));
    });
    if (m_player)
        connect(m_player, &Player::statusChanged, this, &PowerBridge::notifyPlaybackStatus);
    if (m_powerd.isConnected()) {
        auto *watcher = new QDBusServiceWatcher(powerdService(), m_powerd, QDBusServiceWatcher::WatchForOwnerChange,
                                                this);
        connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &PowerBridge::onPowerdOwner);
    }

    if (!m_bus.isConnected()) {
        qWarning() << "microdap power: session bus not connected";
        return;
    }
    if (!m_bus.connect(QString(), hotkeysPath(), hotkeysInterface(), QStringLiteral("VolumeChanged"),
                       this, SLOT(onVolumeChanged(double)))) {
        qWarning() << "microdap power: cannot subscribe to" << hotkeysInterface()
                   << m_bus.lastError().message();
    }
    if (!m_bus.registerObject(objectPath(), this, QDBusConnection::ExportAdaptors)) {
        qWarning() << "microdap power: cannot register" << objectPath()
                   << m_bus.lastError().message();
        return;
    }
    if (!m_bus.registerService(serviceName())) {
        qWarning() << "microdap power: cannot register" << serviceName()
                   << m_bus.lastError().message();
        m_bus.unregisterObject(objectPath());
        return;
    }
    m_registered = true;
}

PowerBridge::~PowerBridge()
{
    dropSwitchProcess();
    if (m_check) {
        m_check->disconnect(this);
        m_check->kill();
        m_check->waitForFinished(1000);
    }
    if (!m_heldMode.isEmpty() && m_powerd.isConnected()) {
        QDBusMessage call = QDBusMessage::createMethodCall(powerdService(), powerdPath(), powerdInterface(),
                                                           QStringLiteral("Release"));
        call << m_heldMode;
        m_powerd.send(call);
    }
    if (m_radio) {
        m_radio->disconnect(this);
        m_radio->waitForFinished(1000);
    }
    if (!m_registered)
        return;
    m_bus.unregisterService(serviceName());
    m_bus.unregisterObject(objectPath());
}

QString PowerBridge::serviceName()
{
    return QStringLiteral("net.seilent.MicroDap.Player");
}

QString PowerBridge::objectPath()
{
    return QStringLiteral("/net/seilent/MicroDap/Player");
}

QString PowerBridge::interfaceName()
{
    return QStringLiteral("net.seilent.MicroDap.Player");
}

QString PowerBridge::hotkeysPath()
{
    return QStringLiteral("/net/seilent/MicroDap/Hotkeys");
}

QString PowerBridge::hotkeysInterface()
{
    return QStringLiteral("net.seilent.MicroDap.Hotkeys");
}

QString PowerBridge::notSupportedError()
{
    return QStringLiteral("net.seilent.MicroDap.Error.NotSupported");
}

QString PowerBridge::powerdService()
{
    return QStringLiteral("net.seilent.MicroOs.Power");
}

QString PowerBridge::powerdPath()
{
    return QStringLiteral("/net/seilent/MicroOs/Power");
}

QString PowerBridge::powerdInterface()
{
    return QStringLiteral("net.seilent.MicroOs.Power1");
}

QDBusConnection PowerBridge::powerdBus()
{
    const QString address = qEnvironmentVariable("MICRODAP_POWERD_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("microdap-powerd"));
}

QStringList PowerBridge::verbs()
{
    return {
        QStringLiteral("screen-off"),
        QStringLiteral("game-mode"),
        QStringLiteral("suspend"),
        QStringLiteral("poweroff"),
    };
}

QStringList PowerBridge::switchCommand(const QString &session)
{
    return {QStringLiteral("systemctl"), QStringLiteral("start"), QStringLiteral("--no-block"),
            QStringLiteral("microos-session-switch@") + session + QStringLiteral(".service")};
}

bool PowerBridge::validSession(const QString &name)
{
    static const QRegularExpression re(QStringLiteral("^[a-z0-9][a-z0-9-]*$"));
    return re.match(name).hasMatch();
}

QString PowerBridge::sessionsDir()
{
    return qEnvironmentVariable("MICRODAP_SESSIONS_DIR", QStringLiteral("/usr/lib/microos/sessions.d"));
}

QVariantList PowerBridge::readSessions(const QString &dir, const QString &current)
{
    QVariantList out;
    const QFileInfoList entries = QDir(dir).entryInfoList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
    for (const QFileInfo &entry : entries) {
        const QString name = entry.completeBaseName();
        if (name == current || !validSession(name))
            continue;
        QFile file(entry.absoluteFilePath());
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        QString label = name;
        bool listed = false;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            const qsizetype eq = line.indexOf(QLatin1Char('='));
            if (eq <= 0)
                continue;
            const QString key = line.left(eq);
            const QString value = line.mid(eq + 1).trimmed();
            if (key == QLatin1StringView("LABEL") && !value.isEmpty())
                label = value;
            else if (key == QLatin1StringView("DEFAULT"))
                listed = value == QLatin1StringView("yes");
        }
        if (listed)
            out.append(QVariantMap{{QStringLiteral("name"), name}, {QStringLiteral("label"), label}});
    }
    return out;
}

QString PowerBridge::backlightDir()
{
    return qEnvironmentVariable("MICRODAP_SYS_ROOT", QStringLiteral("/sys")) + QStringLiteral("/class/backlight");
}

QString PowerBridge::wifiMarker()
{
    return QDir::homePath() + QStringLiteral("/.local/state/microdap/wifi-off");
}

QString PowerBridge::nmcliPath()
{
    return qEnvironmentVariable("MICRODAP_NMCLI_BIN", QStringLiteral("nmcli"));
}

QString PowerBridge::osReleasePath()
{
    return qEnvironmentVariable("MICRODAP_OS_RELEASE", QStringLiteral("/usr/lib/os-release"));
}

QString PowerBridge::updateFile()
{
    return qEnvironmentVariable("MICRODAP_UPDATE_FILE", QStringLiteral("/run/microos/update-available"));
}

QString PowerBridge::readKey(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    const QString prefix = key + QLatin1Char('=');
    QString value;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(prefix))
            value = line.mid(prefix.size());
    }
    if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))
        && value.endsWith(value.front()))
        value = value.mid(1, value.size() - 2);
    return value;
}

QString PowerBridge::checkStatus(const QString &show)
{
    QString status;
    QString started;
    for (const QString &line : show.split(QLatin1Char('\n'))) {
        if (line.startsWith(QLatin1StringView("ExecMainStatus=")))
            status = line.mid(15).trimmed();
        else if (line.startsWith(QLatin1StringView("ExecMainStartTimestampMonotonic=")))
            started = line.mid(32).trimmed();
    }
    if (started.isEmpty() || started == QLatin1StringView("0"))
        return QStringLiteral("unchecked");
    if (status == QLatin1StringView("0") || status == QLatin1StringView("7"))
        return QStringLiteral("none");
    if (status == QLatin1StringView("4"))
        return QStringLiteral("offline");
    return QStringLiteral("unchecked");
}

int PowerBridge::switchTimeoutMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_SWITCH_TIMEOUT_MS", &ok);
    return ok && value > 0 ? value : 30000;
}

int PowerBridge::idleCheckMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_IDLE_CHECK_MS", &ok);
    return ok && value > 0 ? value : 60000;
}

int PowerBridge::idleMinuteMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_IDLE_MINUTE_MS", &ok);
    return ok && value > 0 ? value : 60000;
}

bool PowerBridge::isRegistered() const
{
    return m_registered;
}

Player *PowerBridge::player() const
{
    return m_player;
}

OverlayBridge *PowerBridge::overlay() const
{
    return m_overlay;
}

void PowerBridge::setOverlay(OverlayBridge *overlay)
{
    if (m_overlay == overlay)
        return;
    if (m_overlay)
        disconnect(m_overlay, nullptr, this, nullptr);
    m_overlay = overlay;
    if (!m_overlay)
        return;
    if (m_sleep) {
        m_sleep->release();
        m_sleep->deleteLater();
        m_sleep = nullptr;
    }
    m_holds = false;
    m_idleTimer.stop();
    applyHold();
    connect(m_overlay, &OverlayBridge::shownChanged, this, &PowerBridge::notifyOverlayShown);
    connect(m_overlay, &QObject::destroyed, this, [this] { m_overlay = nullptr; });
}

bool PowerBridge::overlayMode() const
{
    return m_overlay != nullptr;
}

State *PowerBridge::state() const
{
    return m_state;
}

void PowerBridge::setState(State *state)
{
    if (m_state == state)
        return;
    if (m_state)
        disconnect(m_state, nullptr, this, nullptr);
    m_state = state;
    if (m_state)
        connect(m_state, &QObject::destroyed, this, [this] { m_state = nullptr; });
}

void PowerBridge::setSettings(Settings *settings)
{
    m_settings = settings;
}

bool PowerBridge::screenOff() const
{
    return m_screenOff;
}

void PowerBridge::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    m_screenOff = off;
    emit screenOffChanged();
    emitScreenOffChanged();
    emitPropertiesChanged({{QStringLiteral("ScreenOff"), m_screenOff}});
    applyHold();
}

int PowerBridge::volume() const
{
    return m_volume;
}

QString PowerBridge::playbackStatus() const
{
    if (!m_player)
        return QStringLiteral("Stopped");
    switch (m_player->status()) {
    case Player::Playing:
        return QStringLiteral("Playing");
    case Player::Paused:
        return QStringLiteral("Paused");
    case Player::Stopped:
        break;
    }
    return QStringLiteral("Stopped");
}

bool PowerBridge::overlayShown() const
{
    return m_overlay && m_overlay->shown();
}

bool PowerBridge::switching() const
{
    return m_switching;
}

QVariantList PowerBridge::sessions() const
{
    return m_sessions;
}

QString PowerBridge::holdMode() const
{
    return m_holdMode;
}

QString PowerBridge::imageVersion() const
{
    return m_imageVersion;
}

QString PowerBridge::updateStatus() const
{
    return m_updateStatus;
}

QString PowerBridge::updateVersion() const
{
    return m_updateVersion;
}

QString PowerBridge::updateState() const
{
    return m_updateState;
}

QString PowerBridge::updateChecked() const
{
    return m_updateChecked;
}

void PowerBridge::watchUpdate()
{
    QString dir = QFileInfo(updateFile()).absolutePath();
    while (!QFileInfo(dir).isDir() && dir != QFileInfo(dir).absolutePath())
        dir = QFileInfo(dir).absolutePath();
    const QStringList watched = m_updateWatch.directories();
    if (watched == QStringList{dir})
        return;
    if (!watched.isEmpty())
        m_updateWatch.removePaths(watched);
    m_updateWatch.addPath(dir);
}

bool PowerBridge::readUpdateFile()
{
    const QString path = updateFile();
    const QString state = readKey(path, QStringLiteral("state"));
    const QString checked = readKey(path, QStringLiteral("checked"));
    const bool available = !state.isEmpty() && state != QLatin1StringView("withdrawn");
    const bool moved = m_updateState != state || m_updateChecked != checked;
    m_updateState = state;
    m_updateChecked = checked;
    if (available) {
        const QString version = readKey(path, QStringLiteral("version"));
        if (moved || m_updateStatus != QLatin1StringView("available") || m_updateVersion != version) {
            m_updateStatus = QStringLiteral("available");
            m_updateVersion = version;
            emit updateChanged();
        }
        return true;
    }
    if (moved)
        emit updateChanged();
    return false;
}

void PowerBridge::setUpdate(const QString &status, const QString &version)
{
    if (m_updateStatus == status && m_updateVersion == version)
        return;
    m_updateStatus = status;
    m_updateVersion = version;
    emit updateChanged();
}

void PowerBridge::reloadUpdate()
{
    if (readUpdateFile())
        return;
    if (m_check) {
        m_checkAgain = true;
        return;
    }
    m_checkAgain = false;
    m_check = new QProcess(this);
    connect(m_check, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const QString out = QString::fromUtf8(m_check->readAllStandardOutput());
        m_check->deleteLater();
        m_check = nullptr;
        if (m_checkAgain) {
            reloadUpdate();
            return;
        }
        setUpdate(status == QProcess::NormalExit && code == 0 ? checkStatus(out) : QStringLiteral("unchecked"), {});
    });
    connect(m_check, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        m_check->deleteLater();
        m_check = nullptr;
        if (m_checkAgain) {
            reloadUpdate();
            return;
        }
        setUpdate(QStringLiteral("unchecked"), {});
    });
    m_check->start(programPath(QStringLiteral("systemctl")),
                   {QStringLiteral("show"), QStringLiteral("-p"), QStringLiteral("ExecMainStatus"), QStringLiteral("-p"),
                    QStringLiteral("ExecMainStartTimestampMonotonic"), QStringLiteral("microos-update-check.service")});
}

void PowerBridge::setOutputMode(OutputMode *mode, PlayerPort *player)
{
    if (m_outputMode)
        disconnect(m_outputMode, nullptr, this, nullptr);
    m_outputMode = mode;
    m_switchPlayer = player;
    if (m_outputMode)
        connect(m_outputMode, &OutputMode::switchReady, this, &PowerBridge::onSwitchReady);
}

void PowerBridge::enableSleepWatch()
{
    if (m_sleep || m_overlay)
        return;
    m_sleep = new SleepInhibitor(QStringLiteral("save state"), this);
    connect(m_sleep, &SleepInhibitor::prepareForSleep, this, &PowerBridge::onPrepareForSleep);
    m_sleep->take();
}

void PowerBridge::enableHolds()
{
    if (m_overlay || m_holds)
        return;
    m_holds = true;
    applyHold();
}

SleepInhibitor *PowerBridge::sleep() const
{
    return m_sleep;
}

bool PowerBridge::waking() const
{
    return m_radio && m_wakePending;
}

bool PowerBridge::transition() const
{
    return m_radio != nullptr;
}

void PowerBridge::applyHold()
{
    const QString wanted = m_holds && !m_overlay
                               ? (m_screenOff ? QStringLiteral("dap-off") : QStringLiteral("dap")) : QString();
    if (wanted == m_holdMode)
        return;
    m_holdMode = wanted;
    emit holdModeChanged();
    sendHold();
}

void PowerBridge::sendHold()
{
    if (!m_powerd.isConnected())
        return;
    const QString mode = m_holdMode;
    if (mode.isEmpty() && m_heldMode.isEmpty())
        return;
    const QString method = mode.isEmpty() ? QStringLiteral("Release") : QStringLiteral("Hold");
    QDBusMessage call = QDBusMessage::createMethodCall(powerdService(), powerdPath(), powerdInterface(), method);
    call << (mode.isEmpty() ? m_heldMode : mode);
    qInfo() << "microdap power:" << method << call.arguments().constFirst().toString();
    m_heldMode = mode;
    auto *watcher = new QDBusPendingCallWatcher(m_powerd.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, method, mode] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            return;
        qWarning().noquote() << "microdap power:" << method << "failed," << reply.errorName()
                             << reply.errorMessage();
        if (!mode.isEmpty() && m_heldMode == mode)
            m_heldMode.clear();
    });
}

void PowerBridge::onPowerdOwner(const QString &, const QString &, const QString &newOwner)
{
    m_heldMode.clear();
    if (newOwner.isEmpty() || m_holdMode.isEmpty())
        return;
    qInfo() << "microdap power: powerd is back, hold again";
    sendHold();
}

void PowerBridge::onPrepareForSleep(bool start)
{
    if (start) {
        if (m_player && m_player->status() == Player::Playing)
            m_player->pause();
        if (m_state)
            m_state->save(false);
        if (m_outputMode)
            m_outputMode->prepareForSleep(true);
        m_idleSince.invalidate();
        m_sleep->release();
        return;
    }
    screenOnNow(true);
    if (m_outputMode)
        m_outputMode->prepareForSleep(false);
    m_sleep->take();
}

void PowerBridge::runScreenOn()
{
    screenOnNow(false);
}

void PowerBridge::runScreenOff()
{
    screenOffNow();
}

void PowerBridge::writeBacklight(int value)
{
    const QFileInfoList entries = QDir(backlightDir()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QFileInfo &entry : entries) {
        QFile file(entry.absoluteFilePath() + QStringLiteral("/bl_power"));
        if (!file.exists())
            continue;
        if (!file.open(QIODevice::WriteOnly) || file.write(QByteArray::number(value)) < 0)
            qWarning() << "microdap power: cannot write" << file.fileName() << value;
    }
}

void PowerBridge::screenOffNow()
{
    if (m_overlay) {
        qInfo() << "microdap power: screen off ignored in overlay mode";
        return;
    }
    if (m_radio) {
        qInfo() << "microdap power: screen off during a transition, ignored";
        return;
    }
    qInfo() << "microdap power: screen off";
    writeBacklight(4);
    m_lastTransition.start();
    setScreenOff(true);
    m_idleSince.invalidate();
    m_idleTimer.start();
    if (!m_settings || !m_settings->wifiOffScreenOff() || QFile::exists(wifiMarker()))
        return;
    QDir().mkpath(QFileInfo(wifiMarker()).absolutePath());
    QFile marker(wifiMarker());
    if (!marker.open(QIODevice::WriteOnly)) {
        qWarning() << "microdap power: cannot write" << wifiMarker() << ", wifi kept on";
        return;
    }
    marker.close();
    radio(false, false);
}

void PowerBridge::screenOnNow(bool wake)
{
    if (m_radio) {
        qInfo() << "microdap power: screen on during a transition, ignored";
        if (wake)
            emit wakeFinished(false);
        return;
    }
    qInfo() << "microdap power: screen on" << (wake ? "after wake" : "");
    noteInput();
    writeBacklight(0);
    m_lastTransition.start();
    setScreenOff(false);
    m_idleTimer.stop();
    m_idleSince.invalidate();
    if (QFile::exists(wifiMarker())) {
        radio(true, wake);
        return;
    }
    if (wake)
        emit wakeFinished(true);
}

void PowerBridge::radio(bool on, bool wake)
{
    const QStringList args{QStringLiteral("radio"), QStringLiteral("wifi"), on ? QStringLiteral("on") : QStringLiteral("off")};
    qInfo() << "microdap power:" << nmcliPath() << args;
    m_wakePending = wake;
    m_radio = new QProcess(this);
    connect(m_radio, &QProcess::finished, this, [this, on, wake](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok)
            qWarning() << "microdap power: nmcli radio wifi" << (on ? "on" : "off") << "exit" << code;
        if (on == ok)
            QFile::remove(wifiMarker());
        endTransition(ok, wake);
    });
    connect(m_radio, &QProcess::errorOccurred, this, [this, wake](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "microdap power: cannot start" << nmcliPath();
        endTransition(false, wake);
    });
    m_radio->start(nmcliPath(), args);
}

void PowerBridge::endTransition(bool ok, bool wake)
{
    m_radio->deleteLater();
    m_radio = nullptr;
    m_wakePending = false;
    if (wake)
        emit wakeFinished(ok);
}

void PowerBridge::toggleScreen()
{
    if (m_overlay) {
        qInfo() << "microdap power: ToggleScreen ignored in overlay mode";
        return;
    }
    if (m_radio) {
        qInfo() << "microdap power: toggle during a transition, ignored";
        return;
    }
    if (m_lastTransition.isValid() && m_lastTransition.elapsed() < toggleDebounceMs) {
        qInfo() << "microdap power: toggle within" << toggleDebounceMs << "ms of the last transition, ignored";
        return;
    }
    if (m_screenOff)
        screenOnNow(false);
    else
        screenOffNow();
}

void PowerBridge::checkIdle()
{
    if (!m_screenOff || m_overlay) {
        m_idleSince.invalidate();
        return;
    }
    const bool playing = m_player && m_player->status() == Player::Playing;
    const bool phone = m_player && m_player->transportActive();
    const int minutes = m_settings ? m_settings->suspendMinutes() : 0;
    if (playing || phone || minutes <= 0) {
        m_idleSince.invalidate();
        return;
    }
    if (!m_idleSince.isValid()) {
        m_idleSince.start();
        return;
    }
    if (m_idleSince.elapsed() < qint64(minutes) * idleMinuteMs())
        return;
    qInfo() << "microdap power: idle for" << minutes << "min, suspending";
    m_idleSince.invalidate();
    logind(QStringLiteral("Suspend"));
}

int PowerBridge::sleepMinutesLeft() const
{
    return m_sleepLeft;
}

void PowerBridge::setSleepLeft(int minutes)
{
    if (m_sleepLeft == minutes)
        return;
    m_sleepLeft = minutes;
    emit sleepTimerChanged();
}

void PowerBridge::startSleepTimer(int minutes)
{
    m_sleepEnd.stop();
    if (minutes <= 0) {
        if (m_sleepMs <= 0)
            return;
        qInfo() << "microdap power: sleep timer off";
        m_sleepTimer.stop();
        m_sleepMs = 0;
        setSleepLeft(0);
        return;
    }
    qInfo() << "microdap power: sleep timer" << minutes << "min";
    m_sleepMinuteMs = idleMinuteMs();
    m_sleepMs = qint64(minutes) * m_sleepMinuteMs;
    m_sleepClock.start();
    sleepTick();
}

void PowerBridge::sleepTick()
{
    if (m_sleepMs <= 0)
        return;
    const qint64 remaining = m_sleepMs - m_sleepClock.elapsed();
    if (remaining > 0) {
        const qint64 left = (remaining + m_sleepMinuteMs - 1) / m_sleepMinuteMs;
        setSleepLeft(int(left));
        m_sleepTimer.start(int(remaining - (left - 1) * m_sleepMinuteMs));
        return;
    }
    m_sleepMs = 0;
    setSleepLeft(0);
    qInfo() << "microdap power: sleep timer end, pausing";
    if (m_player && m_player->status() == Player::Playing)
        m_player->pause();
    m_sleepEnd.start();
}

void PowerBridge::noteInput()
{
    if (!m_sleepEnd.isActive())
        return;
    qInfo() << "microdap power: sleep timer suspend cancelled by input";
    m_sleepEnd.stop();
}

void PowerBridge::logind(const QString &method)
{
    QDBusMessage call = QDBusMessage::createMethodCall(SleepInhibitor::service(), SleepInhibitor::path(),
                                                       SleepInhibitor::interface(), method);
    call << false;
    qInfo() << "microdap power: logind" << method;
    auto *watcher = new QDBusPendingCallWatcher(SleepInhibitor::bus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, method] {
        watcher->deleteLater();
        const QDBusMessage reply = watcher->reply();
        if (reply.type() == QDBusMessage::ReplyMessage)
            return;
        qWarning().noquote() << "microdap power: logind" << method << "failed," << reply.errorName()
                             << reply.errorMessage();
        emit verbFailed(method.toLower());
    });
}

bool PowerBridge::runVerb(const QString &verb)
{
    qInfo() << "microdap power: verb" << verb;
    if (verb == QLatin1StringView("game-mode")) {
        if (switching()) {
            qInfo() << "microdap power: switch already running";
            return false;
        }
        startSwitch(QStringLiteral("steam"));
        return true;
    }
    if (verb == QLatin1StringView("screen-off")) {
        screenOffNow();
        return true;
    }
    if (verb == QLatin1StringView("suspend")) {
        logind(QStringLiteral("Suspend"));
        return true;
    }
    if (verb == QLatin1StringView("poweroff")) {
        logind(QStringLiteral("PowerOff"));
        return true;
    }
    qWarning() << "microdap power: unknown verb" << verb;
    return false;
}

void PowerBridge::requestPowerMenu()
{
    if (m_overlay) {
        qInfo() << "microdap power: ShowPowerMenu ignored in overlay mode";
        return;
    }
    emit showPowerMenu();
}

void PowerBridge::requestSwitchPrompt()
{
    if (m_overlay) {
        qInfo() << "microdap power: ShowSwitchPrompt needs dap mode";
        if (calledFromDBus())
            sendErrorReply(notSupportedError(), QStringLiteral("ShowSwitchPrompt needs dap mode"));
        return;
    }
    emit showSwitchPrompt();
}

void PowerBridge::keepInBackground()
{
    qInfo() << "microdap power: keep in background";
    if (switching()) {
        qInfo() << "microdap power: switch already running";
        return;
    }
    m_switchTarget = QStringLiteral("steam");
    if (!m_state) {
        failSwitch(SwitchFailure::BeforeStart, QStringLiteral("no state to hand off"));
        return;
    }
    if (!m_state->save(true)) {
        failSwitch(SwitchFailure::BeforeStart, QStringLiteral("cannot save the handoff state"));
        return;
    }
    if (!Keep::write()) {
        failSwitch(SwitchFailure::BeforeStart, QStringLiteral("cannot write the keep marker"));
        return;
    }
    startSwitch(QStringLiteral("steam"));
}

void PowerBridge::closeAndSwitch()
{
    switchTo(QStringLiteral("steam"));
}

bool PowerBridge::switchTo(const QString &session)
{
    qInfo() << "microdap power: close player, switch to" << session;
    if (!validSession(session) || session == QLatin1StringView("microdap")) {
        qWarning() << "microdap power: not a session to switch to" << session;
        return false;
    }
    if (switching()) {
        qInfo() << "microdap power: switch already running";
        return false;
    }
    if (m_state)
        m_state->save(false);
    Keep::clear();
    startSwitch(session);
    return true;
}

void PowerBridge::startSwitch(const QString &session)
{
    m_switchTarget = session;
    m_switching = true;
    emit switchingChanged();
    if (!m_outputMode) {
        runSwitch();
        return;
    }
    m_switchPlay = m_switchPlayer ? m_switchPlayer->playState() : PlayerPort::PlayState::Stopped;
    m_switchWasParked = m_switchPlayer && m_switchPlayer->parked();
    m_switchReleased = false;
    m_awaitReady = true;
    m_outputMode->releaseForSwitch();
    if (!m_awaitReady)
        return;
    m_switchReleased = m_outputMode->switchStoppedStream();
    m_readyTimer.start(m_outputMode->switchBoundMs() + switchReadyMarginMs);
    qInfo() << "microdap power: switch waits for the output release";
}

void PowerBridge::onSwitchReady()
{
    if (!m_awaitReady)
        return;
    m_awaitReady = false;
    m_readyTimer.stop();
    if (!m_switchReleased && m_switchPlayer && !m_switchWasParked && m_switchPlayer->parked()) {
        m_switchReleased = true;
        qInfo() << "microdap power: switch parked the player";
    }
    runSwitch();
}

void PowerBridge::runSwitch()
{
    QStringList command = switchCommand(m_switchTarget);
    const QString program = programPath(command.takeFirst());
    qInfo() << "microdap power: switch" << program << command;
    m_switch = new QProcess(this);
    connect(m_switch, &QProcess::finished, this, &PowerBridge::onSwitchFinished);
    connect(m_switch, &QProcess::errorOccurred, this, &PowerBridge::onSwitchError);
    m_switchTimer.start(switchTimeoutMs());
    m_switch->start(program, command);
}
void PowerBridge::onSwitchFinished(int code, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && code == 0) {
        qInfo() << "microdap power: switch start returned 0, waiting for the session stop";
        dropSwitchProcess();
        return;
    }
    failSwitch(SwitchFailure::ExitCode, QStringLiteral("exit %1").arg(status == QProcess::NormalExit ? code : -1));
}

void PowerBridge::onSwitchError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    failSwitch(SwitchFailure::StartError, QStringLiteral("cannot start"));
}

void PowerBridge::onSwitchTimeout()
{
    failSwitch(SwitchFailure::Timeout, QStringLiteral("no session stop within %1 ms").arg(switchTimeoutMs()));
}

void PowerBridge::failSwitch(SwitchFailure kind, const QString &reason)
{
    qWarning().noquote() << "microdap power: switch failed," << reason;
    const bool wasSwitching = m_switching;
    const bool rebuild = m_switchReleased && m_switchPlayer && m_switchPlay != PlayerPort::PlayState::Stopped
                         && (kind == SwitchFailure::ExitCode || kind == SwitchFailure::StartError);
    m_switching = false;
    m_awaitReady = false;
    m_switchReleased = false;
    m_readyTimer.stop();
    m_switchTimer.stop();
    dropSwitchProcess();
    Keep::clear();
    if (rebuild) {
        const qint64 position = m_switchPlayer->positionMs();
        const bool playing = m_switchPlay == PlayerPort::PlayState::Playing;
        qInfo() << "microdap power: rebuild after the failed switch at" << position << (playing ? "playing" : "paused");
        m_switchPlayer->rebuildAt(position, playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
    }
    if (m_state)
        m_state->save(false);
    if (wasSwitching)
        emit switchingChanged();
    emit switchFailed();
}

void PowerBridge::dropSwitchProcess()
{
    if (!m_switch)
        return;
    m_switch->disconnect(this);
    if (m_switch->state() != QProcess::NotRunning) {
        m_switch->kill();
        m_switch->waitForFinished(1000);
    }
    m_switch->deleteLater();
    m_switch = nullptr;
}

void PowerBridge::requestScreenOff(bool off)
{
    if (m_overlay) {
        qInfo() << "microdap power: SetScreenOff ignored in overlay mode" << off;
        return;
    }
    if (off)
        screenOffNow();
    else
        screenOnNow(false);
}

void PowerBridge::showOverlay()
{
    if (rejectWithoutOverlay("ShowOverlay"))
        return;
    m_overlay->show();
}

void PowerBridge::hideOverlay()
{
    if (rejectWithoutOverlay("HideOverlay"))
        return;
    m_overlay->hide();
}

void PowerBridge::toggleOverlay()
{
    if (rejectWithoutOverlay("ToggleOverlay"))
        return;
    m_overlay->toggle();
}

bool PowerBridge::rejectWithoutOverlay(const char *member)
{
    if (m_overlay)
        return false;
    qInfo() << "microdap power:" << member << "needs overlay mode";
    if (calledFromDBus())
        sendErrorReply(notSupportedError(), QString::fromLatin1(member) + QStringLiteral(" needs overlay mode"));
    return true;
}

void PowerBridge::setVolume(int percent)
{
    m_volume = qBound(0, percent, 100);
    emit volumeChanged(m_volume);
}

void PowerBridge::onVolumeChanged(double level)
{
    setVolume(qRound(qBound(0.0, level, 1.0) * 100.0));
}

void PowerBridge::notifyPlaybackStatus()
{
    emitPropertiesChanged({{QStringLiteral("PlaybackStatus"), playbackStatus()}});
}

void PowerBridge::notifyOverlayShown()
{
    emitPropertiesChanged({{QStringLiteral("OverlayShown"), overlayShown()}});
}

void PowerBridge::emitPropertiesChanged(const QVariantMap &changed)
{
    if (!m_registered)
        return;
    QDBusMessage message = QDBusMessage::createSignal(objectPath(), propertiesInterface(),
                                                      QStringLiteral("PropertiesChanged"));
    message << interfaceName() << QVariant(changed) << QVariant(QStringList());
    m_bus.send(message);
}

void PowerBridge::emitScreenOffChanged()
{
    if (!m_registered)
        return;
    QDBusMessage message = QDBusMessage::createSignal(objectPath(), interfaceName(),
                                                      QStringLiteral("ScreenOffChanged"));
    message << m_screenOff;
    m_bus.send(message);
}

}
