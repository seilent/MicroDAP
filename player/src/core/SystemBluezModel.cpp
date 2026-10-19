#include "SystemBluezModel.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace microdap {

namespace {

constexpr int positionPollMs = 1000;
constexpr int artGateMs = 1000;
constexpr int nudgeStepMs = 3000;
constexpr int nudgeWatchMs = 2000;

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

QStringList objectPaths(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    QStringList out;
    if (inner.metaType() == QMetaType::fromType<QDBusArgument>()) {
        for (const QDBusObjectPath &path : qdbus_cast<QList<QDBusObjectPath>>(qvariant_cast<QDBusArgument>(inner)))
            out.append(path.path());
    } else if (inner.metaType() == QMetaType::fromType<QList<QDBusObjectPath>>()) {
        for (const QDBusObjectPath &path : inner.value<QList<QDBusObjectPath>>())
            out.append(path.path());
    }
    return out;
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

bool hasUuid(const QVariant &uuids, QLatin1StringView prefix)
{
    const QStringList list = stringList(uuids);
    for (const QString &uuid : list) {
        if (uuid.startsWith(prefix, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

bool hasA2dpSource(const QVariant &uuids)
{
    return hasUuid(uuids, QLatin1StringView("0000110a"));
}

bool isHeadset(const QVariant &uuids)
{
    return hasUuid(uuids, QLatin1StringView("0000110b")) && !hasA2dpSource(uuids);
}

bool inRole(const QVariantMap &device, const QString &role)
{
    const QVariant uuids = device.value(QStringLiteral("UUIDs"));
    return role == QLatin1StringView("receiver") ? hasA2dpSource(uuids) : isHeadset(uuids);
}

bool isAudioSink(const QVariantMap &device)
{
    const uint major = (device.value(QStringLiteral("Class")).toUInt() >> 8) & 0x1f;
    return major == 0x04 || hasUuid(device.value(QStringLiteral("UUIDs")), QLatin1StringView("0000110b"));
}

QString aliasOf(const QVariantMap &device)
{
    return device.value(QStringLiteral("Alias"), device.value(QStringLiteral("Name"))).toString();
}

QVariantList sortedByName(QVariantList list)
{
    std::stable_sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return QString::compare(a.toMap().value(QStringLiteral("name")).toString(),
                                b.toMap().value(QStringLiteral("name")).toString(), Qt::CaseInsensitive) < 0;
    });
    return list;
}

void writeLine(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate) || file.write(text.toUtf8() + '\n') < 0
        || !file.commit())
        qWarning() << "microdap bluez: cannot write" << path;
}

int roleCheckMs()
{
    bool ok = false;
    const int value = qEnvironmentVariableIntValue("MICRODAP_BT_ROLE_CHECK_MS", &ok);
    return ok && value > 0 ? value : 10000;
}

}

SystemBluezModel::SystemBluezModel(const QDBusConnection &bluez, const QDBusConnection &session,
                                   QObject *parent)
    : BluezModel(parent)
    , m_bluez(bluez)
    , m_session(session)
    , m_bluezWatcher(bluezService(), bluez, QDBusServiceWatcher::WatchForOwnerChange)
    , m_agentWatcher(agentService(), session, QDBusServiceWatcher::WatchForOwnerChange)
    , m_art(session)
    , m_engineWatcher(engineService(), session, QDBusServiceWatcher::WatchForOwnerChange)
{
    registerMetaTypes();
    QFile roleFile(rolePath());
    if (roleFile.open(QIODevice::ReadOnly))
        restoreRole(QString::fromUtf8(roleFile.read(64)).trimmed());
    QFile last(lastPhonePath());
    if (last.open(QIODevice::ReadOnly))
        updateLastPhone(QString::fromUtf8(last.read(1024)).trimmed());
    connect(this, &BluezModel::lastPhoneChanged, this, [this] { writeLine(lastPhonePath(), lastPhone()); });
    QFile lastHeadsetFile(lastHeadsetPath());
    if (lastHeadsetFile.open(QIODevice::ReadOnly))
        updateLastHeadset(QString::fromUtf8(lastHeadsetFile.read(1024)).trimmed());
    connect(this, &BluezModel::lastHeadsetChanged, this, [this] { writeLine(lastHeadsetPath(), lastHeadset()); });
    connect(this, &BluezModel::roleChanged, this, [this] {
        writeLine(rolePath(), role());
        callEngine(QStringLiteral("SetRole"), {role()});
    });
    connect(this, &BluezModel::switchingChanged, this, [this] { callEngine(QStringLiteral("Hold"), {switching()}); });
    connect(&m_engineWatcher, &QDBusServiceWatcher::serviceRegistered, this, [this] {
        m_engineUp = true;
        syncEngine();
    });
    connect(&m_engineWatcher, &QDBusServiceWatcher::serviceUnregistered, this, [this] { m_engineUp = false; });

    m_gateTimer.setSingleShot(true);
    connect(&m_gateTimer, &QTimer::timeout, this, &SystemBluezModel::apply);
    m_nudgeTimer.setSingleShot(true);
    connect(&m_nudgeTimer, &QTimer::timeout, this, &SystemBluezModel::nudgeTimeout);
    connect(&m_art, &PhoneArt::artChanged, this, &SystemBluezModel::updateArt);
    connect(&m_art, &PhoneArt::sessionOpenChanged, this, &SystemBluezModel::apply, Qt::QueuedConnection);

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
            qWarning() << "microdap bluez: cannot subscribe to InterfacesAdded"
                       << m_bluez.lastError().message();
        }
        if (!m_bluez.connect(bluezService(), QStringLiteral("/"), objectManagerInterface(),
                             QStringLiteral("InterfacesRemoved"), this,
                             SLOT(onInterfacesRemoved(QDBusMessage)))) {
            qWarning() << "microdap bluez: cannot subscribe to InterfacesRemoved"
                       << m_bluez.lastError().message();
        }
        if (!m_bluez.connect(bluezService(), QString(), propertiesInterface(),
                             QStringLiteral("PropertiesChanged"), this,
                             SLOT(onBluezPropertiesChanged(QString,QVariantMap,QStringList,QDBusMessage)))) {
            qWarning() << "microdap bluez: cannot subscribe to PropertiesChanged"
                       << m_bluez.lastError().message();
        }
        refresh();
    } else {
        qWarning() << "microdap bluez: bus not connected";
    }

    if (m_session.isConnected()) {
        if (!m_session.connect(agentService(), agentPath(), propertiesInterface(),
                               QStringLiteral("PropertiesChanged"), this,
                               SLOT(onAgentPropertiesChanged(QString,QVariantMap,QStringList)))) {
            qWarning() << "microdap bluez: cannot subscribe to" << agentInterface()
                       << m_session.lastError().message();
        }
        m_session.connect(agentService(), agentPath(), agentInterface(), QStringLiteral("CompanionPaired"), this,
                          SLOT(onCompanionPaired(QDBusObjectPath)));
        refreshAgent();
        if (role() != QLatin1StringView("receiver"))
            applyRole(role());
        m_engineUp = m_session.interface() && m_session.interface()->isServiceRegistered(engineService()).value();
        syncEngine();
    } else {
        qWarning() << "microdap bluez: session bus not connected";
    }
}

