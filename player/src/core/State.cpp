#include "State.h"

#include "Keep.h"
#include "Library.h"
#include "Player.h"
#include "Queue.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSet>
#include <QSocketNotifier>
#include <QSqlQuery>
#include <QStandardPaths>

#include <atomic>
#include <csignal>
#include <ctime>
#include <fcntl.h>
#include <numeric>
#include <sys/socket.h>
#include <unistd.h>

#include <utility>

namespace microdap {

namespace {

constexpr int saveIntervalMs = 10000;
constexpr int queueDelayMs = 1000;
constexpr int pauseSaveDelayMs = 500;
constexpr qint64 handoffWindowS = 60;
constexpr int pathChunk = 500;

std::atomic<int> signalFd{-1};

void handleSignal(int)
{
    const int fd = signalFd.load(std::memory_order_relaxed);
    if (fd < 0)
        return;
    const char byte = 1;
    const ssize_t written = ::write(fd, &byte, 1);
    (void)written;
}

QString statusName(Player::Status status)
{
    switch (status) {
    case Player::Playing:
        return QStringLiteral("playing");
    case Player::Paused:
        return QStringLiteral("paused");
    case Player::Stopped:
        break;
    }
    return QStringLiteral("stopped");
}

qint64 bootSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return now.tv_sec;
}

bool handoffOf(const QJsonObject &object)
{
    const qint64 savedAt = static_cast<qint64>(object.value(QStringLiteral("savedAt")).toDouble(-1));
    const qint64 age = bootSeconds() - savedAt;
    return object.value(QStringLiteral("handoff")).toBool(false)
           && object.value(QStringLiteral("bootId")).toString() == Keep::bootId() && savedAt >= 0 && age >= 0
           && age < handoffWindowS;
}

bool handoffPlaying(const QJsonObject &object)
{
    return handoffOf(object) && object.value(QStringLiteral("status")).toString() == QLatin1String("playing");
}

QList<int> parseOrder(const QJsonArray &array)
{
    QList<int> order;
    order.reserve(array.size());
    for (const QJsonValue &value : array) {
        if (!value.isDouble())
            return {};
        order.append(value.toInt());
    }
    return order;
}

QHash<QString, QueueEntry> entriesByPath(const QSqlDatabase &db, const QStringList &paths)
{
    QHash<QString, QueueEntry> found;
    const QStringList unique = QSet<QString>(paths.begin(), paths.end()).values();
    for (qsizetype from = 0; from < unique.size(); from += pathChunk) {
        const QStringList chunk = unique.mid(from, pathChunk);
        QStringList marks;
        marks.reserve(chunk.size());
        for (qsizetype i = 0; i < chunk.size(); ++i)
            marks.append(QStringLiteral("?"));
        QSqlQuery query(db);
        query.prepare(QStringLiteral("SELECT t.id, t.path, t.title, t.artist, a.title, "
                                     "COALESCE(t.art_path, a.art_path), t.duration_ms "
                                     "FROM tracks t JOIN albums a ON a.id = t.album_id "
                                     "WHERE t.path IN (%1)")
                          .arg(marks.join(QLatin1Char(','))));
        for (const QString &path : chunk)
            query.addBindValue(path);
        if (!query.exec())
            continue;
        while (query.next()) {
            QueueEntry entry;
            entry.id = query.value(0).toLongLong();
            entry.path = query.value(1).toString();
            entry.title = query.value(2).toString();
            entry.artist = query.value(3).toString();
            entry.album = query.value(4).toString();
            entry.artPath = query.value(5).toString();
            entry.durationMs = query.value(6).toLongLong();
            found.insert(entry.path, entry);
        }
    }
    return found;
}

}

State::State(Player *player, QObject *parent)
    : State(player, defaultPath(), parent)
{
}

State::State(Player *player, const QString &path, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_path(path)
    , m_queuePath(queuePathFor(path))
{
    m_timer.setInterval(saveIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, [this] { saveState(false); });
    m_queueTimer.setSingleShot(true);
    m_queueTimer.setInterval(queueDelayMs);
    connect(&m_queueTimer, &QTimer::timeout, this, &State::writeQueueLater);
    m_pauseTimer.setSingleShot(true);
    m_pauseTimer.setInterval(pauseSaveDelayMs);
    connect(&m_pauseTimer, &QTimer::timeout, this, [this] { save(false); });
    connect(m_player, &Player::statusChanged, this, &State::onStatusChanged);
    connect(m_player, &Player::positionChanged, this, &State::onPositionChanged);
    Queue *queue = m_player->queue();
    connect(queue, &QAbstractItemModel::modelReset, this, &State::onQueueChanged);
    connect(queue, &QAbstractItemModel::rowsInserted, this, &State::onQueueChanged);
    connect(queue, &QAbstractItemModel::rowsRemoved, this, &State::onQueueChanged);
    connect(queue, &QAbstractItemModel::rowsMoved, this, &State::onQueueChanged);
    connect(queue, &Queue::orderChanged, this, &State::onQueueChanged);
    onStatusChanged();
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &State::saveExit);
}

