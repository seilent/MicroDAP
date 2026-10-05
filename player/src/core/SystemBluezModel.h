#pragma once

#include "BluezModel.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QList>
#include <QMap>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

namespace casdap {

using InterfaceList = QMap<QString, QVariantMap>;
using ManagedObjectList = QMap<QDBusObjectPath, InterfaceList>;

class SystemBluezModel : public BluezModel
{
    Q_OBJECT

public:
    SystemBluezModel(const QDBusConnection &bluez, const QDBusConnection &session,
                     QObject *parent = nullptr);

    static void registerMetaTypes();
    static QString bluezService();
    static QString agentService();
    static QString agentPath();
    static QString agentInterface();
    static QString a2dpSourceUuid();

    QString devicePath() const;
    QString transportPath() const;
    QString playerPath() const;
    bool pollingPosition() const;

    void refresh();

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void rewind() override;
    void fastForward() override;
    void startPairing(int timeoutSeconds = defaultPairingTimeout) override;
    void stopPairing() override;
    void setShuffle(const QString &shuffle) override;
    void setRepeat(const QString &repeat) override;

private slots:
    void onInterfacesAdded(const QDBusMessage &message);
    void onInterfacesRemoved(const QDBusMessage &message);
    void onBluezPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated, const QDBusMessage &message);
    void onAgentPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                  const QStringList &invalidated);
    void onBluezUnregistered();
    void onAgentUnregistered();

private:
    void refreshAgent();
    void applyAgent(const QVariantMap &properties);
    void apply();
    void clearDevice();
    void select(QString &devicePath, QString &transportPath, QString &playerPath) const;
    QString childWithInterface(const QString &devicePath, const QString &interface) const;
    void syncPolling();
    void pollPosition();
    void callPlayer(const QString &method);
    void setPlayerProperty(const QString &name, const QString &value);
    void callAgent(const QString &method, const QList<QVariant> &arguments = {});

    QDBusConnection m_bluez;
    QDBusConnection m_session;
    QDBusServiceWatcher m_bluezWatcher;
    QDBusServiceWatcher m_agentWatcher;
    QMap<QString, InterfaceList> m_objects;
    QString m_devicePath;
    QString m_transportPath;
    QString m_playerPath;
    int m_transportVolume = -1;
    QTimer m_positionTimer;
    bool m_positionPending = false;
    quint64 m_agentGeneration = 0;
};

}