SystemBluezModel::~SystemBluezModel()
{
    disconnect(&m_art, nullptr, this, nullptr);
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
    return QStringLiteral("net.seilent.MicroDap.Bluetooth");
}

QString SystemBluezModel::agentPath()
{
    return QStringLiteral("/net/seilent/MicroDap/Bluetooth");
}

QString SystemBluezModel::agentInterface()
{
    return QStringLiteral("net.seilent.MicroDap.Bluetooth");
}

QString SystemBluezModel::a2dpSourceUuid()
{
    return QStringLiteral("0000110a-0000-1000-8000-00805f9b34fb");
}

QString SystemBluezModel::engineService()
{
    return QStringLiteral("net.seilent.MicroOs.Reconnect");
}

QString SystemBluezModel::enginePath()
{
    return QStringLiteral("/net/seilent/MicroOs/Reconnect");
}

QString SystemBluezModel::engineInterface()
{
    return QStringLiteral("net.seilent.MicroOs.Reconnect1");
}

void SystemBluezModel::callEngine(const QString &method, const QList<QVariant> &arguments)
{
    if (!m_engineUp || !m_session.isConnected())
        return;
    QDBusMessage call = QDBusMessage::createMethodCall(engineService(), enginePath(), engineInterface(), method);
    call.setArguments(arguments);
    m_session.asyncCall(call);
}

