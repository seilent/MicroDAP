#pragma once

#include "AlsaVolume.h"
#include "DeviceReservation.h"
#include "OutputPorts.h"
#include "Player.h"
#include "VolumeKeys.h"

#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

namespace casdap {

class BluezModel;
class CodecFragment;
class EqControl;

class OutputMode : public QObject, public OutputPolicy, public RestorePort
{
    Q_OBJECT
    Q_PROPERTY(QString levelStatus READ levelStatus NOTIFY levelStatusChanged)
    Q_PROPERTY(bool effective READ effective NOTIFY effectiveChanged)
    Q_PROPERTY(QVariantList rates READ rates NOTIFY ratesChanged)
    Q_PROPERTY(bool rateReadOnly READ rateReadOnly NOTIFY effectiveChanged)

public:
    struct Bounds
    {
        int pcmFreeMs = 1000;
        int pcmPollMs = 20;
        int acquireMs = 2000;
        int codecWaitMs = 3000;
        int parkMs = 5000;
        int netFirstMs = 1500;
        int netSecondMs = 3000;
        int switchMs = 1000;
        int pcmWatchMs = 1500;
        int retryMs = 60000;
        int resolveMs = 3000;
        int readyMs = 3000;
        int quitMs = 1000;
        int sleepReleaseMs = 1000;
        double minRangeDb = 40.0;
        double volumeToleranceDb = 0.05;
        DeviceReservation::Bounds reservation;
        int pacingMs = AlsaVolume::defaultPacingMs;
        int storeDelayMs = RoutedSinkVolume::defaultStoreDelayMs;
    };

    struct Ports
    {
        PlayerPort *player = nullptr;
        ReservationPort *reservation = nullptr;
        HeldOutputPort *output = nullptr;
        DacSinkPort *dac = nullptr;
        DacVolumePort *volume = nullptr;
        AlsaVolume *mixer = nullptr;
        CodecFragment *codec = nullptr;
        BluezModel *bluez = nullptr;
        EqControl *eq = nullptr;
    };

    enum class State { Pw, Bp, ExIdle, ExAcquiring, ExHeld };
    enum class Reason { None, Phone, Volume, NoVolume, Unavailable, Released, Idle, Switch, Lost };

    OutputMode(const Bounds &bounds, const Ports &ports, QObject *parent = nullptr);
    ~OutputMode() override;

    static QList<int> builtInRates();

    State state() const;
    Reason reason() const;
    QString stateName() const;
    QString reasonName() const;
    QString level() const;
    QString levelStatus() const;
    bool effective() const;
    QVariantList rates() const;
    bool rateReadOnly() const;
    bool releasing() const;
    quint64 generation() const;
    QString cardId() const;

    Player::OutputChain chainFor(GstState target) const override;
    Admit admit(const Player::PendingBuild &build) override;
    QString exclusiveSink() const override;
    QString exclusiveCard() const override;
    int fixedRate() const override;
    void outputError(int code) override;

    bool levelSet() const override;
    void restorePlaying(bool playing) override;

public slots:
    void setLevel(const QString &level);
    void setSampleRate(const QString &rate);
    void onActiveOutputChanged(const QString &active, const QString &usbSink);
    void onApplied(const QString &output, bool ok);
    void onCardGone(int index);
    void onSinksRead(const QString &usbSink, quint64 read);
    void onStatusChanged();
    void onEndOfStream();
    void onHwRateChanged(int rate);
    void onMixerDisconnected();
    void onVolumePressed();
    void onSinkAppeared(quint32 index, const QString &name, int card);
    void onSinkChanged(quint32 index);
    void onCardRemoved(quint32 cardIndex);
    void onBluezConnectedChanged();
    void onBluezReadyChanged();
    void onTransportStateChanged();
    void onCodecRestartFinished(bool ok);
    void onAcquired(quint64 seq);
    void onRefused(quint64 seq);
    void onFailed(quint64 seq, const QString &reason);
    void onReleaseRequested(int priority);
    void onReleaseTimedOut();
    void onNameLost(bool requested);
    void onConnectionLost();
    void onReleased();
    void onFormatChanged();
    void releaseForSwitch();
    void prepareForSleep(bool sleeping);
    void aboutToQuit();

signals:
    void levelStatusChanged();
    void effectiveChanged();
    void ratesChanged();
    void switchReady();
    void notice(const QString &text);

private:
    enum class Phase { None, WaitReleased, WaitNameLost, Sink, Read, Restart, Second };
    enum class Acquire { None, Card, Check, Reserve, Pcm };
    enum class Build { None, Park, Keep, Continue };

