#include "SystemBluezModel.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>

namespace casdap {

namespace {

constexpr int positionPollMs = 1000;

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString device1Interface()
{
    return QStringLiteral("org.bluez.Device1");
}

QString mediaTransport1Interface()
{
    return QStringLiteral("org.bluez.MediaTransport1");
}

QString mediaPlayer1Interface()
{
    return QStringLiteral("org.bluez.MediaPlayer1");
}

QString battery1Interface()
{
    return QStringLiteral("org.bluez.Battery1");
}

QVariant unwrap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

QVariantMap variantMap(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    if (inner.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QVariantMap>(qvariant_cast<QDBusArgument>(inner));
    return inner.toMap();
}

QStringList stringList(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    if (inner.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QStringList>(qvariant_cast<QDBusArgument>(inner));
    return inner.toStringList();
}

QString objectPathValue(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    if (inner.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return qvariant_cast<QDBusObjectPath>(inner).path();
    return inner.toString();
}

bool hasA2dpSource(const QVariant &uuids)
{
    const QStringList list = stringList(uuids);
    for (const QString &uuid : list) {
        if (uuid.startsWith(QLatin1StringView("0000110a"), Qt::CaseInsensitive))
            return true;
    }
    return false;
}

}

SystemBluezModel::SystemBluezModel(const QDBusConnection &bluez, const QDBusConnection &session,
                                   QObject *parent)
    : BluezModel(parent)
    , m_bluez(bluez)
    , m_session(session)
    , m_bluezWatcher(bluezService(), bluez, QDBusServiceWatcher::WatchForOwnerChange)
    , m_agentWatcher(agentService(), session, QDBusServiceWatcher::WatchForOwnerChange)
{
    registerMetaTypes();

    m_positionTimer.setInterval(positionPollMs);
    connect(&m_positionTimer, &QTimer::timeout, this, &SystemBluezModel::pollPosition);
    connect(this, &BluezModel::activeChanged, this, &SystemBluezModel::syncPolling);
    connect(&m_bluezWatcher, &QDBusServiceWatcher::serviceRegistered, this,
            &SystemBluezModel::refresh);
    connect(&m_bluezWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &SystemBluezModel::onBluezUnregistered);
    connect(&m_agentWatcher, &QDBusServiceWatcher::serviceRegistered, this,
            &SystemBluezModel::refreshAgent);
    connect(&m_agentWatcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &SystemBluezModel::onAgentUnregistered);

    if (m_bluez.isConnected()) {
        updateReady(false);
        if (!m_bluez.connect(bluezService(), QStringLiteral("/"), objectManagerInterface(),
                             QStringLiteral("InterfacesAdded"), this,
                             SLOT(onInterfacesAdded(QDBusMessage)))) {
            qWarning() << "casdap bluez: cannot subscribe to InterfacesAdded"
                       << m_bluez.lastError().message();
        }
        if (!m_bluez.connect(bluezService(), QStringLiteral("/"), objectManagerInterface(),
                             QStringLiteral("InterfacesRemoved"), this,
                             SLOT(onInterfacesRemoved(QDBusMessage)))) {
            qWarning() << "casdap bluez: cannot subscribe to InterfacesRemoved"
                       << m_bluez.lastError().message();
        }
        if (!m_bluez.connect(bluezService(), QString(), propertiesInterface(),
                             QStringLiteral("PropertiesChanged"), this,
                             SLOT(onBluezPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)))) {
            qWarning() << "casdap bluez: cannot subscribe to PropertiesChanged"
                       << m_bluez.lastError().message();
        }
        refresh();
    } else {
        qWarning() << "casdap bluez: bus not connected";
    }

    if (m_session.isConnected()) {
        if (!m_session.connect(agentService(), agentPath(), propertiesInterface(),
                               QStringLiteral("PropertiesChanged"), this,
                               SLOT(onAgentPropertiesChanged(QString,QVariantMap,QStringList)))) {
            qWarning() << "casdap bluez: cannot subscribe to" << agentInterface()
                       << m_session.lastError().message();
        }
        refreshAgent();
    } else {
        qWarning() << "casdap bluez: session bus not connected";
    }
}

void SystemBluezModel::registerMetaTypes()
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();
}

QString SystemBluezModel::bluezService()
{
    return QStringLiteral("org.bluez");
}

QString SystemBluezModel::agentService()
{
    return QStringLiteral("org.casdap.Bluetooth");
}

QString SystemBluezModel::agentPath()
{
    return QStringLiteral("/org/casdap/Bluetooth");
}

QString SystemBluezModel::agentInterface()
{
    return QStringLiteral("org.casdap.Bluetooth");
}

QString SystemBluezModel::a2dpSourceUuid()
{
    return QStringLiteral("0000110a-0000-1000-8000-00805f9b34fb");
}

QString SystemBluezModel::devicePath() const
{
    return m_devicePath;
}

QString SystemBluezModel::transportPath() const
{
    return m_transportPath;
}

QString SystemBluezModel::playerPath() const
{
    return m_playerPath;
}

bool SystemBluezModel::pollingPosition() const
{
    return m_positionTimer.isActive();
}

void SystemBluezModel::refresh()
{
    if (!m_bluez.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), QStringLiteral("/"),
                                                       objectManagerInterface(),
                                                       QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(m_bluez.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "casdap bluez: GetManagedObjects failed" << reply.errorMessage();
            m_objects.clear();
            apply();
            updateReady(true);
            return;
        }
        const ManagedObjectList objects = qdbus_cast<ManagedObjectList>(reply.arguments().value(0));
        m_objects.clear();
        for (auto it = objects.cbegin(); it != objects.cend(); ++it)
            m_objects.insert(it.key().path(), it.value());
        apply();
        updateReady(true);
    });
}