void SystemBluezModel::syncEngine()
{
    if (!m_engineUp)
        return;
    callEngine(QStringLiteral("SetRole"), {role()});
    if (switching())
        callEngine(QStringLiteral("Hold"), {true});
    if (m_dap && m_scope > 0)
        callEngine(QStringLiteral("Scope"), {true});
}

void SystemBluezModel::setDapSession(bool dap)
{
    m_dap = dap;
}

void SystemBluezModel::want()
{
    if (m_dap)
        callEngine(QStringLiteral("Want"));
}

void SystemBluezModel::setReceiverScope(bool held)
{
    const int before = m_scope;
    m_scope = std::max(0, m_scope + (held ? 1 : -1));
    if (m_dap && (before > 0) != (m_scope > 0))
        callEngine(QStringLiteral("Scope"), {m_scope > 0});
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

PhoneArt *SystemBluezModel::phoneArt()
{
    return &m_art;
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
            qInfo() << "microdap bluez: GetManagedObjects failed" << reply.errorMessage();
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
    updatePairingMode(properties.value(QStringLiteral("PairingMode")).toString());
    updateConfirm(properties.value(QStringLiteral("ConfirmPending")).toBool(),
                  properties.value(QStringLiteral("ConfirmPasskey")).toInt(),
                  properties.value(QStringLiteral("ConfirmName")).toString());
    updateCompanionSets(objectPaths(properties.value(QStringLiteral("CompanionKnown"))),
                        objectPaths(properties.value(QStringLiteral("CompanionRemoving"))));
    updatePairing(properties.value(QStringLiteral("PairingActive")).toBool());
    updateSearchRemaining(properties.value(QStringLiteral("SearchRemaining")).toInt());
    updateSearching(properties.value(QStringLiteral("SearchActive")).toBool());
    if (properties.contains(QStringLiteral("Role"))
        && properties.value(QStringLiteral("Role")).toString() != activeRole())
        applyRole(activeRole());
}

void SystemBluezModel::onAgentPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                                const QStringList &invalidated)
{
    if (interface != agentInterface())
        return;
    ++m_agentGeneration;
    if (changed.contains(QStringLiteral("PairingRemaining")))
        updatePairingRemaining(changed.value(QStringLiteral("PairingRemaining")).toInt());
    if (changed.contains(QStringLiteral("PairingMode")))
        updatePairingMode(changed.value(QStringLiteral("PairingMode")).toString());
    if (changed.contains(QStringLiteral("ConfirmPending")) || changed.contains(QStringLiteral("ConfirmPasskey"))
        || changed.contains(QStringLiteral("ConfirmName")))
        updateConfirm(changed.value(QStringLiteral("ConfirmPending"), confirmPending()).toBool(),
                      changed.value(QStringLiteral("ConfirmPasskey"), confirmPasskey()).toInt(),
                      changed.value(QStringLiteral("ConfirmName"), confirmName()).toString());
    if (changed.contains(QStringLiteral("CompanionKnown")) || changed.contains(QStringLiteral("CompanionRemoving")))
        updateCompanionSets(changed.contains(QStringLiteral("CompanionKnown"))
                                ? objectPaths(changed.value(QStringLiteral("CompanionKnown"))) : companionKnown(),
                            changed.contains(QStringLiteral("CompanionRemoving"))
                                ? objectPaths(changed.value(QStringLiteral("CompanionRemoving"))) : companionRemoving());
    if (changed.contains(QStringLiteral("PairingActive")))
        updatePairing(changed.value(QStringLiteral("PairingActive")).toBool());
    if (changed.contains(QStringLiteral("SearchRemaining")))
        updateSearchRemaining(changed.value(QStringLiteral("SearchRemaining")).toInt());
    const bool wasSearching = searching();
    if (changed.contains(QStringLiteral("SearchActive")))
        updateSearching(changed.value(QStringLiteral("SearchActive")).toBool());
    if (wasSearching && !searching())
        m_pairAfterSearch.clear();
    if (searching() && !m_pairAfterSearch.isEmpty())
        startPair(std::exchange(m_pairAfterSearch, QString()));
    if (invalidated.contains(QStringLiteral("PairingRemaining"))
        || invalidated.contains(QStringLiteral("PairingActive"))
        || invalidated.contains(QStringLiteral("SearchRemaining"))
        || invalidated.contains(QStringLiteral("SearchActive")))
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
    const QString path = message.path();
    if (interface == device1Interface() && changed.contains(QStringLiteral("Connected"))) {
        if (changed.value(QStringLiteral("Connected")).toBool())
            checkHeadset(path);
        else if (path == m_connectTarget)
            m_connectTarget.clear();
    }
    apply();
    if (interface != device1Interface())
        return;
    if (changed.value(QStringLiteral("ServicesResolved")).toBool() || changed.contains(QStringLiteral("UUIDs")))
        checkRole(path);
    if (changed.contains(QStringLiteral("Connected")) && !changed.value(QStringLiteral("Connected")).toBool())
        m_wrongRole.remove(path);
    if (changed.value(QStringLiteral("Connected")).toBool())
        QTimer::singleShot(roleCheckMs(), this, [this, path] { checkRole(path); });
}

