#pragma once

#include "OutputPorts.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusContext>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>

namespace microdap {

class OutputMode;
class OverlayBridge;
class Player;
class PowerBridge;
class Settings;
class SleepInhibitor;
class State;

class PowerBridgeAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "net.seilent.MicroDap.Player")
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
    void ToggleScreen();
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
    Q_PROPERTY(bool switching READ switching NOTIFY switchingChanged)
    Q_PROPERTY(QVariantList sessions READ sessions CONSTANT)
    Q_PROPERTY(QString holdMode READ holdMode NOTIFY holdModeChanged)
    Q_PROPERTY(QString imageVersion READ imageVersion CONSTANT)
    Q_PROPERTY(QString updateStatus READ updateStatus NOTIFY updateChanged)
    Q_PROPERTY(QString updateVersion READ updateVersion NOTIFY updateChanged)
    Q_PROPERTY(QString updateState READ updateState NOTIFY updateChanged)
    Q_PROPERTY(QString updateChecked READ updateChecked NOTIFY updateChanged)
    Q_PROPERTY(int sleepMinutesLeft READ sleepMinutesLeft NOTIFY sleepTimerChanged)

public:
    static constexpr int toggleDebounceMs = 2000;
    static constexpr int sleepEndDelayMs = 500;

    PowerBridge(Player *player, const QDBusConnection &bus, QObject *parent = nullptr);
    ~PowerBridge() override;

    static QString serviceName();
    static QString objectPath();
    static QString interfaceName();
    static QString hotkeysPath();
    static QString hotkeysInterface();
    static QString notSupportedError();
    static QString powerdService();
    static QString powerdPath();
    static QString powerdInterface();
    static QDBusConnection powerdBus();
    static QStringList verbs();
    static QStringList switchCommand(const QString &session);
    static bool validSession(const QString &name);
    static QString sessionsDir();
    static QVariantList readSessions(const QString &dir, const QString &current);
    static QString backlightDir();
    static QString wifiMarker();
    static QString nmcliPath();
    static QString osReleasePath();
    static QString updateFile();
    static QString readKey(const QString &path, const QString &key);
    static QString checkStatus(const QString &show);
    static int switchTimeoutMs();
    static int idleCheckMs();
    static int idleMinuteMs();
    static constexpr int switchReadyMarginMs = 500;

    bool isRegistered() const;
    Player *player() const;
    OverlayBridge *overlay() const;
    void setOverlay(OverlayBridge *overlay);
    bool overlayMode() const;
    State *state() const;
    void setState(State *state);
    void setSettings(Settings *settings);
    void setOutputMode(OutputMode *mode, PlayerPort *player);
    void enableSleepWatch();
    void enableHolds();
    SleepInhibitor *sleep() const;
    bool waking() const;
    bool transition() const;

    bool screenOff() const;
    void setScreenOff(bool off);
    int volume() const;
    void setVolume(int percent);
    QString playbackStatus() const;
    bool overlayShown() const;
    bool switching() const;
    QVariantList sessions() const;
    QString holdMode() const;
    QString imageVersion() const;
    QString updateStatus() const;
    QString updateVersion() const;
    QString updateState() const;
    QString updateChecked() const;
    int sleepMinutesLeft() const;

    Q_INVOKABLE bool runVerb(const QString &verb);
    Q_INVOKABLE void runScreenOff();
    Q_INVOKABLE void runScreenOn();
    Q_INVOKABLE void keepInBackground();
    Q_INVOKABLE void closeAndSwitch();
    Q_INVOKABLE bool switchTo(const QString &session);
    Q_INVOKABLE void reloadUpdate();
    Q_INVOKABLE void startSleepTimer(int minutes);
    Q_INVOKABLE void noteInput();
    void requestPowerMenu();
    void requestSwitchPrompt();
    void requestScreenOff(bool off);
    void toggleScreen();
    void showOverlay();
    void hideOverlay();
    void toggleOverlay();
    void checkIdle();

signals:
    void screenOffChanged();
    void volumeChanged(int volume);
    void showPowerMenu();
    void showSwitchPrompt();
    void switchFailed();
    void switchingChanged();
    void holdModeChanged();
    void updateChanged();
    void sleepTimerChanged();
    void wakeFinished(bool ok);
    void verbFailed(const QString &verb);

private slots:
    void onVolumeChanged(double level);
    void onSwitchFinished(int code, QProcess::ExitStatus status);
    void onSwitchError(QProcess::ProcessError error);
    void onSwitchTimeout();
    void onPrepareForSleep(bool start);
    void onPowerdOwner(const QString &name, const QString &oldOwner, const QString &newOwner);

private:
    enum class SwitchFailure { BeforeStart, ExitCode, StartError, Timeout };

    bool rejectWithoutOverlay(const char *member);
    void startSwitch(const QString &session);
    void onSwitchReady();
    void runSwitch();
    void failSwitch(SwitchFailure kind, const QString &reason);
    void dropSwitchProcess();
    void screenOffNow();
    void screenOnNow(bool wake);
    void writeBacklight(int value);
    void radio(bool on, bool wake);
    void endTransition(bool ok, bool wake);
    void logind(const QString &method);
    void setUpdate(const QString &status, const QString &version);
    bool readUpdateFile();
    void watchUpdate();
    void applyHold();
    void sendHold();
    void notifyPlaybackStatus();
    void notifyOverlayShown();
    void emitPropertiesChanged(const QVariantMap &changed);
    void emitScreenOffChanged();
    void sleepTick();
    void setSleepLeft(int minutes);

    Player *m_player;
    QDBusConnection m_bus;
    QDBusConnection m_powerd;
    PowerBridgeAdaptor *m_adaptor;
    OverlayBridge *m_overlay = nullptr;
    State *m_state = nullptr;
    Settings *m_settings = nullptr;
    OutputMode *m_outputMode = nullptr;
    PlayerPort *m_switchPlayer = nullptr;
    PlayerPort::PlayState m_switchPlay = PlayerPort::PlayState::Stopped;
    SleepInhibitor *m_sleep = nullptr;
    QProcess *m_switch = nullptr;
    QProcess *m_radio = nullptr;
    QProcess *m_check = nullptr;
    bool m_checkAgain = false;
    QTimer m_switchTimer;
    QTimer m_readyTimer;
    QTimer m_idleTimer;
    QTimer m_sleepTimer;
    QTimer m_sleepEnd;
    QElapsedTimer m_sleepClock;
    qint64 m_sleepMs = 0;
    int m_sleepMinuteMs = 0;
    int m_sleepLeft = 0;
    QElapsedTimer m_idleSince;
    QElapsedTimer m_lastTransition;
    QString m_switchTarget;
    QString m_holdMode;
    QString m_imageVersion;
    QString m_updateStatus = QStringLiteral("unchecked");
    QString m_updateVersion;
    QString m_updateState;
    QString m_updateChecked;
    QFileSystemWatcher m_updateWatch;
    QString m_heldMode;
    QVariantList m_sessions;
    bool m_holds = false;
    bool m_switching = false;
    bool m_awaitReady = false;
    bool m_switchReleased = false;
    bool m_switchWasParked = false;
    bool m_registered = false;
    bool m_screenOff = false;
    bool m_wakePending = false;
    int m_volume = 0;
};

}
