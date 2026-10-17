#include "Intercept.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>
#include <QTimer>

#include <utility>

namespace microdap {

namespace {

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString objectManagerInterface()
{
    return QStringLiteral("org.freedesktop.DBus.ObjectManager");
}

QString holderProperty()
{
    return QStringLiteral("Holder");
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

}

Intercept::Intercept(const QDBusConnection &session, const QDBusConnection &inputPlumber, QObject *parent)
    : QObject(parent)
    , m_session(session)
    , m_bus(inputPlumber)
    , m_watcher(service(), inputPlumber, QDBusServiceWatcher::WatchForOwnerChange)
    , m_arbiter(arbiterService(), session, QDBusServiceWatcher::WatchForOwnerChange)
{
    registerMetaTypes();
    m_retry.setSingleShot(true);
    m_retry.setInterval(ReleaseRetryMs);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (!m_wanted)
            release();
    });
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &Intercept::refresh);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this,
            &Intercept::onServiceUnregistered);
    connect(&m_arbiter, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
        if (owner.isEmpty()) {
            m_retry.stop();
            setHolder({});
            emit arbiterLost();
            return;
        }
        if (m_wanted)
            acquire();
        else
            readHolder();
    });

    if (!m_session.isConnected())
        qWarning() << "microdap intercept: session bus not connected";
    else if (!m_session.connect(arbiterService(), arbiterPath(), propertiesInterface(),
                                QStringLiteral("PropertiesChanged"), this,
                                SLOT(onArbiterProperties(QString,QVariantMap,QStringList))))
        qWarning() << "microdap intercept: cannot subscribe to the arbiter" << m_session.lastError().message();

    if (!m_bus.isConnected()) {
        qWarning() << "microdap intercept: bus not connected";
        return;
    }
    if (!m_bus.connect(service(), rootPath(), objectManagerInterface(), QStringLiteral("InterfacesAdded"),
                       this, SLOT(onInterfacesAdded(QDBusMessage)))) {
        qWarning() << "microdap intercept: cannot subscribe to InterfacesAdded"
                   << m_bus.lastError().message();
    }
    if (!m_bus.connect(service(), rootPath(), objectManagerInterface(), QStringLiteral("InterfacesRemoved"),
                       this, SLOT(onInterfacesRemoved(QDBusMessage)))) {
        qWarning() << "microdap intercept: cannot subscribe to InterfacesRemoved"
                   << m_bus.lastError().message();
    }
    refresh();
}

QDBusConnection Intercept::bus()
{
    const QString address = qEnvironmentVariable("MICRODAP_INPUTPLUMBER_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("microdap-inputplumber"));
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

QString Intercept::arbiterService()
{
    return QStringLiteral("net.seilent.MicroOs.Intercept");
}

QString Intercept::arbiterPath()
{
    return QStringLiteral("/net/seilent/MicroOs/Intercept");
}

QString Intercept::arbiterInterface()
{
    return QStringLiteral("net.seilent.MicroOs.Intercept1");
}

bool Intercept::wanted() const
{
    return m_wanted;
}

void Intercept::setWanted(bool wanted)
{
    if (m_wanted == wanted)
        return;
    const bool before = holding();
    m_wanted = wanted;
    m_retry.stop();
    if (wanted)
        acquire();
    else
        release();
    if (before != holding())
        emit holdingChanged();
}

bool Intercept::holding() const
{
    return m_wanted && !m_holder.isEmpty() && m_holder == m_session.baseService();
}

QString Intercept::holder() const
{
    return m_holder;
}

QString Intercept::name() const
{
    return m_session.baseService();
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

void Intercept::acquire()
{
    if (!m_session.isConnected())
        return;
    const quint64 request = ++m_request;
    const QDBusMessage call = QDBusMessage::createMethodCall(arbiterService(), arbiterPath(), arbiterInterface(),
                                                             QStringLiteral("Acquire"));
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, request](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qInfo() << "microdap intercept: Acquire failed" << reply.errorMessage();
            if (request == m_request)
                emit acquired(false);
            return;
        }
        if (request == m_request)
            readHolder(true);
    });
}

void Intercept::release()
{
    if (!m_session.isConnected())
        return;
    ++m_request;
    const QDBusMessage call = QDBusMessage::createMethodCall(arbiterService(), arbiterPath(), arbiterInterface(),
                                                             QStringLiteral("Release"));
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusMessage reply = finished->reply();
        if (reply.type() != QDBusMessage::ErrorMessage)
            return;
        qInfo() << "microdap intercept: Release failed" << reply.errorMessage();
        if (!m_wanted && m_holder == name())
            m_retry.start();
    });
}

void Intercept::readHolder(bool afterAcquire)
{
    QDBusMessage call = QDBusMessage::createMethodCall(arbiterService(), arbiterPath(), propertiesInterface(),
                                                       QStringLiteral("Get"));
    call << arbiterInterface() << holderProperty();
    const quint64 request = m_request;
    auto *watcher = new QDBusPendingCallWatcher(m_session.asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, request, afterAcquire](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (request != m_request)
            return;
        const QDBusMessage reply = finished->reply();
        setHolder(reply.type() == QDBusMessage::ErrorMessage ? QString()
                                                              : unwrap(reply.arguments().value(0)).toString());
        if (afterAcquire)
            emit acquired(true);
    });
}

void Intercept::setHolder(const QString &holder)
{
    if (m_holder == holder)
        return;
    const bool before = holding();
    m_holder = holder;
    if (m_wanted || m_holder != name())
        m_retry.stop();
    else if (!m_retry.isActive())
        m_retry.start();
    if (before != holding())
        emit holdingChanged();
    emit holderChanged();
}

void Intercept::onArbiterProperties(const QString &interface, const QVariantMap &changed,
                                    const QStringList &invalidated)
{
    if (interface != arbiterInterface())
        return;
    if (changed.contains(holderProperty()))
        setHolder(unwrap(changed.value(holderProperty())).toString());
    else if (invalidated.contains(holderProperty()))
        readHolder();
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
            qInfo() << "microdap intercept: GetManagedObjects failed" << reply.errorMessage();
        } else {
            const ManagedObjectList objects = qdbus_cast<ManagedObjectList>(reply.arguments().value(0));
            bool devices = false;
            bool targets = false;
            for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                track(it.key().path(), it.value(), devices, targets);
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

void Intercept::rumble(const QList<int> &pattern)
{
    int at = 0;
    for (qsizetype i = 0; i < pattern.size(); ++i) {
        const int ms = pattern.at(i);
        if (ms <= 0)
            return;
        if (i % 2 == 0) {
            QTimer::singleShot(at, this, [this] { sendRumble(RumbleLevel); });
            QTimer::singleShot(at + ms, this, [this] { sendRumble(0.0); });
        }
        at += ms;
    }
}

void Intercept::sendRumble(double level)
{
    if (!m_bus.isConnected())
        return;
    for (const QString &path : devices()) {
        QDBusMessage call = QDBusMessage::createMethodCall(
            service(), path, QStringLiteral("org.shadowblip.Output.ForceFeedback"), QStringLiteral("Rumble"));
        call << level;
        call.setAutoStartService(false);
        m_bus.asyncCall(call);
    }
}

}
