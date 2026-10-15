#include "SensorRotation.h"

#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>

namespace microdap {

namespace {

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

QString hasProperty()
{
    return QStringLiteral("HasAccelerometer");
}

QString orientationProperty()
{
    return QStringLiteral("AccelerometerOrientation");
}

QVariant replyValue(const QDBusMessage &reply)
{
    const QVariant value = reply.arguments().value(0);
    if (value.metaType() == QMetaType::fromType<QDBusVariant>())
        return qvariant_cast<QDBusVariant>(value).variant();
    return value;
}

}

SensorRotation::SensorRotation(QObject *parent)
    : SensorRotation(bus(), parent)
{
}

SensorRotation::SensorRotation(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
    , m_watcher(service(), bus, QDBusServiceWatcher::WatchForOwnerChange)
    , m_sensorTurn(defaultSensorTurn())
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(settleMs);
    connect(&m_settle, &QTimer::timeout, this, &SensorRotation::settle);
    m_reclaim.setSingleShot(true);
    m_reclaim.setInterval(defaultReclaimMs());
    connect(&m_reclaim, &QTimer::timeout, this, &SensorRotation::claim);
    connect(&m_watcher, &QDBusServiceWatcher::serviceRegistered, this, &SensorRotation::onServiceRegistered);
    connect(&m_watcher, &QDBusServiceWatcher::serviceUnregistered, this, &SensorRotation::onServiceUnregistered);
    connect(&m_watcher, &QDBusServiceWatcher::serviceOwnerChanged, this, &SensorRotation::onServiceOwnerChanged);

    if (!m_bus.isConnected()) {
        qWarning() << "microdap sensor: bus not connected";
        return;
    }
    if (!m_bus.connect(service(), path(), propertiesInterface(), QStringLiteral("PropertiesChanged"), this,
                       SLOT(onPropertiesChanged(QString,QVariantMap,QStringList)))) {
        qWarning() << "microdap sensor: cannot subscribe to PropertiesChanged" << m_bus.lastError().message();
    }
    readHas();
}

QDBusConnection SensorRotation::bus()
{
    const QString address = qEnvironmentVariable("MICRODAP_SENSOR_BUS");
    if (address.isEmpty())
        return QDBusConnection::systemBus();
    if (address == QLatin1StringView("session"))
        return QDBusConnection::sessionBus();
    return QDBusConnection::connectToBus(address, QStringLiteral("microdap-sensor"));
}

QString SensorRotation::service()
{
    return QStringLiteral("net.hadess.SensorProxy");
}

QString SensorRotation::path()
{
    return QStringLiteral("/net/hadess/SensorProxy");
}

QString SensorRotation::interface()
{
    return QStringLiteral("net.hadess.SensorProxy");
}

int SensorRotation::defaultSensorTurn()
{
    return qEnvironmentVariableIntValue("MICRODAP_SENSOR_TURN") == 3 ? 3 : 1;
}

int SensorRotation::defaultReclaimMs()
{
    bool ok = false;
    const int ms = qEnvironmentVariableIntValue("MICRODAP_SENSOR_RECLAIM_MS", &ok);
    return ok && ms > 0 ? ms : reclaimMs;
}

int SensorRotation::turnOf(const QString &raw, int sensorTurn)
{
    if (raw == QLatin1StringView("normal"))
        return 0;
    if (raw == QLatin1StringView("bottom-up"))
        return 2;
    if (raw == QLatin1StringView("left-up"))
        return sensorTurn;
    if (raw == QLatin1StringView("right-up"))
        return (sensorTurn + 2) % 4;
    return -1;
}

bool SensorRotation::supported() const
{
    return m_supported;
}

int SensorRotation::turn() const
{
    return m_turn;
}

QString SensorRotation::raw() const
{
    return m_raw;
}

QString SensorRotation::settledRaw() const
{
    return m_settledRaw;
}

bool SensorRotation::wanted() const
{
    return m_wanted;
}

bool SensorRotation::claimed() const
{
    return m_claimed;
}

int SensorRotation::sensorTurn() const
{
    return m_sensorTurn;
}

void SensorRotation::setWanted(bool wanted, bool keepTurn)
{
    if (!wanted && !keepTurn) {
        m_last = -1;
        setSettledRaw(QString());
    }
    if (m_wanted == wanted)
        return;
    m_wanted = wanted;
    if (wanted) {
        maybeClaim();
        return;
    }
    m_settle.stop();
    if (m_holding) {
        m_reclaim.stop();
        m_holding = false;
        updateSupported();
    }
    if (m_claimed)
        release();
    m_claimed = false;
    setTurn(-1);
}

QDBusMessage SensorRotation::call(const QString &method) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(service(), path(), interface(), method);
    message.setAutoStartService(false);
    return message;
}

