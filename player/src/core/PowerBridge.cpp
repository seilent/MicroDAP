#include "PowerBridge.h"

#include "OverlayBridge.h"
#include "Player.h"

#include <QDBusMessage>
#include <QDebug>
#include <QFile>
#include <QProcess>

namespace casdap {

namespace {

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
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
    qInfo() << "casdap power: SetScreenOff" << off;
    m_bridge->requestScreenOff(off);
}

void PowerBridgeAdaptor::ShowPowerMenu()
{
    qInfo() << "casdap power: ShowPowerMenu";
    m_bridge->requestPowerMenu();
}

void PowerBridgeAdaptor::ShowOverlay()
{
    qInfo() << "casdap power: ShowOverlay";
    m_bridge->showOverlay();
}

void PowerBridgeAdaptor::HideOverlay()
{
    qInfo() << "casdap power: HideOverlay";
    m_bridge->hideOverlay();
}

void PowerBridgeAdaptor::ToggleOverlay()
{
    qInfo() << "casdap power: ToggleOverlay";
    m_bridge->toggleOverlay();
}

void PowerBridgeAdaptor::ShowSwitchPrompt()
{
    qInfo() << "casdap power: ShowSwitchPrompt";
    m_bridge->requestSwitchPrompt();
}

PowerBridge::PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_bus(bus)
    , m_adaptor(new PowerBridgeAdaptor(this))
{
    m_switchTimer.setSingleShot(true);
    connect(&m_switchTimer, &QTimer::timeout, this, &PowerBridge::onSwitchTimeout);
    reloadDrift();
    if (m_player)
        connect(m_player, &Player::statusChanged, this, &PowerBridge::notifyPlaybackStatus);

    if (!m_bus.isConnected()) {
        qWarning() << "casdap power: session bus not connected";
        return;
    }
    if (!m_bus.connect(QString(), hotkeysPath(), hotkeysInterface(), QStringLiteral("VolumeChanged"),
                       this, SLOT(onVolumeChanged(double)))) {
        qWarning() << "casdap power: cannot subscribe to" << hotkeysInterface()
                   << m_bus.lastError().message();
    }
    if (!m_bus.registerObject(objectPath(), this, QDBusConnection::ExportAdaptors)) {
        qWarning() << "casdap power: cannot register" << objectPath()
                   << m_bus.lastError().message();
        return;
    }
    if (!m_bus.registerService(serviceName())) {
        qWarning() << "casdap power: cannot register" << serviceName()
                   << m_bus.lastError().message();
        m_bus.unregisterObject(objectPath());
        return;
    }
    m_registered = true;
}

PowerBridge::~PowerBridge()
{
    dropSwitchProcess();
    if (!m_registered)
        return;
    m_bus.unregisterService(serviceName());
    m_bus.unregisterObject(objectPath());
}

QString PowerBridge::serviceName()
{
    return QStringLiteral("org.casdap.Player");
}

QString PowerBridge::objectPath()
{
    return QStringLiteral("/org/casdap/Player");
}

QString PowerBridge::interfaceName()
{
    return QStringLiteral("org.casdap.Player");
}

QString PowerBridge::hotkeysPath()
{
    return QStringLiteral("/org/casdap/Hotkeys");
}

QString PowerBridge::hotkeysInterface()
{
    return QStringLiteral("org.casdap.Hotkeys");
}

QString PowerBridge::notSupportedError()
{
    return QStringLiteral("org.casdap.Error.NotSupported");
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

QStringList PowerBridge::commandFor(const QString &verb)
{
    if (verb == QLatin1StringView("screen-off"))
        return {QStringLiteral("casdap-power"), QStringLiteral("screen-off")};
    if (verb == QLatin1StringView("game-mode"))
        return {QStringLiteral("systemctl"), QStringLiteral("start"),
                QStringLiteral("casdap-switch-back.service")};
    if (verb == QLatin1StringView("suspend"))
        return {QStringLiteral("casdap-power"), QStringLiteral("suspend")};
    if (verb == QLatin1StringView("poweroff"))
        return {QStringLiteral("casdap-power"), QStringLiteral("poweroff")};
    return {};
}

QStringList PowerBridge::switchCommand()
{
    return {QStringLiteral("systemctl"), QStringLiteral("start"), QStringLiteral("casdap-switch-back.service")};
}

int PowerBridge::switchTimeoutMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("CASDAP_SWITCH_TIMEOUT_MS", &ok);
    return ok && value > 0 ? value : 30000;
}

QString PowerBridge::driftFile()
{
    return qEnvironmentVariable("CASDAP_DRIFT_FILE", QStringLiteral("/run/casdap/drift"));
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
    connect(m_overlay, &OverlayBridge::shownChanged, this, &PowerBridge::notifyOverlayShown);
    connect(m_overlay, &QObject::destroyed, this, [this] { m_overlay = nullptr; });
}

bool PowerBridge::overlayMode() const
{
    return m_overlay != nullptr;
}

Keep *PowerBridge::keep()
{
    return &m_keep;
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
}

int PowerBridge::volume() const
{
    return m_volume;
}

QStringList PowerBridge::driftPaths() const
{
    return m_driftPaths;
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
    return m_switchTimer.isActive();
}

QString PowerBridge::programPath(const QString &name) const
{
    const QString dir = qEnvironmentVariable("CASDAP_VERB_DIR");
    if (dir.isEmpty())
        return name;
    return dir + QLatin1Char('/') + name;
}

bool PowerBridge::runVerb(const QString &verb)
{
    QStringList command = commandFor(verb);
    if (command.isEmpty()) {
        qWarning() << "casdap power: unknown verb" << verb;
        return false;
    }
    const QString program = programPath(command.takeFirst());
    qInfo() << "casdap power: verb" << verb << program << command;
    const bool started = QProcess::startDetached(program, command);
    if (!started && !m_failedVerbs.contains(verb)) {
        m_failedVerbs.insert(verb);
        qWarning() << "casdap power: cannot start" << program << "for" << verb;
    }
    return started;
}

void PowerBridge::reloadDrift()
{
    QStringList paths;
    QFile file(driftFile());
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (!line.isEmpty())
                paths.append(line);
        }
    }
    if (paths == m_driftPaths)
        return;
    m_driftPaths = paths;
    emit driftPathsChanged();
}