State::~State()
{
    m_thread.quit();
    m_thread.wait();
    delete m_worker;
    if (m_fds[0] >= 0) {
        signalFd.store(-1, std::memory_order_relaxed);
        struct sigaction action {};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGTERM, &action, nullptr);
        ::sigaction(SIGINT, &action, nullptr);
        ::sigaction(SIGHUP, &action, nullptr);
        ::close(m_fds[0]);
    }
    if (m_fds[1] >= 0)
        ::close(m_fds[1]);
}

QString State::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/microdap/state.json");
}

QString State::queuePathFor(const QString &statePath)
{
    return QFileInfo(statePath).absolutePath() + QStringLiteral("/queue.json");
}

QString State::path() const
{
    return m_path;
}

QString State::queuePath() const
{
    return m_queuePath;
}

qint64 State::queueRev() const
{
    return m_rev;
}

qint64 State::writtenQueueRev()
{
    QMutexLocker lock(&m_writeMutex);
    return m_writtenRev;
}

void State::onStatusChanged()
{
    if (m_player->status() == Player::Playing) {
        m_pauseTimer.stop();
        m_timer.start();
        return;
    }
    m_timer.stop();
    onPositionChanged();
}

void State::onPositionChanged()
{
    if (m_loading || m_player->status() != Player::Paused || m_player->resumePending()
        || (m_restore && m_restore->restorePending()))
        return;
    m_pauseTimer.start();
}

void State::onQueueChanged()
{
    if (m_loading)
        return;
    ++m_rev;
    m_queueTimer.start();
}

QueueSnapshot State::queueSnapshot() const
{
    const Queue *queue = m_player->queue();
    QueueSnapshot out;
    out.path = m_queuePath;
    out.rev = m_rev;
    out.index = queue->currentIndex();
    out.entries = queue->entries();
    out.order = queue->order();
    return out;
}

void State::writeQueueLater()
{
    m_queueTimer.stop();
    QueueSnapshot job = queueSnapshot();
    bool schedule = false;
    {
        QMutexLocker lock(&m_jobMutex);
        std::swap(m_job, job);
        m_hasJob = true;
        schedule = !m_scheduled;
        m_scheduled = true;
    }
    if (!schedule)
        return;
    if (!m_thread.isRunning())
        m_thread.start();
    QMetaObject::invokeMethod(m_worker, [this] { runQueueJobs(); }, Qt::QueuedConnection);
}

void State::runQueueJobs()
{
    for (;;) {
        QueueSnapshot job;
        {
            QMutexLocker lock(&m_jobMutex);
            if (!m_hasJob) {
                m_scheduled = false;
                return;
            }
            std::swap(job, m_job);
            m_hasJob = false;
        }
        QMutexLocker lock(&m_writeMutex);
        if (job.rev <= m_writtenRev)
            continue;
        if (writeQueue(job))
            m_writtenRev = job.rev;
        else
            qWarning() << "microdap state: cannot write" << job.path;
    }
}

bool State::flushQueue()
{
    m_queueTimer.stop();
    QueueSnapshot dropped;
    {
        QMutexLocker lock(&m_jobMutex);
        std::swap(dropped, m_job);
        m_hasJob = false;
    }
    QMutexLocker lock(&m_writeMutex);
    if (m_writtenRev == m_rev)
        return true;
    if (!writeQueue(queueSnapshot())) {
        qWarning() << "microdap state: cannot write" << m_queuePath;
        return false;
    }
    m_writtenRev = m_rev;
    return true;
}

QJsonObject State::snapshot(const Player *player, bool handoff, const QJsonArray &stack, qint64 queueRev)
{
    const Queue *queue = player->queue();
    QJsonObject object;
    object.insert(QStringLiteral("index"), queue->currentIndex());
    object.insert(QStringLiteral("position"), player->position());
    object.insert(QStringLiteral("shuffle"), queue->shuffle());
    object.insert(QStringLiteral("repeat"), static_cast<int>(queue->repeat()));
    object.insert(QStringLiteral("status"), statusName(player->status()));
    object.insert(QStringLiteral("savedAt"), bootSeconds());
    object.insert(QStringLiteral("bootId"), Keep::bootId());
    object.insert(QStringLiteral("handoff"), handoff);
    object.insert(QStringLiteral("stack"), stack);
    object.insert(QStringLiteral("queueRev"), queueRev);
    return object;
}