QDBusMessage SensorRotation::get(const QString &property) const
{
    QDBusMessage message = QDBusMessage::createMethodCall(service(), path(), propertiesInterface(),
                                                          QStringLiteral("Get"));
    message.setAutoStartService(false);
    message << interface() << property;
    return message;
}

void SensorRotation::readHas()
{
    if (!m_bus.isConnected())
        return;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get(hasProperty())), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation)
            return;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (m_holding)
                lose();
            return;
        }
        m_present = true;
        m_holding = false;
        m_reclaim.stop();
        setHas(replyValue(reply).toBool());
    });
}

void SensorRotation::readOrientation()
{
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(get(orientationProperty())), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation || !m_claimed)
            return;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            qWarning() << "microdap sensor: orientation read failed" << reply.errorMessage();
            return;
        }
        take(replyValue(reply).toString());
    });
}

void SensorRotation::maybeClaim()
{
    if (m_wanted && m_present && m_has && !m_claimed && !m_claiming)
        claim();
}

void SensorRotation::claim()
{
    if (!m_bus.isConnected() || !m_wanted || m_claimed || m_claiming)
        return;
    m_claiming = true;
    const quint64 generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(call(QStringLiteral("ClaimAccelerometer"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, generation](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (generation != m_generation)
            return;
        m_claiming = false;
        const QDBusMessage reply = finished->reply();
        if (reply.type() == QDBusMessage::ErrorMessage) {
            if (m_holding)
                lose();
            else
                qWarning() << "microdap sensor: claim failed" << reply.errorMessage();
            return;
        }
        if (!m_wanted) {
            release();
            return;
        }
        m_claimed = true;
        setTurn(m_last);
        readHas();
        readOrientation();
    });
}

void SensorRotation::release()
{
    if (!m_bus.isConnected())
        return;
    m_bus.asyncCall(call(QStringLiteral("ReleaseAccelerometer")));
}

void SensorRotation::lose()
{
    m_reclaim.stop();
    m_holding = false;
    m_settle.stop();
    m_last = -1;
    setSettledRaw(QString());
    setTurn(-1);
    updateSupported();
}

void SensorRotation::take(const QString &raw)
{
    setRaw(raw);
    const int turn = turnOf(raw, m_sensorTurn);
    if (turn < 0 || turn == m_turn) {
        m_settle.stop();
        return;
    }
    if (turn == m_pending && m_settle.isActive())
        return;
    m_pending = turn;
    m_pendingRaw = raw;
    m_settle.start();
}

void SensorRotation::settle()
{
    if (!m_claimed)
        return;
    m_last = m_pending;
    setSettledRaw(m_pendingRaw);
    setTurn(m_pending);
}

void SensorRotation::setHas(bool has)
{
    m_has = has;
    updateSupported();
    maybeClaim();
}

void SensorRotation::updateSupported()
{
    const bool supported = (m_present && m_has) || m_holding;
    if (m_supported == supported)
        return;
    m_supported = supported;
    emit supportedChanged();
}

void SensorRotation::setTurn(int turn)
{
    if (m_turn == turn)
        return;
    m_turn = turn;
    emit turnChanged();
}

void SensorRotation::setRaw(const QString &raw)
{
    if (m_raw == raw)
        return;
    m_raw = raw;
    emit rawChanged();
}

void SensorRotation::setSettledRaw(const QString &raw)
{
    if (m_settledRaw == raw)
        return;
    m_settledRaw = raw;
    emit settledRawChanged();
}

void SensorRotation::onPropertiesChanged(const QString &name, const QVariantMap &changed,
                                         const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (name != interface())
        return;
    const auto has = changed.constFind(hasProperty());
    if (has != changed.constEnd()) {
        m_present = true;
        m_holding = false;
        m_reclaim.stop();
        setHas(has->toBool());
    }
    const auto orientation = changed.constFind(orientationProperty());
    if (orientation != changed.constEnd() && m_claimed)
        take(orientation->toString());
}

void SensorRotation::onServiceRegistered()
{
    readHas();
}

void SensorRotation::onServiceUnregistered()
{
    const bool held = m_supported || m_claimed;
    ++m_generation;
    m_present = false;
    m_claiming = false;
    m_claimed = false;
    m_settle.stop();
    if (m_wanted && held) {
        m_holding = true;
        m_reclaim.start();
    }
    updateSupported();
}

void SensorRotation::onServiceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner)
{
    Q_UNUSED(name)
    if (oldOwner.isEmpty() || newOwner.isEmpty())
        return;
    onServiceUnregistered();
    onServiceRegistered();
}

}
