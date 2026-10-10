#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantMap>

namespace casdap {

class OverlayBridge;
class Player;
class PowerBridge;
class SleepInhibitor;
class State;

class PowerBridgeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.casdap.Player")
    Q_PROPERTY(bool ScreenOff READ screenOff)
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(bool OverlayShown READ overlayShown)

public:
    explicit PowerBridgeAdaptor(PowerBridge *bridge);

    bool screenOff() const;
    QString playbackStatus() const;
    bool overlayShown() const;

public slots:
    void SetScreenOff(bool off);
    void ShowPowerMenu();
    void ShowOverlay();
    void HideOverlay();
    void ToggleOverlay();
    void ShowSwitchPrompt();

signals:
    void ScreenOffChanged(bool screenOff);

private:
    PowerBridge *m_bridge;
};

class PowerBridge : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_PROPERTY(bool screenOff READ screenOff WRITE setScreenOff NOTIFY screenOffChanged)
    Q_PROPERTY(int volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(QStringList driftPaths READ driftPaths NOTIFY driftPathsChanged)
    Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)

public:
    PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent = nullptr);
    ~PowerBridge() override;

    static QString serviceName();
    static QString objectPath();
    static QString interfaceName();
    static QString hotkeysPath();
    static QString hotkeysInterface();
    static QString notSupportedError();
    static QStringList verbs();
    static QString powerBin();
    static QStringList commandFor(const QString &verb);
    static QStringList switchCommand();
    static QStringList wakeCommand();
    void runScreenOn();
    static int switchTimeoutMs();
    static QString driftFile();

    bool isRegistered() const;
    Player *player() const;
    OverlayBridge *overlay() const;
    void setOverlay(OverlayBridge *overlay);
    bool overlayMode() const;
    State *state() const;
    void setState(State *state);
    void enableSleepWatch();
    SleepInhibitor *sleep() const;
    bool waking() const;

    bool screenOff() const;
    void setScreenOff(bool off);
    int volume() const;
    void setVolume(int percent);
    QStringList driftPaths() const;
    QString playbackStatus() const;
    bool overlayShown() const;
    bool switching() const;

    Q_INVOKABLE bool runVerb(const QString &verb);
    Q_INVOKABLE void reloadDrift();
    Q_INVOKABLE void keepInBackground();
    Q_INVOKABLE void closeAndSwitch();
    void requestPowerMenu();
    void requestSwitchPrompt();
    void requestScreenOff(bool off);
    void showOverlay();
    void hideOverlay();
    void toggleOverlay();

signals:
    void screenOffChanged();
    void volumeChanged(int volume);
    void driftPathsChanged();
    void showPowerMenu();
    void showSwitchPrompt();
    void switchFailed();
    void switchingChanged();
    void wakeFinished(bool ok);

private slots:
    void onVolumeChanged(double level);
    void onSwitchFinished(int code, QProcess::ExitStatus status);
    void onSwitchError(QProcess::ProcessError error);
    void onSwitchTimeout();
    void onPrepareForSleep(bool start);
    void onWakeFinished(int code, QProcess::ExitStatus status);
    void onWakeError(QProcess::ProcessError error);

private:
    bool rejectWithoutOverlay(const char *member);
    QString programPath(const QString &name) const;
    void startSwitch();
    void failSwitch(const QString &reason);
    void dropSwitchProcess();
    void dropWakeProcess();
    void notifyPlaybackStatus();
    void notifyOverlayShown();
    void emitPropertiesChanged(const QVariantMap &changed);
    void emitScreenOffChanged();

    Player *m_player;
    QDBusConnection m_bus;
    PowerBridgeAdaptor *m_adaptor;
    OverlayBridge *m_overlay = nullptr;
    State *m_state = nullptr;
    SleepInhibitor *m_sleep = nullptr;
    QProcess *m_switch = nullptr;
    QProcess *m_wake = nullptr;
    QTimer m_switchTimer;
    bool m_switching = false;
    bool m_registered = false;
    bool m_screenOff = false;
    int m_volume = 0;
    QStringList m_driftPaths;
    QSet<QString> m_failedVerbs;
};

}