bool State::writeQueue(const QueueSnapshot &job)
{
    QJsonArray ids;
    QJsonArray paths;
    for (const QueueEntry &entry : job.entries) {
        ids.append(entry.id);
        paths.append(entry.path);
    }
    QJsonArray order;
    for (int row : job.order)
        order.append(row);
    QJsonObject object;
    object.insert(QStringLiteral("rev"), job.rev);
    object.insert(QStringLiteral("index"), job.index);
    object.insert(QStringLiteral("ids"), ids);
    object.insert(QStringLiteral("paths"), paths);
    object.insert(QStringLiteral("order"), order);
    return write(job.path, object);
}

bool State::write(const QString &path, const QJsonObject &object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool State::read(const QString &path, QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    object = document.object();
    return true;
}

bool State::saveState(bool handoff)
{
    const bool ok = write(m_path, snapshot(m_player, handoff, m_stack, m_rev));
    if (!ok)
        qWarning() << "microdap state: cannot write" << m_path;
    return ok;
}

bool State::save(bool handoff)
{
    const bool queueOk = flushQueue();
    const bool stateOk = saveState(handoff);
    return queueOk && stateOk;
}

bool State::saveNow()
{
    return save(false);
}

bool State::saveExit()
{
    return save(true);
}

void State::quit(bool handoff)
{
    save(handoff);
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    disconnect(app, &QCoreApplication::aboutToQuit, this, &State::saveExit);
    app->quit();
}

QVariantList State::stack() const
{
    return m_stack.toVariantList();
}

void State::setStack(const QVariantList &stack)
{
    QJsonArray out;
    for (const QVariant &entry : stack) {
        const QVariantMap map = entry.toMap();
        const QString name = map.value(QStringLiteral("name")).toString();
        if (name.isEmpty())
            continue;
        QJsonObject row;
        row.insert(QStringLiteral("name"), name);
        row.insert(QStringLiteral("id"), map.value(QStringLiteral("id"), -1).toLongLong());
        out.append(row);
    }
    m_stack = out;
}

QJsonArray State::savedStack(const QString &path)
{
    QJsonObject object;
    if (!read(path, object))
        return {};
    return object.value(QStringLiteral("stack")).toArray();
}

void State::applyModes(const QJsonObject &state)
{
    Queue *queue = m_player->queue();
    queue->setShuffle(state.value(QStringLiteral("shuffle")).toBool(false));
    const int repeat = state.value(QStringLiteral("repeat")).toInt(0);
    queue->setRepeat(static_cast<Queue::Repeat>(qBound(0, repeat, 2)));
}

QList<QueueEntry> State::resolveEntries(const QList<qint64> &ids, const QStringList &paths) const
{
    const qsizetype n = paths.size();
    QList<QueueEntry> entries(n);
    QList<bool> found(n, false);
    Library *library = m_player->library();
    if (library) {
        QVariantList unique;
        QSet<qint64> seen;
        for (qint64 id : ids) {
            if (id > 0 && !seen.contains(id)) {
                seen.insert(id);
                unique.append(id);
            }
        }
        QHash<qint64, QueueEntry> byId;
        for (const QueueEntry &entry : library->queueEntries(unique))
            byId.insert(entry.id, entry);
        QStringList missing;
        for (qsizetype i = 0; i < n; ++i) {
            const auto it = byId.constFind(ids.value(i));
            if (it != byId.constEnd() && it->path == paths.at(i)) {
                entries[i] = *it;
                found[i] = true;
            } else {
                missing.append(paths.at(i));
            }
        }
        if (!missing.isEmpty()) {
            const QHash<QString, QueueEntry> byPath = entriesByPath(library->database(), missing);
            for (qsizetype i = 0; i < n; ++i) {
                if (found.at(i))
                    continue;
                const auto it = byPath.constFind(paths.at(i));
                if (it == byPath.constEnd())
                    continue;
                entries[i] = *it;
                found[i] = true;
            }
        }
    }
    for (qsizetype i = 0; i < n; ++i) {
        if (found.at(i))
            continue;
        QueueEntry entry;
        entry.id = library ? 0 : ids.value(i);
        entry.path = paths.at(i);
        entry.title = QFileInfo(entry.path).completeBaseName();
        entries[i] = entry;
    }
    return entries;
}

bool State::restoreLegacy(const QJsonObject &object)
{
    QList<QueueEntry> entries;
    for (const QJsonValue &value : object.value(QStringLiteral("queue")).toArray())
        entries.append(QueueEntry::fromMap(value.toObject().toVariantMap()));
    if (entries.isEmpty())
        return false;
    applyModes(object);
    const bool kept = handoffOf(object);
    if (!kept && m_resume == QLatin1StringView("off"))
        return false;
    const int index = object.value(QStringLiteral("index")).toInt(0);
    const qint64 position = kept || m_resume != QLatin1StringView("track")
        ? static_cast<qint64>(object.value(QStringLiteral("position")).toDouble(0)) : 0;
    restorePlayer(entries, index, position, handoffPlaying(object),
                  parseOrder(object.value(QStringLiteral("order")).toArray()));
    return true;
}

bool State::restoreQueue(const QJsonObject &state, bool hasState)
{
    QJsonObject queue;
    if (!read(m_queuePath, queue))
        return false;
    const qint64 rev = queue.value(QStringLiteral("rev")).toInteger(0);
    m_rev = rev;
    {
        QMutexLocker lock(&m_writeMutex);
        m_writtenRev = rev;
    }
    const QJsonArray pathValues = queue.value(QStringLiteral("paths")).toArray();
    const QJsonArray idValues = queue.value(QStringLiteral("ids")).toArray();
    QStringList paths;
    QList<qint64> ids;
    paths.reserve(pathValues.size());
    ids.reserve(pathValues.size());
    for (qsizetype i = 0; i < pathValues.size(); ++i) {
        paths.append(pathValues.at(i).toString());
        ids.append(idValues.at(i).toInteger(0));
    }
    if (paths.isEmpty())
        return false;
    const QList<int> order = parseOrder(queue.value(QStringLiteral("order")).toArray());
    const QList<QueueEntry> entries = resolveEntries(ids, paths);
    if (hasState) {
        applyModes(state);
    } else {
        QJsonObject modes;
        modes.insert(QStringLiteral("shuffle"), !order.isEmpty());
        applyModes(modes);
    }
    const bool kept = hasState && handoffOf(state);
    if (!kept && m_resume == QLatin1StringView("off"))
        return false;
    const bool match = hasState && state.value(QStringLiteral("queueRev")).toInteger(-1) == rev;
    if (match) {
        const qint64 position = kept || m_resume != QLatin1StringView("track")
            ? static_cast<qint64>(state.value(QStringLiteral("position")).toDouble(0)) : 0;
        restorePlayer(entries, state.value(QStringLiteral("index")).toInt(0), position, handoffPlaying(state), order);
    } else {
        restorePlayer(entries, queue.value(QStringLiteral("index")).toInt(0), 0, false, order);
    }
    return true;
}

void State::restorePlayer(const QList<QueueEntry> &entries, int index, qint64 positionMs, bool playing,
                          const QList<int> &order)
{
    QList<int> kept = order;
    if (kept.isEmpty() && m_player->queue()->shuffle()) {
        kept.resize(entries.size());
        std::iota(kept.begin(), kept.end(), 0);
    }
    if (!m_restore || !m_restore->levelSet()) {
        m_player->restore(entries, index, positionMs, playing, kept);
        return;
    }
    m_player->restore(entries, index, positionMs, false, kept);
    m_player->park(positionMs);
    if (playing)
        m_restore->restorePlaying(true);
}

bool State::load()
{
    QJsonObject object;
    const bool hasState = read(m_path, object);
    m_stack = hasState ? object.value(QStringLiteral("stack")).toArray() : QJsonArray();
    const bool legacy = hasState && object.value(QStringLiteral("queue")).isArray();
    m_loading = true;
    const bool ok = legacy ? restoreLegacy(object) : restoreQueue(object, hasState);
    m_loading = false;
    if (legacy) {
        ++m_rev;
        save(false);
    }
    return ok;
}

void State::setRestore(RestorePort *restore)
{
    m_restore = restore;
}

void State::setResume(const QString &mode)
{
    m_resume = mode;
}

void State::installSignalHandlers()
{
    if (m_fds[0] >= 0)
        return;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, m_fds) != 0) {
        qWarning() << "microdap state: socketpair failed";
        m_fds[0] = -1;
        m_fds[1] = -1;
        return;
    }
    ::fcntl(m_fds[0], F_SETFL, ::fcntl(m_fds[0], F_GETFL) | O_NONBLOCK);
    signalFd.store(m_fds[0], std::memory_order_relaxed);

    m_notifier = new QSocketNotifier(m_fds[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &State::onSignal);

    struct sigaction action {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
}

void State::onSignal()
{
    char byte = 0;
    const ssize_t count = ::read(m_fds[1], &byte, 1);
    (void)count;
    m_notifier->setEnabled(false);
    quit();
}

}
