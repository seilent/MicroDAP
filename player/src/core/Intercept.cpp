#include "Intercept.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QTimer>
#include <QtGlobal>

#include <utility>

namespace casdap {

namespace {

constexpr int exitWriteTimeoutMs = 1000;
constexpr int releaseRetryMs = 1000;

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString interceptModeProperty()
{
    return QStringLiteral("InterceptMode");
}

QVariant unwrap(const QVariant &value)
{
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

QString objectPathValue(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    if (inner.metaType() == QMetaType::fromType<QDBusObjectPath>())
        return qvariant_cast<QDBusObjectPath>(inner).path();
    return inner.toString();
}

QStringList stringList(const QVariant &value)
{
    const QVariant inner = unwrap(value);
    if (inner.metaType() == QMetaType::fromType<QDBusArgument>())
        return qdbus_cast<QStringList>(qvariant_cast<QDBusArgument>(inner));
    return inner.toStringList();
}

QDBusMessage setModeCall(const QString &path, uint mode)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Intercept::service(), path,
                                                       propertiesInterface(), QStringLiteral("Set"));
    call << Intercept::compositeInterface() << interceptModeProperty()
         << QVariant::fromValue(QDBusVariant(QVariant::fromValue(mode)));
    return call;
}

}

Intercept::Intercept(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(service(), bus, QDBusServiceWatcher::WatchForOwnerChange)
{
    registerMetaTypes();
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &Intercept::refresh);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &Intercept::onServiceUnregistered);

    if (!m_bus.isConnected()) {
        qWarning() << "casdap intercept: bus not connected";
        return;
    }
    if (!m_bus.connect(service(), rootPath(), objectManagerInterface(), QStringLiteral("InterfacesAdded"),
                       this, SLOT(onInterfacesAdded(QDBusMessage)))) {
        qWarning() << "casdap intercept: cannot subscribe to InterfacesAdded"
                   << m_bus.lastError().message();
    }
    if (!m_bus.connect(service(), rootPath(), objectManagerInterface(), QStringLiteral("InterfacesRemoved"),
                       this, SLOT(onInterfacesRemoved(QDBusMessage)))) {
        qWarning() << "casdap intercept: cannot subscribe to InterfacesRemoved"
                   << m_bus.lastError().message();
    }
    refresh();
}

Intercept::~Intercept()
{
    if (!m_everShown || !m_bus.isConnected())
        return;
    for (const QString &path : std::as_const(m_devices)) {
        const QDBusMessage reply = m_bus.call(setModeCall(path, ModeNone), QDBus::Block, exitWriteTimeoutMs);
        if (reply.type() == QDBusMessage::ErrorMessage)
            qWarning() << "casdap intercept: release failed" << path << reply.errorMessage();
    }
}