void SystemBluezModel::applyHeadsets()
{
    QString connectedName;
    QString connectedPath;
    QVariantList paired;
    QVariantList found;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap device = it->value(device1Interface());
        if (device.isEmpty())
            continue;
        const QString path = it.key();
        const bool connected = device.value(QStringLiteral("Connected")).toBool();
        if (device.value(QStringLiteral("Paired")).toBool()) {
            if (!isHeadset(device.value(QStringLiteral("UUIDs"))))
                continue;
            const QString transport = childWithInterface(path, mediaTransport1Interface());
            const bool active = connected && !transport.isEmpty()
                && m_objects.value(transport).value(mediaTransport1Interface()).value(QStringLiteral("State")).toString()
                       == QLatin1StringView("active");
            QVariantMap row;
            row.insert(QStringLiteral("path"), path);
            row.insert(QStringLiteral("name"), aliasOf(device));
            row.insert(QStringLiteral("connected"), connected);
            row.insert(QStringLiteral("state"), active ? QStringLiteral("audio active")
                                                       : connected ? QStringLiteral("connected")
                                                                   : QStringLiteral("disconnected"));
            paired.append(row);
            if (connected && connectedPath.isEmpty()) {
                connectedPath = path;
                connectedName = aliasOf(device);
            }
            continue;
        }
        if (!isAudioSink(device) || hasA2dpSource(device.value(QStringLiteral("UUIDs"))))
            continue;
        QVariantMap row;
        row.insert(QStringLiteral("path"), path);
        row.insert(QStringLiteral("name"), aliasOf(device));
        row.insert(QStringLiteral("connected"), false);
        row.insert(QStringLiteral("state"), m_pairState.value(path, QStringLiteral("not paired")));
        found.append(row);
    }
    updateHeadsets(sortedByName(paired));
    updateDiscovered(sortedByName(found));
    updateHeadsetName(connectedName);
    if (!connectedPath.isEmpty() && activeRole() == QLatin1StringView("headphones"))
        updateLastHeadset(connectedPath);
}

