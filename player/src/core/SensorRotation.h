#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusServiceWatcher>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace microdap {

class SensorRotation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported NOTIFY supportedChanged)
    Q_PROPERTY(int turn READ turn NOTIFY turnChanged)
    Q_PROPERTY(QString raw READ raw NOTIFY rawChanged)
    Q_PROPERTY(QString settledRaw READ settledRaw NOTIFY settledRawChanged)

public:
    static constexpr int settleMs = 500;
    static constexpr int reclaimMs = 1000;

    explicit SensorRotation(QObject *parent = nullptr);
    explicit SensorRotation(const QDBusConnection &bus, QObject *parent = nullptr);

    static QDBusConnection bus();
    static QString service();
    static QString path();
    static QString interface();
    static int defaultSensorTurn();
    static int defaultReclaimMs();
    static int turnOf(const QString &raw, int sensorTurn);

    bool supported() const;
    int turn() const;
    QString raw() const;
    QString settledRaw() const;
    bool wanted() const;
    bool claimed() const;
    int sensorTurn() const;

    Q_INVOKABLE void setWanted(bool wanted, bool keepTurn = false);

signals:
    void supportedChanged();
    void turnChanged();
    void rawChanged();
    void settledRawChanged();

private slots:
    void onPropertiesChanged(const QString &name, const QVariantMap &changed, const QStringList &invalidated);
    void onServiceRegistered();
    void onServiceUnregistered();
    void onServiceOwnerChanged(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    QDBusMessage call(const QString &method) const;
    QDBusMessage get(const QString &property) const;
    void readHas();
    void readOrientation();
    void maybeClaim();
    void claim();
    void release();
    void lose();
    void take(const QString &raw);
    void settle();
    void setHas(bool has);
    void updateSupported();
    void setTurn(int turn);
    void setRaw(const QString &raw);
    void setSettledRaw(const QString &raw);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QTimer m_settle;
    QTimer m_reclaim;
    int m_sensorTurn = 1;
    bool m_present = false;
    bool m_has = false;
    bool m_holding = false;
    bool m_supported = false;
    bool m_wanted = false;
    bool m_claiming = false;
    bool m_claimed = false;
    int m_turn = -1;
    int m_last = -1;
    int m_pending = -1;
    QString m_raw;
    QString m_pendingRaw;
    QString m_settledRaw;
    quint64 m_generation = 0;
};

}
