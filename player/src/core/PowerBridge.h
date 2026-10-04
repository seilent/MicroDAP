#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

namespace casdap {

class Player;
class PowerBridge;

class PowerBridgeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.casdap.Player")
    Q_PROPERTY(bool ScreenOff READ screenOff)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)

public:
    explicit PowerBridgeAdaptor(PowerBridge *bridge);

    bool screenOff() const;
    QString playbackStatus() const;

public slots:
    void SetScreenOff(bool off);
    void ShowPowerMenu();

signals:
    void ScreenOffChanged(bool screenOff);

private:
    PowerBridge *m_bridge;
};

class PowerBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool screenOff READ screenOff WRITE setScreenOff NOTIFY screenOffChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QStringList driftPaths READ driftPaths NOTIFY driftPathsChanged)

public:
    PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent = nullptr);
    ~PowerBridge() override;

    static QString serviceName();
    static QString objectPath();
    static QString interfaceName();
    static QString hotkeysPath();
    static QString hotkeysInterface();
    static QStringList verbs();
    static QStringList commandFor(const QString &verb);
    static QString driftFile();

    bool isRegistered() const;
    Player *player() const;

    bool screenOff() const;
    void setScreenOff(bool off);
    int volume() const;
    QStringList driftPaths() const;
    QString playbackStatus() const;

    Q_INVOKABLE bool runVerb(const QString &verb);
    Q_INVOKABLE void reloadDrift();
    void requestPowerMenu();

signals:
    void screenOffChanged();
    void volumeChanged(int volume);
    void driftPathsChanged();
    void showPowerMenu();

private slots:
    void onVolumeChanged(double level);

private:
    void notifyPlaybackStatus();
    void emitPropertiesChanged(const QVariantMap &changed);
    void emitScreenOffChanged();

    Player *m_player;
    QDBusConnection m_bus;
    PowerBridgeAdaptor *m_adaptor;
    bool m_registered = false;
    bool m_screenOff = false;
    int m_volume = 0;
    QStringList m_driftPaths;
    QSet<QString> m_failedVerbs;
};

}