void SystemBluezModel::checkHeadset(const QString &path)
{
    const QVariantMap device = m_objects.value(path).value(device1Interface());
    if (activeRole() != QLatin1StringView("headphones") || path == m_connectTarget
        || !device.value(QStringLiteral("Connected")).toBool() || !isHeadset(device.value(QStringLiteral("UUIDs"))))
        return;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap other = it->value(device1Interface());
        if (it.key() != path && other.value(QStringLiteral("Connected")).toBool()
            && isHeadset(other.value(QStringLiteral("UUIDs")))) {
            qInfo() << "microdap bluez:" << path << "connected beside" << it.key();
            disconnectDevice(path);
            return;
        }
    }
}

void SystemBluezModel::checkRole(const QString &path)
{
    const QVariantMap device = m_objects.value(path).value(device1Interface());
    if (m_wrongRole.contains(path) || !device.value(QStringLiteral("Paired")).toBool()
        || !device.value(QStringLiteral("Connected")).toBool())
        return;
    const QVariant uuids = device.value(QStringLiteral("UUIDs"));
    const QString name = device.value(QStringLiteral("Alias"), device.value(QStringLiteral("Name"))).toString();
    if (!wrongRoleCheck(name, hasA2dpSource(uuids), hasUuid(uuids, QLatin1StringView("0000110b")),
                        path == lastHeadset()))
        return;
    m_wrongRole.insert(path);
    disconnectDevice(path);
}

void SystemBluezModel::disconnectDevice(const QString &path)
{
    if (!m_bluez.isConnected())
        return;
    qInfo() << "microdap bluez: disconnecting" << path;
    m_bluez.asyncCall(QDBusMessage::createMethodCall(bluezService(), path, device1Interface(),
                                                     QStringLiteral("Disconnect")));
}

int SystemBluezModel::roleDevices(const QString &role) const
{
    int count = 0;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap device = it->value(device1Interface());
        if (device.value(QStringLiteral("Connected")).toBool() && inRole(device, role))
            ++count;
    }
    return count;
}

void SystemBluezModel::dropRole(const QString &role)
{
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap device = it->value(device1Interface());
        if (device.value(QStringLiteral("Connected")).toBool() && inRole(device, role))
            disconnectDevice(it.key());
    }
}

void SystemBluezModel::applyRole(const QString &role)
{
    if (role != QLatin1StringView("headphones"))
        m_pairAfterSearch.clear();
    callAgent(QStringLiteral("SetRole"), {role});
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
        if (isHeadset(device.value(QStringLiteral("UUIDs"))))
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
    forgetVolume();
    m_activeSince.invalidate();
    m_gateTimer.stop();
    m_nudgeTimer.stop();
    m_nudge = Nudge::Idle;
    m_nudgeDevice.clear();
    m_handed.clear();
    m_artSession.clear();
    m_imgHandle.clear();
    m_art.setTarget({}, 0, {});
    updateArtUrl({});
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

    applyHeadsets();
    roleDevicesChanged();

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

    const bool wasActive = transportActive();
    const QString state = transport.value(QStringLiteral("State")).toString();
    if (transportPath.isEmpty())
        updateTransport(false, QStringLiteral("none"));
    else if (state == QLatin1StringView("active") || state == QLatin1StringView("pending"))
        updateTransport(state == QLatin1StringView("active"), state);
    else
        updateTransport(false, QStringLiteral("idle"));
    if (!transportActive())
        m_activeSince.invalidate();
    else if (!wasActive || !m_activeSince.isValid())
        m_activeSince.start();
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
    applyArt(device.value(QStringLiteral("Address")).toString(),
             player.value(QStringLiteral("ObexPort")).toInt(),
             track.value(QStringLiteral("ImgHandle")).toString());
}

void SystemBluezModel::applyArt(const QString &address, int psm, const QString &handle)
{
    if (m_art.sessionPath() != m_artSession) {
        m_artSession = m_art.sessionPath();
        m_handed.clear();
    }
    const bool playing = status() == QLatin1StringView("playing");
    const qint64 ran = m_activeSince.isValid() ? m_activeSince.elapsed() : -1;
    if (handle.isEmpty() || !playing || ran >= artGateMs) {
        m_handed = handle;
        m_gateTimer.stop();
    } else if (ran >= 0 && !m_gateTimer.isActive()) {
        m_gateTimer.start(static_cast<int>(artGateMs - ran));
    }
    m_imgHandle = handle;
    m_art.setTarget(address, psm, m_handed);
    updateArt();
    nudge(handle);
}

