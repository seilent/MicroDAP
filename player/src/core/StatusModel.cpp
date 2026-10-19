#include "StatusModel.h"

#include <QDBusInterface>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>

namespace microdap {

namespace {

constexpr int tickMs = 1000;
constexpr uint stateCharging = 1;
constexpr uint stateFullyCharged = 4;
constexpr uint statePendingCharge = 5;

QString upowerService()
{
    return QStringLiteral("org.freedesktop.UPower");
}

QString upowerDevicePath()
{
    return QStringLiteral("/org/freedesktop/UPower/devices/DisplayDevice");
}

QString upowerDeviceInterface()
{
    return QStringLiteral("org.freedesktop.UPower.Device");
}

QString readLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

bool chargingState(uint state)
{
    return state == stateCharging || state == stateFullyCharged || state == statePendingCharge;
}

}

StatusModel::StatusModel(QObject *parent)
    : StatusModel(QDBusConnection::systemBus(), defaultPowerSupplyRoot(), parent)
{
}

StatusModel::StatusModel(const QDBusConnection &systemBus, const QString &powerSupplyRoot,
                         QObject *parent)
    : QObject(parent)
    , m_bus(systemBus)
    , m_root(powerSupplyRoot)
{
    m_timer.setInterval(tickMs);
    connect(&m_timer, &QTimer::timeout, this, &StatusModel::tick);

    if (m_bus.isConnected()) {
        auto *upower = new QDBusInterface(upowerService(), upowerDevicePath(),
                                          upowerDeviceInterface(), m_bus, this);
        if (upower->isValid()) {
            m_upower = upower;
            if (!m_bus.connect(upowerService(), upowerDevicePath(),
                               QStringLiteral("org.freedesktop.DBus.Properties"),
                               QStringLiteral("PropertiesChanged"), this,
                               SLOT(onUPowerChanged(QString,QVariantMap,QStringList)))) {
                qWarning() << "microdap status: cannot subscribe to UPower"
                           << m_bus.lastError().message();
            }
        } else {
            qInfo() << "microdap status: no UPower DisplayDevice, reading" << m_root;
            delete upower;
        }
    }

    refresh();
    updateClock();
    m_timer.start();
}

QString StatusModel::defaultPowerSupplyRoot()
{
    return qEnvironmentVariable("MICRODAP_POWER_SUPPLY_ROOT",
                                QStringLiteral("/sys/class/power_supply"));
}

QString StatusModel::formatClock(const QTime &time, const QString &mode)
{
    if (mode == QLatin1StringView("off"))
        return {};
    if (mode == QLatin1StringView("12h")) {
        const int hour = time.hour() % 12;
        return QStringLiteral("%1:%2").arg(hour == 0 ? 12 : hour).arg(time.minute(), 2, 10, QLatin1Char('0'));
    }
    return time.toString(QStringLiteral("h:mm"));
}

bool StatusModel::usesUPower() const
{
    return m_upower != nullptr;
}

int StatusModel::battery() const
{
    return m_battery;
}

bool StatusModel::charging() const
{
    return m_charging;
}

bool StatusModel::batteryPresent() const
{
    return m_present;
}

QString StatusModel::clock() const
{
    return m_clock;
}

bool StatusModel::active() const
{
    return m_active;
}

void StatusModel::setActive(bool active)
{
    if (m_active == active)
        return;
    m_active = active;
    if (m_active) {
        refresh();
        updateClock();
        m_timer.start();
    } else {
        m_timer.stop();
    }
    emit activeChanged();
}

void StatusModel::refresh()
{
    if (m_upower)
        readUPower();
    else
        readSysfs();
}

void StatusModel::readUPower()
{
    const double percent = m_upower->property("Percentage").toDouble();
    const uint state = m_upower->property("State").toUInt();
    const bool present = m_upower->property("IsPresent").toBool();
    apply(qRound(percent), chargingState(state), present);
}

void StatusModel::readSysfs()
{
    const QDir root(m_root);
    const QFileInfoList supplies = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : supplies) {
        const QString base = info.filePath() + QLatin1Char('/');
        if (readLine(base + QStringLiteral("type")) != QLatin1StringView("Battery"))
            continue;
        bool ok = false;
        const int capacity = readLine(base + QStringLiteral("capacity")).toInt(&ok);
        if (!ok)
            continue;
        const QString status = readLine(base + QStringLiteral("status"));
        apply(capacity,
              status == QLatin1StringView("Charging") || status == QLatin1StringView("Full"),
              true);
        return;
    }
    apply(0, false, false);
}

void StatusModel::apply(int percent, bool charging, bool present)
{
    const int battery = present ? qBound(0, percent, 100) : -1;
    if (battery != m_battery || present != m_present) {
        m_battery = battery;
        m_present = present;
        emit batteryChanged();
    }
    if (charging != m_charging) {
        m_charging = charging;
        emit chargingChanged();
    }
}

QString StatusModel::clockMode() const
{
    return m_clockMode;
}

void StatusModel::setClockMode(const QString &mode)
{
    if (mode == m_clockMode)
        return;
    m_clockMode = mode;
    emit clockModeChanged();
    updateClock();
}

bool StatusModel::batteryPercent() const
{
    return m_batteryPercent;
}

void StatusModel::setBatteryPercent(bool on)
{
    if (on == m_batteryPercent)
        return;
    m_batteryPercent = on;
    emit batteryPercentChanged();
}

void StatusModel::updateClock()
{
    const QString now = formatClock(QTime::currentTime(), m_clockMode);
    if (now == m_clock)
        return;
    m_clock = now;
    emit clockChanged();
}

void StatusModel::onUPowerChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated)
{
    if (interface != upowerDeviceInterface())
        return;
    if (!invalidated.isEmpty()) {
        readUPower();
        return;
    }
    int percent = m_battery < 0 ? 0 : m_battery;
    bool charging = m_charging;
    bool present = m_present;
    if (changed.contains(QStringLiteral("Percentage")))
        percent = qRound(changed.value(QStringLiteral("Percentage")).toDouble());
    if (changed.contains(QStringLiteral("State")))
        charging = chargingState(changed.value(QStringLiteral("State")).toUInt());
    if (changed.contains(QStringLiteral("IsPresent")))
        present = changed.value(QStringLiteral("IsPresent")).toBool();
    apply(percent, charging, present);
}

void StatusModel::tick()
{
    updateClock();
    if (!m_upower)
        readSysfs();
}

}