void SystemBluezModel::refreshAgent()
{
    if (!m_session.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(agentService(), agentPath(),
                                                       propertiesInterface(),
                                                       QStringLiteral("GetAll"));
    call << agentInterface();
    const quint64 generation = m_agentGeneration;
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_agentGeneration)
            return;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            applyAgent({});
            return;
        }
        applyAgent(qdbus_cast<QVariantMap>(reply.arguments().value(0)));
    });
}

void SystemBluezModel::applyAgent(const QVariantMap &properties)
{
    updatePairingRemaining(properties.value(QStringLiteral("PairingRemaining")).toInt());
    updatePairing(properties.value(QStringLiteral("PairingActive")).toBool());
}

void SystemBluezModel::onAgentPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                                const QStringList &invalidated)
{
    if (interface != agentInterface())
        return;
    ++m_agentGeneration;
    if (changed.contains(QStringLiteral("PairingRemaining")))
        updatePairingRemaining(changed.value(QStringLiteral("PairingRemaining")).toInt());
    if (changed.contains(QStringLiteral("PairingActive")))
        updatePairing(changed.value(QStringLiteral("PairingActive")).toBool());
    if (invalidated.contains(QStringLiteral("PairingRemaining"))
        || invalidated.contains(QStringLiteral("PairingActive")))
        refreshAgent();
}

void SystemBluezModel::onAgentUnregistered()
{
    ++m_agentGeneration;
    applyAgent({});
}

void SystemBluezModel::onBluezUnregistered()
{
    m_objects.clear();
    apply();
}

void SystemBluezModel::onInterfacesAdded(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2)
        return;
    const QString path = objectPathValue(arguments.at(0));
    const InterfaceList added = qdbus_cast<InterfaceList>(arguments.at(1));
    if (path.isEmpty())
        return;
    InterfaceList &object = m_objects[path];
    for (auto it = added.cbegin(); it != added.cend(); ++it)
        object.insert(it.key(), it.value());
    apply();
}

void SystemBluezModel::onInterfacesRemoved(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2)
        return;
    const QString path = objectPathValue(arguments.at(0));
    const QStringList removed = stringList(arguments.at(1));
    auto it = m_objects.find(path);
    if (it == m_objects.end())
        return;
    for (const QString &interface : removed)
        it->remove(interface);
    if (it->isEmpty())
        m_objects.erase(it);
    apply();
}

