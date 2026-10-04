#include "PowerBridge.h"

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

void PowerBridgeAdaptor::SetScreenOff(bool off)
{
    qInfo() << "casdap power: SetScreenOff" << off;
    m_bridge->setScreenOff(off);
}

void PowerBridgeAdaptor::ShowPowerMenu()
{
    qInfo() << "casdap power: ShowPowerMenu";
    m_bridge->requestPowerMenu();
}

PowerBridge::PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_bus(bus)
    , m_adaptor(new PowerBridgeAdaptor(this))
{
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

bool PowerBridge::runVerb(const QString &verb)
{
    QStringList command = commandFor(verb);
    if (command.isEmpty()) {
        qWarning() << "casdap power: unknown verb" << verb;
        return false;
    }
    QString program = command.takeFirst();
    const QString dir = qEnvironmentVariable("CASDAP_VERB_DIR");
    if (!dir.isEmpty())
        program = dir + QLatin1Char('/') + program;
    qInfo() << "casdap power: verb" << verb << program << command;
    if (!QProcess::startDetached(program, command) && !m_failedVerbs.contains(verb)) {
        m_failedVerbs.insert(verb);
        qWarning() << "casdap power: cannot start" << program << "for" << verb;
    }
    return true;
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
    emit showPowerMenu();
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
