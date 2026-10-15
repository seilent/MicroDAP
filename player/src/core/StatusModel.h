#pragma once

#include <QDBusConnection>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTime>
#include <QTimer>
#include <QVariantMap>

class QDBusInterface;

namespace microdap {

class StatusModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int battery READ battery NOTIFY batteryChanged)
    Q_PROPERTY(bool charging READ charging NOTIFY chargingChanged)
    Q_PROPERTY(bool batteryPresent READ batteryPresent NOTIFY batteryChanged)
    Q_PROPERTY(QString clock READ clock NOTIFY clockChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    explicit StatusModel(QObject *parent = nullptr);
    StatusModel(const QDBusConnection &systemBus, const QString &powerSupplyRoot,
                QObject *parent = nullptr);

    static QString defaultPowerSupplyRoot();
    static QString formatClock(const QTime &time);

    bool usesUPower() const;
    int battery() const;
    bool charging() const;
    bool batteryPresent() const;
    QString clock() const;
    bool active() const;
    void setActive(bool active);

    Q_INVOKABLE void refresh();

signals:
    void batteryChanged();
    void chargingChanged();
    void clockChanged();
    void activeChanged();

private slots:
    void onUPowerChanged(const QString &interface, const QVariantMap &changed,
                         const QStringList &invalidated);
    void tick();

private:
    void readUPower();
    void readSysfs();
    void apply(int percent, bool charging, bool present);
    void updateClock();

    QDBusConnection m_bus;
    QString m_root;
    QDBusInterface *m_upower = nullptr;
    QTimer m_timer;
    int m_battery = -1;
    bool m_charging = false;
    bool m_present = false;
    QString m_clock;
    bool m_active = true;
};

}