QDBusConnection Intercept::bus()
{
    const QString address = qEnvironmentVariable("CASDAP_INPUTPLUMBER_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("casdap-inputplumber"));
}

void Intercept::registerMetaTypes()
{
    qDBusRegisterMetaType<InterfaceList>();
    qDBusRegisterMetaType<ManagedObjectList>();
}

QString Intercept::service()
{
    return QStringLiteral("org.shadowblip.InputPlumber");
}

QString Intercept::rootPath()
{
    return QStringLiteral("/org/shadowblip/InputPlumber");
}

QString Intercept::compositeInterface()
{
    return QStringLiteral("org.shadowblip.Input.CompositeDevice");
}

QString Intercept::dbusDeviceInterface()
{
    return QStringLiteral("org.shadowblip.Input.DBusDevice");
}

QString Intercept::targetPrefix()
{
    return rootPath() + QStringLiteral("/devices/target/dbus");
}

bool Intercept::shown() const
{
    return m_shown;
}

void Intercept::setShown(bool shown)
{
    if (m_shown == shown)
        return;
    m_shown = shown;
    if (shown)
        m_everShown = true;
    writeAll(shown ? ModeOverlay : ModeNone);
}

QStringList Intercept::devices() const
{
    QStringList list = m_devices.values();
    list.sort();
    return list;
}

QStringList Intercept::targets() const
{
    QStringList list = m_targets.values();
    list.sort();
    return list;
}

bool Intercept::hasTargets() const
{
    return !m_targets.isEmpty();
}

void Intercept::refresh()
{
    if (!m_bus.isConnected())
        return;
    const quint64 generation = ++m_generation;
    QDBusMessage call = QDBusMessage::createMethodCall(service(), rootPath(), objectManagerInterface(),
                                                       QStringLiteral("GetManagedObjects"));
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusMessage reply = finished->reply();
        const QSet<QString> previousDevices = m_devices;
        const QSet<QString> previousTargets = m_targets;
        m_devices.clear();
        m_targets.clear();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "casdap intercept: GetManagedObjects failed" << reply.errorMessage();
        } else {
            const ManagedObjectList objects = qdbus_cast<ManagedObjectList>(reply.arguments().value(0));
            bool devices = false;
            bool targets = false;
            for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                track(it.key().path(), it.value(), devices, targets);
        }
        if (m_shown) {
            for (const QString &path : std::as_const(m_devices)) {
                if (!previousDevices.contains(path))
                    write(path, ModeOverlay);
            }
        }
        if (previousDevices != m_devices)
            emit devicesChanged();
        if (previousTargets != m_targets)
            emit targetsChanged();
    });
}

void Intercept::track(const QString &path, const InterfaceList &interfaces, bool &devices, bool &targets)
{
    if (interfaces.contains(compositeInterface()) && !m_devices.contains(path)) {
        m_devices.insert(path);
        devices = true;
    }
    if (interfaces.contains(dbusDeviceInterface()) && path.startsWith(targetPrefix())
        && !m_targets.contains(path)) {
        m_targets.insert(path);
        targets = true;
    }
}

void Intercept::onInterfacesAdded(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2)
        return;
    const QString path = objectPathValue(arguments.at(0));
    if (path.isEmpty())
        return;
    const InterfaceList added = qdbus_cast<InterfaceList>(arguments.at(1));
    bool devices = false;
    bool targets = false;
    track(path, added, devices, targets);
    if (devices && m_shown)
        write(path, ModeOverlay);
    if (devices)
        emit devicesChanged();
    if (targets)
        emit targetsChanged();
}

void Intercept::onInterfacesRemoved(const QDBusMessage &message)
{
    const QList<QVariant> arguments = message.arguments();
    if (arguments.size() < 2)
        return;
    const QString path = objectPathValue(arguments.at(0));
    const QStringList removed = stringList(arguments.at(1));
    if (removed.contains(compositeInterface()) && m_devices.remove(path))
        emit devicesChanged();
    if (removed.contains(dbusDeviceInterface()) && m_targets.remove(path))
        emit targetsChanged();
}

void Intercept::onServiceUnregistered()
{
    ++m_generation;
    const bool devices = !m_devices.isEmpty();
    const bool targets = !m_targets.isEmpty();
    m_devices.clear();
    m_targets.clear();
    if (devices)
        emit devicesChanged();
    if (targets)
        emit targetsChanged();
}

void Intercept::writeAll(uint mode)
{
    for (const QString &path : std::as_const(m_devices))
        write(path, mode);
}

void Intercept::write(const QString &path, uint mode, bool retry)
{
    if (!m_bus.isConnected()) {
        qWarning() << "casdap intercept: bus not connected, skipping" << path << mode;
        return;
    }
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(setModeCall(path, mode)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, path, mode, retry](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() != QDBusMessage::ErrorMessage)
            return;
        if (mode == ModeNone && !retry) {
            qWarning() << "casdap intercept: intercept release failed" << path << reply.errorMessage();
            return;
        }
        qWarning() << "casdap intercept: intercept failed" << path << mode << reply.errorMessage();
        if (mode == ModeNone && !m_shown) {
            QTimer::singleShot(releaseRetryMs, this, [this, path] {
                if (!m_shown && m_devices.contains(path))
                    write(path, ModeNone, false);
            });
        }
    });
}

}