void SystemBluezModel::onBluezPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                                const QStringList &invalidated,
                                                const QDBusMessage &message)
{
    if (!interface.startsWith(QLatin1StringView("org.bluez.")))
        return;
    auto it = m_objects.find(message.path());
    if (it == m_objects.end() || !it->contains(interface))
        return;
    QVariantMap &properties = (*it)[interface];
    for (auto change = changed.cbegin(); change != changed.cend(); ++change)
        properties.insert(change.key(), change.value());
    for (const QString &name : invalidated)
        properties.remove(name);
    apply();
}

QString SystemBluezModel::childWithInterface(const QString &devicePath,
                                             const QString &interface) const
{
    QString byPrefix;
    const QString prefix = devicePath + QLatin1Char('/');
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!it->contains(interface))
            continue;
        const QVariantMap properties = it->value(interface);
        if (properties.contains(QStringLiteral("Device"))) {
            if (objectPathValue(properties.value(QStringLiteral("Device"))) == devicePath)
                return it.key();
            continue;
        }
        if (byPrefix.isEmpty() && it.key().startsWith(prefix))
            byPrefix = it.key();
    }
    return byPrefix;
}

void SystemBluezModel::select(QString &devicePath, QString &transportPath,
                              QString &playerPath) const
{
    QString fallback;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!it->contains(device1Interface()))
            continue;
        const QVariantMap device = it->value(device1Interface());
        if (!device.value(QStringLiteral("Connected")).toBool())
            continue;
        const QString transport = childWithInterface(it.key(), mediaTransport1Interface());
        if (!transport.isEmpty()) {
            devicePath = it.key();
            transportPath = transport;
            break;
        }
        if (fallback.isEmpty() && hasA2dpSource(device.value(QStringLiteral("UUIDs"))))
            fallback = it.key();
    }
    if (devicePath.isEmpty())
        devicePath = fallback;
    if (!devicePath.isEmpty())
        playerPath = childWithInterface(devicePath, mediaPlayer1Interface());
}

void SystemBluezModel::clearDevice()
{
    updateTransport(false, QStringLiteral("none"));
    updateStatus(QStringLiteral("stopped"));
    updateTitle({});
    updateArtist({});
    updateAlbum({});
    updateTrackNumber(0);
    updateNumberOfTracks(0);
    updateDurationMs(0);
    updatePositionMs(0);
    updateShuffle(QStringLiteral("off"));
    updateRepeat(QStringLiteral("off"));
    updateBattery(-1);
    updateConnected(false);
    updateDeviceName({});
    m_transportVolume = -1;
}

void SystemBluezModel::apply()
{
    QString devicePath;
    QString transportPath;
    QString playerPath;
    select(devicePath, transportPath, playerPath);
    m_devicePath = devicePath;
    m_transportPath = transportPath;
    m_playerPath = playerPath;

    if (devicePath.isEmpty()) {
        clearDevice();
        syncPolling();
        return;
    }

    const InterfaceList object = m_objects.value(devicePath);
    const QVariantMap device = object.value(device1Interface());
    const QVariantMap transport = m_objects.value(transportPath).value(mediaTransport1Interface());
    const QVariantMap player = m_objects.value(playerPath).value(mediaPlayer1Interface());
    const QVariantMap track = variantMap(player.value(QStringLiteral("Track")));

    updateDeviceName(device.value(QStringLiteral("Alias"),
                                  device.value(QStringLiteral("Name"))).toString());
    if (object.contains(battery1Interface()))
        updateBattery(object.value(battery1Interface()).value(QStringLiteral("Percentage")).toInt());
    else
        updateBattery(-1);

    const QString state = transport.value(QStringLiteral("State")).toString();
    if (transportPath.isEmpty())
        updateTransport(false, QStringLiteral("none"));
    else if (state == QLatin1StringView("active") || state == QLatin1StringView("pending"))
        updateTransport(state == QLatin1StringView("active"), state);
    else
        updateTransport(false, QStringLiteral("idle"));
    if (transport.contains(QStringLiteral("Volume"))) {
        const int raw = transport.value(QStringLiteral("Volume")).toInt();
        if (raw != m_transportVolume) {
            m_transportVolume = raw;
            updateVolume(volumeFromTransport(raw));
        }
    }

    updateTitle(track.value(QStringLiteral("Title")).toString());
    updateArtist(track.value(QStringLiteral("Artist")).toString());
    updateAlbum(track.value(QStringLiteral("Album")).toString());
    updateTrackNumber(track.value(QStringLiteral("TrackNumber")).toInt());
    updateNumberOfTracks(track.value(QStringLiteral("NumberOfTracks")).toInt());
    updateDurationMs(track.value(QStringLiteral("Duration")).toLongLong());
    updatePositionMs(player.value(QStringLiteral("Position")).toLongLong());
    updateStatus(player.contains(QStringLiteral("Status"))
                     ? player.value(QStringLiteral("Status")).toString()
                     : QStringLiteral("stopped"));
    updateShuffle(player.value(QStringLiteral("Shuffle"), QStringLiteral("off")).toString());
    updateRepeat(player.value(QStringLiteral("Repeat"), QStringLiteral("off")).toString());
    updateConnected(true);
    syncPolling();
}

