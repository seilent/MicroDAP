#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace casdap {

using InterfaceList = QMap<QString, QVariantMap>;
using ManagedObjectList = QMap<QDBusObjectPath, InterfaceList>;

class Intercept : public QObject
{
    Q_OBJECT

public:
    static constexpr uint ModeNone = 0;
    static constexpr uint ModeOverlay = 3;

    explicit Intercept(const QDBusConnection &bus, QObject *parent = nullptr);
    ~Intercept() override;

    static QDBusConnection bus();
    static void registerMetaTypes();
    static QString service();
    static QString rootPath();
    static QString compositeInterface();
    static QString dbusDeviceInterface();
    static QString targetPrefix();

    bool shown() const;
    void setShown(bool shown);
    QStringList devices() const;
    QStringList targets() const;
    bool hasTargets() const;
    void refresh();

signals:
    void devicesChanged();
    void targetsChanged();

private slots:
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onServiceUnregistered();

private:
    void write(const QString &path, uint mode);
    void writeAll(uint mode);
    void track(const QString &path, const InterfaceList &interfaces, bool &devices, bool &targets);

    QDBusConnection m_bus;
    QDBusServiceWatcher m_watcher;
    bool m_shown = false;
    bool m_everShown = false;
    QSet<QString> m_devices;
    QSet<QString> m_targets;
    quint64 m_generation = 0;
};

}