void SystemBluezModel::updateArt()
{
    const QString path = m_art.artPath(m_imgHandle);
    updateArtUrl(path.isEmpty() ? QString() : QUrl::fromLocalFile(path).toString());
}

void SystemBluezModel::nudge(const QString &handle)
{
    if (m_devicePath != m_nudgeDevice) {
        m_nudgeDevice = m_devicePath;
        m_nudge = Nudge::Idle;
        m_nudgeTimer.stop();
    }
    if (m_nudge == Nudge::Idle) {
        if (!m_art.sessionOpen())
            return;
        if (!handle.isEmpty() || title().isEmpty() || status() != QLatin1StringView("paused")) {
            m_nudge = Nudge::Done;
            return;
        }
        m_nudge = Nudge::Next;
        m_nudgeTitle = title();
        m_nudgeNumber = trackNumber();
        m_nudgeTimer.start(nudgeStepMs);
        next();
        return;
    }
    if (m_nudge == Nudge::Next) {
        const bool moved = title() != m_nudgeTitle || trackNumber() != m_nudgeNumber
                           || (m_nudgeNumber == 0 && !handle.isEmpty());
        if (!moved)
            return;
        m_nudge = Nudge::Previous;
        m_nudgeTimer.start(nudgeStepMs);
        previous();
        return;
    }
    if (m_nudge == Nudge::Previous) {
        if (title() != m_nudgeTitle || trackNumber() != m_nudgeNumber)
            return;
        m_nudge = Nudge::Watch;
        m_nudgeTimer.start(nudgeWatchMs);
    }
    if (m_nudge == Nudge::Watch && status() == QLatin1StringView("playing") && title() == m_nudgeTitle
        && trackNumber() == m_nudgeNumber) {
        m_nudge = Nudge::Done;
        m_nudgeTimer.stop();
        pause();
    }
}

void SystemBluezModel::nudgeTimeout()
{
    if (m_nudge == Nudge::Next || m_nudge == Nudge::Previous) {
        qInfo() << "microdap bluez: first track art nudge got no answer";
        if (status() == QLatin1StringView("playing"))
            pause();
    }
    if (m_nudge != Nudge::Idle)
        m_nudge = Nudge::Done;
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
    if (holdWindow(false, timeoutSeconds))
        return;
    const uint seconds = static_cast<uint>(qMax(1, timeoutSeconds));
    callAgent(QStringLiteral("StartPairing"), {QVariant::fromValue(seconds)});
}

void SystemBluezModel::stopPairing()
{
    dropHeld(false);
    callAgent(QStringLiteral("StopPairing"));
}

void SystemBluezModel::startSearch(int timeoutSeconds)
{
    if (holdWindow(true, timeoutSeconds))
        return;
    for (auto it = m_pairState.begin(); it != m_pairState.end();) {
        if (it.value() == QLatin1StringView("pair failed"))
            it = m_pairState.erase(it);
        else
            ++it;
    }
    applyHeadsets();
    const uint seconds = static_cast<uint>(qMax(1, timeoutSeconds));
    callAgent(QStringLiteral("StartSearch"), {QVariant::fromValue(seconds)});
}

void SystemBluezModel::stopSearch()
{
    dropHeld(true);
    m_pairAfterSearch.clear();
    callAgent(QStringLiteral("StopSearch"));
}

void SystemBluezModel::startCompanionPairing(int timeoutSeconds)
{
    const uint seconds = static_cast<uint>(qMax(1, timeoutSeconds));
    callAgent(QStringLiteral("StartCompanionPairing"), {QVariant::fromValue(seconds)});
}

void SystemBluezModel::answerConfirm(int passkey, bool yes)
{
    callAgent(QStringLiteral("AnswerConfirm"), {QVariant::fromValue(uint(passkey)), yes});
}