    struct Card
    {
        int alsa = -1;
        quint32 cardIndex = 0xffffffffu;
        QString reserveName;
        QString id;
        QString sink;
    };

    struct Release
    {
        bool running = false;
        Reason reason = Reason::None;
        Phase phase = Phase::None;
        bool net = true;
        bool switching = false;
        bool failed = false;
        bool moved = false;
        bool read = false;
        quint64 readAfter = 0;
    };

    bool exclusive() const;
    bool blocked() const;
    Reason blockReason() const;
    bool retryPending() const;
    bool cardPresent() const;
    QString readCardId(int alsa) const;
    QString pcmStatus() const;
    void bump();
    void disconnectEvent(const char *source);
    void handleOutputError(int code);
    void enterLevel(bool adopt);
    void refreshCard(const QString &sink);
    void adoptStep();
    void checkStopped();
    void leaveLevel();
    void cardReturn(const QString &sink, bool release);
    void checkVolume();
    void reconcile();
    void settle();
    void updateAll();
    void updateStatus();
    QString computeStatus() const;
    void schedulePark();
    void startAcquire();
    void acquireCard(const DacCard &card);
    void acquireChecked(bool blocked);
    void pollPcm();
    void acquireDone();
    void failBeforeReserve(Reason reason);
    void acquireTimedOut();
    void abortAcquire(Reason reason, Build build);
    void dropAcquire();
    void startRelease(Reason reason, Build build, bool net, bool switching = false);
    void releaseName();
    void startReturn();
    void sinkFound(const QString &name);
    void maybeEndReturn();
    void endReturn();
    void endReturnGone();
    void failReturn(bool deferred);
    void onNetTimer();
    void retryNet();
    void finishRelease();
    void decideWaiting();
    void resumeRestore();
    void waitReady();
    void drivePark(qint64 positionMs);
    void driveRebuild(qint64 positionMs, GstState state);
    void driveBuild(Player::OutputChain chain);
    void log(const QString &text) const;

    Bounds m_bounds;
    Ports m_ports;
    QString m_level = QStringLiteral("off");
    QString m_rate = QStringLiteral("auto");
    QString m_active;
    QString m_usbSink;
    Card m_card;
    QHash<QString, quint64> m_generations;
    State m_state = State::Pw;
    Reason m_reason = Reason::None;
    Release m_r;
    Acquire m_acq = Acquire::None;
    bool m_cardGone = false;
    bool m_nameOwned = false;
    bool m_volumeBlock = false;
    bool m_noVolume = false;
    bool m_refused = false;
    bool m_netSticky = false;
    bool m_deferredNet = false;
    int m_netFirings = 0;
    qint64 m_retryAt = -1;
    bool m_waiting = false;
    GstState m_waitTarget = GST_STATE_PAUSED;
    bool m_codecWaiting = false;
    bool m_asleep = false;
    bool m_sleepHeld = false;
    bool m_sleepReturn = false;
    quint64 m_sleepGeneration = 0;
    bool m_restoreWanted = false;
    bool m_resolved = false;
    bool m_quitting = false;
    bool m_reconcile = false;
    int m_builtRate = 0;
    bool m_routeAlsa = false;
    QString m_routeCard;
    QString m_routeSink;
    bool m_bypassed = false;
    int m_storeAllowed = -1;
    int m_driving = 0;
    int m_hwRate = 0;
    quint64 m_acqSeq = 0;
    quint64 m_admitSeq = 0;
    quint64 m_entrySeq = 0;
    quint64 m_cardSeq = 0;
    quint64 m_volumeSeq = 0;
    quint64 m_reserveSeq = 0;
    bool m_entryPending = false;
    bool m_storePending = false;
    QList<bool> m_releases;
    PlayerPort::PlayState m_lastPlay = PlayerPort::PlayState::Stopped;
    QString m_status = QStringLiteral("off");
    bool m_effective = false;
    QVariantList m_rates;
    QElapsedTimer m_clock;
    QElapsedTimer m_pcmClock;
    QTimer m_park;
    QTimer m_acquireTimer;
    QTimer m_pcmPoll;
    QTimer m_codecWait;
    QTimer m_netTimer;
    QTimer m_resolveTimer;
    QTimer m_readyTimer;
};

}