void SystemBluezModel::syncPolling()
{
    const bool run = active() && !m_playerPath.isEmpty()
                     && status() == QLatin1StringView("playing");
    if (run) {
        if (!m_positionTimer.isActive())
            m_positionTimer.start();
    } else {
        m_positionTimer.stop();
    }
}

void SystemBluezModel::pollPosition()
{
    if (m_playerPath.isEmpty() || m_positionPending || !m_bluez.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), m_playerPath,
                                                       propertiesInterface(), QStringLiteral("Get"));
    call << mediaPlayer1Interface() << QStringLiteral("Position");
    const QString path = m_playerPath;
    m_positionPending = true;
    auto *watcher = new QDBusPendingCallWatcher(m_bluez.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        m_positionPending = false;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage || path != m_playerPath)
            return;
        auto it = m_objects.find(path);
        if (it == m_objects.end() || !it->contains(mediaPlayer1Interface()))
            return;
        const QVariant value = unwrap(reply.arguments().value(0));
        (*it)[mediaPlayer1Interface()].insert(QStringLiteral("Position"), value);
        updatePositionMs(value.toLongLong());
    });
}

void SystemBluezModel::callPlayer(const QString &method)
{
    if (m_playerPath.isEmpty() || !m_bluez.isConnected())
        return;
    m_bluez.asyncCall(QDBusMessage::createMethodCall(bluezService(), m_playerPath,
                                                     mediaPlayer1Interface(), method));
}

void SystemBluezModel::setPlayerProperty(const QString &name, const QString &value)
{
    if (m_playerPath.isEmpty() || !m_bluez.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), m_playerPath,
                                                       propertiesInterface(), QStringLiteral("Set"));
    call << mediaPlayer1Interface() << name << QVariant::fromValue(QDBusVariant(value));
    m_bluez.asyncCall(call);
}

void SystemBluezModel::callAgent(const QString &method, const QList<QVariant> &arguments)
{
    if (!m_session.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(agentService(), agentPath(),
                                                       agentInterface(), method);
    call.setArguments(arguments);
    m_session.asyncCall(call);
}

void SystemBluezModel::play()
{
    callPlayer(QStringLiteral("Play"));
}

void SystemBluezModel::pause()
{
    callPlayer(QStringLiteral("Pause"));
}

void SystemBluezModel::next()
{
    callPlayer(QStringLiteral("Next"));
}

void SystemBluezModel::previous()
{
    callPlayer(QStringLiteral("Previous"));
}

void SystemBluezModel::rewind()
{
    callPlayer(QStringLiteral("Rewind"));
}

void SystemBluezModel::fastForward()
{
    callPlayer(QStringLiteral("FastForward"));
}

void SystemBluezModel::startPairing(int timeoutSeconds)
{
    const uint seconds = static_cast<uint>(qMax(1, timeoutSeconds));
    callAgent(QStringLiteral("StartPairing"), {QVariant::fromValue(seconds)});
}

void SystemBluezModel::stopPairing()
{
    callAgent(QStringLiteral("StopPairing"));
}

void SystemBluezModel::setShuffle(const QString &shuffle)
{
    setPlayerProperty(QStringLiteral("Shuffle"), shuffle);
}

void SystemBluezModel::setRepeat(const QString &repeat)
{
    setPlayerProperty(QStringLiteral("Repeat"), repeat);
}

}
