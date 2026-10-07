#pragma once

#include "Eq.h"
#include "Queue.h"

#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantMap>

#include <gst/gst.h>

class QSocketNotifier;

namespace casdap {

class BluezModel;
class Library;

void initGStreamer(int &argc, char **argv);

class Player : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Status status READ status NOTIFY statusChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(Queue *queue READ queue CONSTANT)
    Q_PROPERTY(bool positionPollEnabled READ positionPollEnabled WRITE setPositionPollEnabled NOTIFY positionPollEnabledChanged)

public:
    enum Status {
        Stopped,
        Playing,
        Paused,
    };
    Q_ENUM(Status)

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
    bool transportActive() const;
    void setTransportActive(bool active);
    bool transportReady() const;
    void setTransportReady(bool ready);
    bool playPending() const;
    bool positionPollEnabled() const;
    void setPositionPollEnabled(bool on);
    GstElement *audioFilter() const;
    void applyEq(const casdap::EqProfile &profile);

    void play(const QList<QueueEntry> &entries, int start = 0);
    Q_INVOKABLE int playSource(const QString &kind, qint64 id, const QString &mode, qint64 startId = -1);
    Q_INVOKABLE void playTracks(const QVariantList &trackIds, int start = 0);
    Q_INVOKABLE int enqueueTracks(const QVariantList &trackIds);
    Q_INVOKABLE int playNextTracks(const QVariantList &trackIds);
    void restore(const QList<QueueEntry> &entries, int index, qint64 positionMs,
                 bool playing = false, const QList<int> &order = {});
    Q_INVOKABLE void playAt(int index);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 ms);
    Q_INVOKABLE void toggleShuffle();
    Q_INVOKABLE void cycleRepeat();
    Q_INVOKABLE void rate();

signals:
    void statusChanged();
    void positionChanged();
    void durationChanged();
    void currentChanged();
    void positionPollEnabledChanged();
    void trackStarted(int index);
    void endOfStream();
    void error(const QString &message);
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
    };

    static void aboutToFinish(GstElement *element, gpointer data);

    void buildEq();
    void pollBus();
    void pollPosition();
    void handleStreamStart();
    void handleEos();
    void handleError(GstMessage *message);
    void handleAsyncDone();
    void startAt(int index, GstState target);
    void setUri(const QString &path);
    void setNull();
    void doSeek(qint64 ms);
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
    bool holdForPhone();
    void releaseHold();
    void cancelHold();
    void resumeNow();

    Library *m_library;
    Queue *m_queue;
    BluezModel *m_bluez = nullptr;
    bool m_transportActive = false;
    bool m_transportReady = true;
    bool m_playPending = false;
    bool m_resumeOnReady = false;
    QTimer m_holdTimer;
    GstElement *m_playbin = nullptr;
    GstElement *m_eqFilter = nullptr;
    GstElement *m_preamp = nullptr;
    QList<GObject *> m_eqBands;
    GstBus *m_bus = nullptr;
    gulong m_aboutToFinishId = 0;
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
};

}
