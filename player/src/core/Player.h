#pragma once

#include "Deck.h"
#include "OutputPorts.h"
#include "Queue.h"
#include "ReplayGain.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <gst/gst.h>

#include <atomic>
#include <memory>
#include <optional>

class QSocketNotifier;

namespace microdap {

class BluezModel;
class Crossfade;
class Favorites;
class Library;
class OutputPolicy;
class Tail;

void initGStreamer(int &argc, char **argv);

class Player : public QObject, public PlayerPort
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(Queue *queue READ queue CONSTANT)
    Q_PROPERTY(bool positionPollEnabled READ positionPollEnabled WRITE setPositionPollEnabled NOTIFY positionPollEnabledChanged)
    Q_PROPERTY(QVariantMap sourceFormat READ sourceFormat NOTIFY formatChanged)
    Q_PROPERTY(QVariantMap sinkFormat READ sinkFormat NOTIFY formatChanged)

public:
    enum Status {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(Status)

    struct PendingBuild
    {
        QueueEntry entry;
        GstState target = GST_STATE_PAUSED;
        qint64 positionMs = 0;
        bool sameListen = false;
    };

    static QString sinkDescription(OutputChain chain, const QString &audioSinkEnv,
                                   const QString &exclusiveEnv, const QString &policyExclusive,
                                   const QString &cardId = {});
    static QString bitPerfectProperties(const QString &target, int rate);
    static bool isDsd(const QString &path);

    explicit Player(Library *library = nullptr, QObject *parent = nullptr);
    ~Player() override;

    Status status() const;
    qint64 position() const;
    qint64 duration() const;
    QVariantMap current() const;
    Queue *queue() const;
    Library *library() const;
    bool isValid() const;
    BluezModel *bluez() const;
    void setBluez(BluezModel *bluez);
    void setFavorites(Favorites *favorites);
    bool transportActive() const;
    void setTransportActive(bool active);
    bool transportReady() const;
    void setTransportReady(bool ready);
    bool playPending() const;
    bool resumePending() const;
    bool positionPollEnabled() const;
    void setPositionPollEnabled(bool on);
    GstElement *audioFilter() const;
    GstElement *audioSink() const;
    void setPolicy(OutputPolicy *policy);
    OutputChain builtChain() const override;
    bool parked() const override;
    bool dsd() const override;
    std::optional<PendingBuild> pendingBuild() const;
    QVariantMap sourceFormat() const override;
    QVariantMap sinkFormat() const override;
    void buildPending(OutputChain chain) override;
    void rebuildAt(qint64 positionMs, GstState state) override;
    void resumeFrom() override;
    void park(qint64 positionMs) override;
    PlayState playState() const override;
    qint64 positionMs() const override;

