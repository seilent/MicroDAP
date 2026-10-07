#pragma once

#include "Queue.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>
#include <QVariantList>

class QSocketNotifier;

namespace casdap {

class Player;

struct QueueSnapshot
{
    QString path;
    qint64 rev = 0;
    int index = -1;
    QList<QueueEntry> entries;
    QList<int> order;
};

class State : public QObject
{
    Q_OBJECT

public:
    explicit State(Player *player, QObject *parent = nullptr);
    State(Player *player, const QString &path, QObject *parent = nullptr);
    ~State() override;

    static QString defaultPath();
    static QString queuePathFor(const QString &statePath);
    QString path() const;
    QString queuePath() const;
    qint64 queueRev() const;
    qint64 writtenQueueRev();

    Q_INVOKABLE bool saveNow();
    bool save(bool handoff);
    bool saveExit();
    bool saveState(bool handoff);
    bool flushQueue();
    void writeQueueLater();
    void quit(bool handoff = true);
    bool load();
    void installSignalHandlers();

    Q_INVOKABLE QVariantList stack() const;
    Q_INVOKABLE void setStack(const QVariantList &stack);

    QueueSnapshot queueSnapshot() const;
    static QJsonObject snapshot(const Player *player, bool handoff, const QJsonArray &stack = {},
                                qint64 queueRev = 0);
    static bool write(const QString &path, const QJsonObject &object);
    static bool writeQueue(const QueueSnapshot &snapshot);
    static bool read(const QString &path, QJsonObject &object);
    static QJsonArray savedStack(const QString &path);

private:
    void onSignal();
    void onStatusChanged();
    void onQueueChanged();
    void runQueueJobs();
    bool restoreLegacy(const QJsonObject &object);
    bool restoreQueue(const QJsonObject &state, bool hasState);
    void applyModes(const QJsonObject &state);
    QList<QueueEntry> resolveEntries(const QList<qint64> &ids, const QStringList &paths) const;

    Player *m_player;
    QString m_path;
    QString m_queuePath;
    QTimer m_timer;
    QTimer m_queueTimer;
    QJsonArray m_stack;
    QSocketNotifier *m_notifier = nullptr;
    int m_fds[2] = {-1, -1};
    qint64 m_rev = 0;
    bool m_loading = false;
    QThread m_thread;
    QObject *m_worker = nullptr;
    QMutex m_jobMutex;
    QueueSnapshot m_job;
    bool m_hasJob = false;
    bool m_scheduled = false;
    QMutex m_writeMutex;
    qint64 m_writtenRev = -1;
};

}
