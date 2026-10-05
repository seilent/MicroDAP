#pragma once

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
    bool positionPollEnabled() const;
    void setPositionPollEnabled(bool on);

    void play(const QList<QueueEntry> &entries, int start = 0);
    void restore(const QList<QueueEntry> &entries, int index, qint64 positionMs);
    Q_INVOKABLE void playAt(int index);
    Q_INVOKABLE void playPause();
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

private:
    struct Chain
    {
        QList<QString> paths;
        QList<int> order;
        bool shuffle = false;
        Queue::Repeat repeat = Queue::Off;
        int last = -1;
        QList<int> pending;
        bool stale = false;
    };

    static void aboutToFinish(GstElement *element, gpointer data);

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

    Library *m_library;
    Queue *m_queue;
    BluezModel *m_bluez = nullptr;
    GstElement *m_playbin = nullptr;
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
    bool m_removingCurrent = false;
    int m_removeTarget = -1;
    QMutex m_chainMutex;
    Chain m_chain;
};

}