    void play(const QList<QueueEntry> &entries, int start = 0);
    Q_INVOKABLE int playSource(const QString &kind, qint64 id, const QString &mode, qint64 startId = -1);
    Q_INVOKABLE void playTracks(const QVariantList &trackIds, int start = 0);
    Q_INVOKABLE int playIds(const QVariantList &trackIds, const QString &mode, qint64 startId = -1);
    Q_INVOKABLE int enqueueTracks(const QVariantList &trackIds);
    Q_INVOKABLE int playNextTracks(const QVariantList &trackIds);
    void restore(const QList<QueueEntry> &entries, int index, qint64 positionMs,
                 bool playing = false, const QList<int> &order = {});
    Q_INVOKABLE void playAt(int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void pause();
    void outputLost();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void rate();
    void setReplayGain(const QString &mode);
    void setCrossfade(int ms);

signals:
    void statusChanged();
    void positionChanged();
    void durationChanged();
    void currentChanged();
    void positionPollEnabledChanged();
    void formatChanged();
    void trackStarted(int index);
    void endOfStream();
    void error(const QString &message);
    void notice(const QString &text);
    void added(int count);
    void localPlayRequested();

private:
    struct Chain
    {
        QList<QueueEntry> entries;
        QList<int> order;
        QList<int> slot;
        bool shuffle = false;
        Queue::Repeat repeat = Queue::Off;
        int last = -1;
        QList<int> pending;
        bool stale = false;
        bool held = false;
    };

    struct BitPerfectBinding
    {
        GstElement *element;
        QString target;
    };

    struct Run
    {
        QList<int> rows;
        QList<Deck::Mark> marks;
        GstClockTime stop = GST_CLOCK_TIME_NONE;
    };

    Deck::Track nextEntry();
    int nextRow(const Chain &chain) const;
    Run runFrom(const Chain &chain, int row) const;
    qsizetype pendingRun(const Chain &chain, int current, qsizetype &from) const;
    void handleCueMark(GstMessage *message);
    qint64 rangeStartMs() const;
    void planFade();
    void prepareFade();
    bool dropFade(bool idle = false);
    void finishFade();
    void endEarly();
    bool queryPosition(gint64 &position) const;
    static GstPadProbeReturn bitPerfectProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static void setStreamProperties(GstElement *element, const QString &text);

    bool configure(OutputChain chain, bool dsd);
    GstElement *pwFilter(int rate);
    void bindBitPerfect(GstElement *sink);
    void build(int index, const PendingBuild &pending, OutputChain chain, bool guard);
    void deckFailed(int index, const PendingBuild &pending);
    void request(int index, GstState target, qint64 positionMs, bool guard, bool sameListen);
    void hold(int index, const PendingBuild &pending);
    void readFormats();
    void setFormats(const QVariantMap &source, const QVariantMap &sink);
    void pollBus();
    void pollPosition();
    void handleStreamStart(bool mark = false);
    void handleEos();
    void handleError(GstMessage *message);
    void skipBroken(const QString &text);
    void handleAsyncDone();
    void startAt(int index, GstState target, qint64 positionMs = 0, bool sameListen = false);
    void setNull();
    bool doSeek(qint64 ms);
    void setStatus(Status status);
    void setPosition(qint64 ms);
    void updateDuration();
    void syncChain();
    void onCountChanged();
    void onRowsAboutToBeRemoved(const QModelIndex &parent, int first, int last);
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsMoved(const QModelIndex &parent, int start, int end,
                     const QModelIndex &destination, int row);
    void addHistoryNow();
    void onTransportActiveChanged();
    int addTracks(const QVariantList &trackIds, bool next);
    int addEntries(const QList<QueueEntry> &entries, bool next);
    void playEntries(const QList<QueueEntry> &entries, int start, bool shuffle);
    int playList(const QList<QueueEntry> &entries, const QString &mode, qint64 startId);
    bool holdForPhone();
    void releaseHold();
    void cancelHold();
    void resumeNow();

    Library *m_library;
    Queue *m_queue;
    BluezModel *m_bluez = nullptr;
    QPointer<Favorites> m_favorites;
    bool m_transportActive = false;
    bool m_transportReady = true;
    bool m_playPending = false;
    bool m_resumeOnReady = false;
    QTimer m_holdTimer;
    GstElement *m_pipeline = nullptr;
    std::unique_ptr<Deck> m_deck;
    std::unique_ptr<Tail> m_tail;
    GstElement *m_filter = nullptr;
    GstElement *m_sink = nullptr;
    QString m_audioSinkEnv;
    QString m_exclusiveEnv;
    OutputPolicy *m_policy = nullptr;
    OutputChain m_configChain = OutputChain::None;
    QString m_configSink;
    QString m_configCard;
    int m_configRate = 0;
    bool m_configDsd = false;
    OutputChain m_builtChain = OutputChain::None;
    bool m_parked = false;
    bool m_dsd = false;
    bool m_outputErrorSent = false;
    bool m_playAfterSeek = false;
    bool m_startHeld = false;
    bool m_sameListen = false;
    bool m_listenCounted = false;
    bool m_listenStarted = false;
    std::optional<PendingBuild> m_pending;
    QVariantMap m_sourceFormat;
    QVariantMap m_sinkFormat;
    GstBus *m_bus = nullptr;
    QSocketNotifier *m_busNotifier = nullptr;
    QTimer m_positionTimer;
    bool m_positionPoll = true;
    Status m_status = Stopped;
    qint64 m_position = 0;
    qint64 m_duration = 0;
    qint64 m_pendingSeek = -1;
    bool m_prerolled = false;
    bool m_historyCounted = true;
    int m_errorStreak = 0;
    int m_firstErrorRow = -1;
    bool m_removingCurrent = false;
    int m_removeTarget = -1;
    QMutex m_chainMutex;
    Chain m_chain;
    std::atomic<ReplayGainMode> m_replayGain{ReplayGainMode::Off};
    int m_crossfadeMs = 0;
    std::unique_ptr<Deck> m_fade;
    std::unique_ptr<Crossfade> m_crossfade;
    int m_fadeGeneration = 0;
    QueueEntry m_fadeEntry;
    bool m_fadeAdded = false;
    bool m_fadeSwapped = false;
    bool m_replanFade = false;
    GstClockTime m_fadeOffset = GST_CLOCK_TIME_NONE;
    GstClockTime m_cueStop = GST_CLOCK_TIME_NONE;
    bool m_firstStart = false;
    bool m_skipStart = false;
};

struct Admit
{
    bool now = true;
    Player::OutputChain chain = Player::OutputChain::Pw;
};

class OutputPolicy
{
public:
    virtual ~OutputPolicy() = default;

    virtual Player::OutputChain chainFor(GstState target) const = 0;
    virtual Admit admit(const Player::PendingBuild &build) = 0;
    virtual QString exclusiveSink() const = 0;
    virtual QString exclusiveCard() const = 0;
    virtual QString bitPerfectSink() const = 0;
    virtual int fixedRate() const = 0;
    virtual void outputError(int code) = 0;
};

}
