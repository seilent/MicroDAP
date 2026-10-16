#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QList>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace microdap {

using InterfaceList = QMap<QString, QVariantMap>;
using ManagedObjectList = QMap<QDBusObjectPath, InterfaceList>;

class Intercept : public QObject
{
    Q_OBJECT

public:
    static constexpr double RumbleLevel = 1.0;

    Intercept(const QDBusConnection &session, const QDBusConnection &inputPlumber, QObject *parent = nullptr);

    static QDBusConnection bus();
    static void registerMetaTypes();
    static QString service();
    static QString rootPath();
    static QString compositeInterface();
    static QString dbusDeviceInterface();
    static QString targetPrefix();
    static QString arbiterService();
    static QString arbiterPath();
    static QString arbiterInterface();

    bool wanted() const;
    void setWanted(bool wanted);
    bool holding() const;
    QString holder() const;
    QStringList devices() const;
    QStringList targets() const;
    bool hasTargets() const;
    void refresh();
    void rumble(const QList<int> &pattern);

signals:
    void holdingChanged();
    void devicesChanged();
    void targetsChanged();

private slots:
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onArbiterProperties(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);
    void onServiceUnregistered();

private:
    void acquire();
    void release();
    void readHolder();
    void setHolder(const QString &holder);
    void track(const QString &path, const InterfaceList &interfaces, bool &devices, bool &targets);
    void sendRumble(double level);

    QDBusConnection m_session;
    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    QDBusServiceWatcher m_arbiter;
    bool m_wanted = false;
    QString m_holder;
    QSet<QString> m_devices;
    QSet<QString> m_targets;
    quint64 m_generation = 0;
    quint64 m_request = 0;
};

}