void SystemBluezModel::onCompanionPaired(const QDBusObjectPath &device)
{
    emit companionPaired(device.path());
}

void SystemBluezModel::setShuffle(const QString &shuffle)
{
    setPlayerProperty(QStringLiteral("Shuffle"), shuffle);
}

void SystemBluezModel::setRepeat(const QString &repeat)
{
    setPlayerProperty(QStringLiteral("Repeat"), repeat);
}

void SystemBluezModel::connectHeadset(const QString &path)
{
    if (holdDevice(false, path) || !m_bluez.isConnected())
        return;
    m_connectTarget = path;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        const QVariantMap device = it->value(device1Interface());
        if (it.key() != path && device.value(QStringLiteral("Connected")).toBool()
            && isHeadset(device.value(QStringLiteral("UUIDs"))))
            disconnectDevice(it.key());
    }
    qInfo() << "microdap bluez: connecting" << path;
    auto *watcher = new QDBusPendingCallWatcher(
        m_bluez.asyncCall(QDBusMessage::createMethodCall(bluezService(), path, device1Interface(),
                                                         QStringLiteral("Connect"))),
        this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (m_connectTarget == path)
            m_connectTarget.clear();
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "microdap bluez: connect" << path << "failed" << reply.errorName();
            return;
        }
        callEngine(QStringLiteral("SetTarget"), {QVariant::fromValue(QDBusObjectPath(path))});
    });
}

void SystemBluezModel::disconnectHeadset(const QString &path)
{
    if (path == m_connectTarget)
        m_connectTarget.clear();
    disconnectDevice(path);
}

void SystemBluezModel::forgetHeadset(const QString &path)
{
    if (!m_bluez.isConnected() || path.isEmpty())
        return;
    if (path == lastHeadset()) {
        updateLastHeadset({});
        callEngine(QStringLiteral("SetTarget"), {QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))});
    }
    if (path == m_connectTarget)
        m_connectTarget.clear();
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), path.section(QLatin1Char('/'), 0, -2),
                                                       QStringLiteral("org.bluez.Adapter1"),
                                                       QStringLiteral("RemoveDevice"));
    call << QVariant::fromValue(QDBusObjectPath(path));
    qInfo() << "microdap bluez: forgetting" << path;
    m_bluez.asyncCall(call);
}

void SystemBluezModel::pairDevice(const QString &path)
{
    if (holdDevice(true, path) || m_pairState.value(path) == QLatin1StringView("pairing") || m_pairAfterSearch == path)
        return;
    if (!searching()) {
        m_pairAfterSearch = path;
        startSearch(defaultPairingTimeout);
        return;
    }
    startPair(path);
}

void SystemBluezModel::startPair(const QString &path)
{
    if (!m_bluez.isConnected() || !m_objects.contains(path) || activeRole() != QLatin1StringView("headphones"))
        return;
    m_pairState.insert(path, QStringLiteral("pairing"));
    applyHeadsets();
    qInfo() << "microdap bluez: pairing" << path;
    QDBusMessage call = QDBusMessage::createMethodCall(bluezService(), path, device1Interface(), QStringLiteral("Pair"));
    auto *watcher = new QDBusPendingCallWatcher(m_bluez.asyncCall(call, pairTimeoutMs), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, path](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "microdap bluez: pair" << path << "failed" << reply.errorName();
            m_pairState.insert(path, QStringLiteral("pair failed"));
            applyHeadsets();
            return;
        }
        m_pairState.remove(path);
        QDBusMessage trust = QDBusMessage::createMethodCall(bluezService(), path, propertiesInterface(),
                                                            QStringLiteral("Set"));
        trust << device1Interface() << QStringLiteral("Trusted") << QVariant::fromValue(QDBusVariant(true));
        m_bluez.asyncCall(trust);
        applyHeadsets();
        if (!switching() && activeRole() == QLatin1StringView("headphones"))
            connectHeadset(path);
    });
}

}
