#pragma once

#include "BluezModel.h"
#include "PhoneArt.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusServiceWatcher>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

namespace microdap {

using InterfaceList = QMap<QString, QVariantMap>;
using ManagedObjectList = QMap<QDBusObjectPath, InterfaceList>;

class SystemBluezModel : public BluezModel
{
    Q_OBJECT

public:
    static constexpr int pairTimeoutMs = 60000;

    SystemBluezModel(const QDBusConnection &bluez, const QDBusConnection &session,
                     QObject *parent = nullptr);
    ~SystemBluezModel() override;

    static void registerMetaTypes();
    static QString bluezService();
    static QString agentService();
    static QString agentPath();
    static QString agentInterface();
    static QString a2dpSourceUuid();
    static QString engineService();
    static QString enginePath();
    static QString engineInterface();

    QString devicePath() const;
    QString transportPath() const;
    QString playerPath() const;
    bool pollingPosition() const;
    PhoneArt *phoneArt();

    void refresh();

    void play() override;
    void pause() override;
    void next() override;
    void previous() override;
    void rewind() override;
    void fastForward() override;
    void startPairing(int timeoutSeconds = defaultPairingTimeout) override;
    void stopPairing() override;
    void startSearch(int timeoutSeconds = defaultPairingTimeout) override;
    void stopSearch() override;
    void connectHeadset(const QString &path) override;
    void disconnectHeadset(const QString &path) override;
    void forgetHeadset(const QString &path) override;
    void pairDevice(const QString &path) override;
    void want() override;
    void setReceiverScope(bool held) override;
    void setDapSession(bool dap);
    void setShuffle(const QString &shuffle) override;
    void setRepeat(const QString &repeat) override;

protected:
    int roleDevices(const QString &role) const override;
    void dropRole(const QString &role) override;
    void applyRole(const QString &role) override;

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
    void checkRole(const QString &path);
    void checkHeadset(const QString &path);
    void applyHeadsets();
    void startPair(const QString &path);
    void callEngine(const QString &method, const QList<QVariant> &arguments = {});
    void syncEngine();
    void disconnectDevice(const QString &path);
    void applyArt(const QString &address, int psm, const QString &handle);
    void updateArt();
    void nudge(const QString &handle);
    void nudgeTimeout();
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
    PhoneArt m_art;
    QString m_imgHandle;
    QString m_handed;
    QString m_artSession;
    QSet<QString> m_wrongRole;
    QMap<QString, QString> m_pairState;
    QString m_pairAfterSearch;
    QString m_connectTarget;
    QDBusServiceWatcher m_engineWatcher;
    bool m_engineUp = false;
    bool m_dap = false;
    int m_scope = 0;
    QElapsedTimer m_activeSince;
    QTimer m_gateTimer;
    enum class Nudge { Idle, Next, Previous, Watch, Done };
    Nudge m_nudge = Nudge::Idle;
    QString m_nudgeDevice;
    QString m_nudgeTitle;
    int m_nudgeNumber = 0;
    QTimer m_nudgeTimer;
};

}
