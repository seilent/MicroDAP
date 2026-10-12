#include "PowerBridge.h"

#include "Keep.h"
#include "OutputMode.h"
#include "OverlayBridge.h"
#include "Player.h"
#include "SleepInhibitor.h"
#include "State.h"

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
    m_readyTimer.setSingleShot(true);
    connect(&m_readyTimer, &QTimer::timeout, this, [this] {
        if (!m_awaitReady)
            return;
        qWarning() << "casdap power: no output release within" << m_readyTimer.interval() << "ms, switching";
        onSwitchReady();
    });
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
    dropWakeProcess();
    dropScreenOffProcess();
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

QString PowerBridge::powerBin()
{
    return qEnvironmentVariable("CASDAP_POWER_BIN", QStringLiteral("/usr/libexec/casdap/casdap-power"));
}

QStringList PowerBridge::commandFor(const QString &verb)
{
    if (verb == QLatin1StringView("screen-off"))
        return {powerBin(), QStringLiteral("screen-off")};
    if (verb == QLatin1StringView("game-mode"))
        return {QStringLiteral("systemctl"), QStringLiteral("start"),
                QStringLiteral("casdap-switch-back.service")};
    if (verb == QLatin1StringView("suspend"))
        return {powerBin(), QStringLiteral("suspend")};
    if (verb == QLatin1StringView("poweroff"))
        return {powerBin(), QStringLiteral("poweroff")};
    return {};
}

QStringList PowerBridge::switchCommand()
{
    return {QStringLiteral("systemctl"), QStringLiteral("start"), QStringLiteral("casdap-switch-back.service")};
}

QStringList PowerBridge::wakeCommand()
{
    return {powerBin(), QStringLiteral("screen-on")};
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
    if (m_sleep) {
        m_sleep->release();
        m_sleep->deleteLater();
        m_sleep = nullptr;
    }
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
    return m_switching;
}

QString PowerBridge::programPath(const QString &name) const
{
    const QString dir = qEnvironmentVariable("CASDAP_VERB_DIR");
    if (dir.isEmpty() || name.contains(QLatin1Char('/')))
        return name;
    return dir + QLatin1Char('/') + name;
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

SleepInhibitor *PowerBridge::sleep() const
{
    return m_sleep;
}

bool PowerBridge::waking() const
{
    return m_wake != nullptr;
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
        m_sleep->release();
        return;
    }
    runScreenOn();
    if (m_outputMode)
        m_outputMode->prepareForSleep(false);
    m_sleep->take();
}

void PowerBridge::runScreenOn()
{
    dropWakeProcess();
    QStringList command = wakeCommand();
    const QString program = command.takeFirst();
    qInfo() << "casdap power: screen-on" << program << command;
    m_wake = new QProcess(this);
    connect(m_wake, &QProcess::finished, this, &PowerBridge::onWakeFinished);
    connect(m_wake, &QProcess::errorOccurred, this, &PowerBridge::onWakeError);
    m_wake->start(program, command);
}

void PowerBridge::onWakeFinished(int code, QProcess::ExitStatus status)
{
    const bool ok = status == QProcess::NormalExit && code == 0;
    if (!ok)
        qWarning() << "casdap power: screen-on after wake exited" << code;
    dropWakeProcess();
    emit wakeFinished(ok);
}

void PowerBridge::onWakeError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    qWarning() << "casdap power: cannot start" << wakeCommand().constFirst() << "after wake";
    dropWakeProcess();
    emit wakeFinished(false);
}

void PowerBridge::dropWakeProcess()
{
    if (!m_wake)
        return;
    m_wake->disconnect(this);
    if (m_wake->state() != QProcess::NotRunning) {
        m_wake->kill();
        m_wake->waitForFinished(1000);
    }
    m_wake->deleteLater();
    m_wake = nullptr;
}

bool PowerBridge::runVerb(const QString &verb)
{
    if (m_outputMode && m_outputMode->levelSet() && verb == QLatin1StringView("game-mode")) {
        qInfo() << "casdap power: verb" << verb;
        if (switching()) {
            qInfo() << "casdap power: switch already running";
            return false;
        }
        startSwitch();
        return true;
    }
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

void PowerBridge::runScreenOff()
{
    if (m_screenOffRun) {
        qInfo() << "casdap power: screen-off already running";
        return;
    }
    QStringList command = commandFor(QStringLiteral("screen-off"));
    const QString program = programPath(command.takeFirst());
    qInfo() << "casdap power: verb screen-off" << program << command;
    auto *process = new QProcess(this);
    m_screenOffRun = process;
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        if (m_screenOffRun != process)
            return;
        dropScreenOffProcess();
        if (status == QProcess::NormalExit && code == 0)
            return;
        qWarning() << "casdap power: screen-off exited" << code;
        emit verbFailed(QStringLiteral("screen-off"));
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || m_screenOffRun != process)
            return;
        qWarning() << "casdap power: cannot start" << process->program() << "for screen-off";
        dropScreenOffProcess();
        emit verbFailed(QStringLiteral("screen-off"));
    });
    process->start(program, command);
}

void PowerBridge::dropScreenOffProcess()
{
    if (!m_screenOffRun)
        return;
    QProcess *process = m_screenOffRun;
    m_screenOffRun = nullptr;
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        connect(process, &QProcess::finished, process, &QObject::deleteLater);
        process->kill();
        return;
    }
    process->deleteLater();
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
    if (switching()) {
        qInfo() << "casdap power: switch already running";
        return;
    }
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
    startSwitch();
}

void PowerBridge::closeAndSwitch()
{
    qInfo() << "casdap power: close player";
    if (switching()) {
        qInfo() << "casdap power: switch already running";
        return;
    }
    if (m_state)
        m_state->save(false);
    Keep::clear();
    startSwitch();
}

void PowerBridge::startSwitch()
{
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
    qInfo() << "casdap power: switch waits for the output release";
}

void PowerBridge::onSwitchReady()
{
    if (!m_awaitReady)
        return;
    m_awaitReady = false;
    m_readyTimer.stop();
    if (!m_switchReleased && m_switchPlayer && !m_switchWasParked && m_switchPlayer->parked()) {
        m_switchReleased = true;
        qInfo() << "casdap power: switch parked the player";
    }
    runSwitch();
}

void PowerBridge::runSwitch()
{
    QStringList command = switchCommand();
    const QString program = programPath(command.takeFirst());
    qInfo() << "casdap power: switch" << program << command;
    m_switch = new QProcess(this);
    connect(m_switch, &QProcess::finished, this, &PowerBridge::onSwitchFinished);
    connect(m_switch, &QProcess::errorOccurred, this, &PowerBridge::onSwitchError);
    m_switchTimer.start(switchTimeoutMs());
    m_switch->start(program, command);
}

void PowerBridge::onSwitchFinished(int code, QProcess::ExitStatus status)
{
    if (status == QProcess::NormalExit && code == 0) {
        qInfo() << "casdap power: switch start returned 0, waiting for the session stop";
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
    qWarning().noquote() << "casdap power: switch failed," << reason;
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
        qInfo() << "casdap power: rebuild after the failed switch at" << position << (playing ? "playing" : "paused");
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