void PowerBridge::requestPowerMenu()
{
    if (m_overlay) {
        qInfo() << "casdap power: ShowPowerMenu ignored in overlay mode";
        return;
    }
    emit showPowerMenu();
}

void PowerBridge::requestSwitchPrompt()
{
    if (m_overlay) {
        qInfo() << "casdap power: ShowSwitchPrompt needs dap mode";
        if (calledFromDBus())
            sendErrorReply(notSupportedError(), QStringLiteral("ShowSwitchPrompt needs dap mode"));
        return;
    }
    emit showSwitchPrompt();
}

void PowerBridge::keepInBackground()
{
    qInfo() << "casdap power: keep in background";
    m_keep.setFlag(true);
    startSwitch();
}

void PowerBridge::closeAndSwitch()
{
    qInfo() << "casdap power: close player";
    m_keep.setFlag(false);
    Keep::clear();
    startSwitch();
}

void PowerBridge::startSwitch()
{
    if (switching()) {
        qInfo() << "casdap power: switch already running";
        return;
    }
    QStringList command = switchCommand();
    const QString program = programPath(command.takeFirst());
    qInfo() << "casdap power: switch" << program << command;
    m_switch = new QProcess(this);
    connect(m_switch, &QProcess::finished, this, &PowerBridge::onSwitchFinished);
    connect(m_switch, &QProcess::errorOccurred, this, &PowerBridge::onSwitchError);
    m_switchTimer.start(switchTimeoutMs());
    emit switchingChanged();
    m_switch->start(program, command);
}

void PowerBridge::onSwitchFinished(int code, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && code == 0) {
        qInfo() << "casdap power: switch start returned 0, waiting for the session stop";
        dropSwitchProcess();
        return;
    }
    failSwitch(QStringLiteral("exit %1").arg(status == QProcess::NormalExit ? code : -1));
}

void PowerBridge::onSwitchError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    failSwitch(QStringLiteral("cannot start"));
}

void PowerBridge::onSwitchTimeout()
{
    failSwitch(QStringLiteral("no session stop within %1 ms").arg(switchTimeoutMs()));
}

void PowerBridge::failSwitch(const QString &reason)
{
    qWarning().noquote() << "casdap power: switch failed," << reason;
    m_switchTimer.stop();
    dropSwitchProcess();
    m_keep.setFlag(false);
    Keep::clear();
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
        qInfo() << "casdap power: SetScreenOff ignored in overlay mode" << off;
        return;
    }
    setScreenOff(off);
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
    qInfo() << "casdap power:" << member << "needs overlay mode";
    if (calledFromDBus())
        sendErrorReply(notSupportedError(), QString::fromLatin1(member) + QStringLiteral(" needs overlay mode"));
    return true;
}

void PowerBridge::onVolumeChanged(double level)
{
    m_volume = qRound(qBound(0.0, level, 1.0) * 100.0);
    emit volumeChanged(m_volume);
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
