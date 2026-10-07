#include "FakeBluezModel.h"
#include "Library.h"
#include "Player.h"
#include "Queue.h"
#include "State.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <memory>

using namespace casdap;

namespace {

constexpr int scanTimeoutMs = 60000;
constexpr int playTimeoutMs = 5000;

bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = QDir(from).relativeFilePath(info.absoluteFilePath());
        const QString target = to + QLatin1Char('/') + relative;
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return false;
        if (!QFile::copy(info.absoluteFilePath(), target))
            return false;
    }
    return true;
}

QVariant scalar(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0);
}

QList<QueueEntry> albumEntries(const QSqlDatabase &db, const QString &album)
{
    QList<QueueEntry> entries;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT t.id, t.path, t.title, t.artist, a.title, a.art_path, t.duration_ms "
        "FROM tracks t JOIN albums a ON a.id = t.album_id WHERE a.title = ? ORDER BY t.track_no"));
    query.addBindValue(album);
    if (!query.exec())
        return entries;
    while (query.next()) {
        QueueEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.path = query.value(1).toString();
        entry.title = query.value(2).toString();
        entry.artist = query.value(3).toString();
        entry.album = query.value(4).toString();
        entry.artPath = query.value(5).toString();
        entry.durationMs = query.value(6).toLongLong();
        entries.append(entry);
    }
    return entries;
}

QList<int> startedIndexes(const QSignalSpy &spy)
{
    QList<int> indexes;
    for (const QList<QVariant> &args : spy)
        indexes.append(args.at(0).toInt());
    return indexes;
}

bool encodeSine(const QString &path, int seconds)
{
    const QString description =
        QStringLiteral("audiotestsrc num-buffers=%1 wave=sine freq=440 ! audioconvert ! flacenc "
                       "! filesink location=\"%2\"")
            .arg(seconds * 44)
            .arg(path);
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(description.toUtf8().constData(), &error);
    g_clear_error(&error);
    if (!pipeline)
        return false;
    gst_element_set_state(pipeline, GST_STATE_PLAYING);
    GstBus *bus = gst_element_get_bus(pipeline);
    GstMessage *message = gst_bus_timed_pop_filtered(
        bus, 30 * GST_SECOND, static_cast<GstMessageType>(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
    const bool ok = message && GST_MESSAGE_TYPE(message) == GST_MESSAGE_EOS;
    if (message)
        gst_message_unref(message);
    gst_object_unref(bus);
    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(pipeline);
    return ok;
}

}

class PlayerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void queueOrder();
    void queueEdits();
    void gapless();
    void removeBeforeCurrent();
    void removeCurrent();
    void repeatOne();
    void shuffleOrder();
    void previousGoesBack();
    void positionWithoutPoll();
    void rating();
    void stateRoundTrip();
    void stateHandoff();
    void eqFilter();
    void queueSlots();
    void queueShuffleEdits();
    void addVerbs();
    void addUnderShuffle();
    void stateOrder();
    void errorKeepsPaused();
    void errorAllRows();
    void phoneGuard();
    void phoneGuardTimeout();
    void handoffWaitsForTransport();
    void queueSignals();
    void addStoppedShuffle();
    void playSource();
    void stateQueueWorker();
    void stateMigration();
    void stateRevMismatch();
    void stateRestoreByPath();

private:
    int historyCount() const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
    QList<QueueEntry> m_tracks;
    QueueEntry m_long;
};

namespace {

std::unique_ptr<Player> syncPlayer(Library *library = nullptr)
{
    const QByteArray sink = qgetenv("CASDAP_AUDIO_SINK");
    qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    auto player = std::make_unique<Player>(library);
    qputenv("CASDAP_AUDIO_SINK", sink);
    return player;
}

QList<QueueEntry> namedEntries(int n)
{
    QList<QueueEntry> entries;
    for (int i = 0; i < n; ++i) {
        QueueEntry entry;
        entry.id = i + 1;
        entry.title = QStringLiteral("e%1").arg(i);
        entry.path = QStringLiteral("/nonexistent/e%1.flac").arg(i);
        entries.append(entry);
    }
    return entries;
}

QVariantList rows(std::initializer_list<int> values)
{
    QVariantList list;
    for (int value : values)
        list.append(value);
    return list;
}

bool consistent(const Queue &queue)
{
    const PlayOrderModel *order = queue.playOrder();
    if (order->rowCount() != queue.count())
        return false;
    for (int slot = 0; slot < queue.count(); ++slot) {
        const QVariantMap map = order->get(slot);
        const int row = queue.rowAt(slot);
        if (map.value(QStringLiteral("row")).toInt() != row || queue.slotOf(row) != slot
            || map.value(QStringLiteral("number")).toInt() != slot + 1
            || map.value(QStringLiteral("id")).toLongLong() != queue.entry(row).id
            || order->data(order->index(slot), PlayOrderModel::IsCurrentRole).toBool()
                   != (row == queue.currentIndex()))
            return false;
    }
    return true;
}

QJsonObject readJson(const QString &path)
{
    QJsonObject object;
    State::read(path, object);
    return object;
}

}

int PlayerTest::historyCount() const
{
    return scalar(m_library->database(), QStringLiteral("SELECT COUNT(*) FROM history")).toInt();
}

void PlayerTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));

    QVERIFY(m_dir.isValid());
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(CASDAP_FIXTURE_MUSIC), m_music));

    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"),
                                          m_dir.path() + QStringLiteral("/art"));
    QVERIFY(m_library->isOpen());
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));

    m_tracks = albumEntries(m_library->database(), QStringLiteral("Album One"));
    QVERIFY(m_tracks.size() >= 3);
    m_tracks = m_tracks.mid(0, 3);
    for (const QueueEntry &entry : m_tracks) {
        QVERIFY(entry.id > 0);
        QVERIFY(QFileInfo::exists(entry.path));
    }

    m_long.path = m_dir.path() + QStringLiteral("/guard.flac");
    m_long.title = QStringLiteral("Guard");
    m_long.durationMs = 6000;
    QVERIFY(encodeSine(m_long.path, 6));
}

void PlayerTest::queueOrder()
{
    Queue queue;
    QSignalSpy indexSpy(&queue, &Queue::indexChanged);
    QSignalSpy countSpy(&queue, &Queue::countChanged);
    queue.set(m_tracks, 0);
    QCOMPARE(queue.count(), 3);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(indexSpy.count(), 1);
    QCOMPARE(countSpy.count(), 1);
    QCOMPARE(queue.roleNames().value(Queue::PathRole), QByteArrayLiteral("path"));
    QCOMPARE(queue.data(queue.index(0), Queue::IsCurrentRole).toBool(), true);
    QCOMPARE(queue.data(queue.index(1), Queue::IsCurrentRole).toBool(), false);
    QCOMPARE(queue.get(1).value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);

    QCOMPARE(queue.nextIndex(), 1);
    QCOMPARE(queue.previousIndex(), -1);
    queue.setCurrentIndex(2);
    QCOMPARE(queue.nextIndex(), -1);
    QCOMPARE(queue.previousIndex(), 1);

    queue.setRepeat(Queue::All);
    QCOMPARE(queue.nextIndex(), 0);
    queue.setCurrentIndex(0);
    QCOMPARE(queue.previousIndex(), 2);

    queue.setRepeat(Queue::One);
    QCOMPARE(queue.nextIndex(), 0);
    QCOMPARE(queue.nextIndex(true), 1);
    QCOMPARE(queue.previousIndex(), 0);

    queue.setRepeat(Queue::Off);
    queue.setCurrentIndex(1);
    queue.setShuffle(true);
    QCOMPARE(queue.order().size(), 3);
    QCOMPARE(queue.order().first(), 1);
    QList<int> visited{queue.currentIndex()};
    while (true) {
        const int next = queue.nextIndex();
        if (next < 0)
            break;
        visited.append(next);
        queue.setCurrentIndex(next);
    }
    std::sort(visited.begin(), visited.end());
    QCOMPARE(visited, QList<int>({0, 1, 2}));
}

void PlayerTest::queueEdits()
{
    Queue queue;
    queue.set(m_tracks, 1);
    queue.playNext({m_tracks.at(0)});
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.entry(2).path, m_tracks.at(0).path);
    QCOMPARE(queue.nextIndex(), 2);

    queue.enqueue({m_tracks.at(2)});
    QCOMPARE(queue.count(), 5);
    QCOMPARE(queue.entry(4).path, m_tracks.at(2).path);

    queue.remove(0);
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.currentIndex(), 0);
    QCOMPARE(queue.entry(0).path, m_tracks.at(1).path);

    queue.move(0, 3);
    QCOMPARE(queue.currentIndex(), 3);
    QCOMPARE(queue.entry(3).path, m_tracks.at(1).path);
    QCOMPARE(queue.entry(0).path, m_tracks.at(0).path);

    queue.remove(3);
    QCOMPARE(queue.count(), 3);
    QCOMPARE(queue.currentIndex(), 2);

    queue.setShuffle(true);
    queue.playNext({m_tracks.at(1)});
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.nextIndex(), 3);
    QCOMPARE(queue.order().size(), 4);

    queue.clear();
    QCOMPARE(queue.count(), 0);
    QCOMPARE(queue.currentIndex(), -1);
    queue.enqueue({m_tracks.at(0)});
    QCOMPARE(queue.currentIndex(), 0);
}

void PlayerTest::gapless()
{
    Player player(m_library.get());
    QVERIFY(player.isValid());
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy eos(&player, &Player::endOfStream);
    QSignalSpy errors(&player, &Player::error);
    QList<int> eosAtStart;
    connect(&player, &Player::trackStarted, this, [&eosAtStart, &eos] {
        eosAtStart.append(static_cast<int>(eos.count()));
    });
    const int historyBefore = historyCount();

    player.play(m_tracks, 0);
    QCOMPARE(player.status(), Player::Playing);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(0).id);

    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, playTimeoutMs);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2}));
    QCOMPARE(eosAtStart, QList<int>({0, 0, 0}));
    QCOMPARE(errors.count(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(player.queue()->currentIndex(), 2);
    QCOMPARE(player.position(), qint64(0));
    QCOMPARE(historyCount() - historyBefore, 3);
    QCOMPARE(scalar(m_library->database(),
                    QStringLiteral("SELECT COUNT(*) FROM history WHERE track_id = ?"),
                    {m_tracks.at(1).id})
                 .toInt(),
             1);
}

void PlayerTest::removeBeforeCurrent()
{
    Player player;
    QVERIFY(player.isValid());
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy eos(&player, &Player::endOfStream);

    player.restore(m_tracks, 1, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 1);

    player.queue()->remove(0);
    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(started.count(), 1);

    player.playPause();
    QCOMPARE(player.status(), Player::Playing);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QCOMPARE(started.at(1).at(0).toInt(), 1);
    QCOMPARE(player.queue()->entry(1).path, m_tracks.at(2).path);

    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    QCOMPARE(started.count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 1);
}

void PlayerTest::removeCurrent()
{
    Player player;
    QVERIFY(player.isValid());
    QSignalSpy started(&player, &Player::trackStarted);

    player.restore(m_tracks, 0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 0);

    player.queue()->remove(0);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QCOMPARE(started.at(1).at(0).toInt(), 0);
    QCOMPARE(player.current().value(QStringLiteral("path")).toString(), m_tracks.at(1).path);

    player.queue()->remove(1);
    QCOMPARE(player.queue()->count(), 1);
    QCOMPARE(player.status(), Player::Paused);
    QTest::qWait(100);
    QCOMPARE(started.count(), 2);

    player.queue()->remove(0);
    QCOMPARE(player.status(), Player::Stopped);
    QCOMPARE(player.queue()->count(), 0);
    QCOMPARE(player.queue()->currentIndex(), -1);

    player.queue()->setRepeat(Queue::All);
    player.restore(m_tracks, 2, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, playTimeoutMs);
    QCOMPARE(started.at(2).at(0).toInt(), 2);

    player.queue()->remove(2);
    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.status(), Player::Paused);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 4, playTimeoutMs);
    QCOMPARE(started.at(3).at(0).toInt(), 0);
    QCOMPARE(player.current().value(QStringLiteral("path")).toString(), m_tracks.at(0).path);

    player.stop();
    player.queue()->remove(0);
    QCOMPARE(player.status(), Player::Stopped);
    QCOMPARE(player.queue()->count(), 1);
    QTest::qWait(100);
    QCOMPARE(started.count(), 4);
}

void PlayerTest::repeatOne()
{
    Player player;
    player.queue()->setRepeat(Queue::One);
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy eos(&player, &Player::endOfStream);

    player.play(m_tracks, 0);
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 2, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 0);
    QCOMPARE(started.at(1).at(0).toInt(), 0);
    QCOMPARE(eos.count(), 0);
    QCOMPARE(player.status(), Player::Playing);

    player.stop();
    QCOMPARE(player.status(), Player::Stopped);
    QCOMPARE(player.queue()->currentIndex(), 0);
}

void PlayerTest::shuffleOrder()
{
    Player player;
    player.toggleShuffle();
    QVERIFY(player.queue()->shuffle());
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy eos(&player, &Player::endOfStream);

    player.play(m_tracks, 1);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, playTimeoutMs);
    QList<int> seen = startedIndexes(started);
    QCOMPARE(seen.first(), 1);
    std::sort(seen.begin(), seen.end());
    QCOMPARE(seen, QList<int>({0, 1, 2}));

    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(started.count(), 3);
}

void PlayerTest::previousGoesBack()
{
    Player player;
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy eos(&player, &Player::endOfStream);

    player.play(m_tracks, 0);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(player.queue()->currentIndex(), 2);
    QCOMPARE(player.position(), qint64(0));

    started.clear();
    player.previous();
    QCOMPARE(player.status(), Player::Playing);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 2, playTimeoutMs);
    QCOMPARE(startedIndexes(started), QList<int>({1, 2}));

    player.next();
    QCOMPARE(player.status(), Player::Stopped);
}

void PlayerTest::positionWithoutPoll()
{
    QueueEntry entry;
    entry.path = m_dir.path() + QStringLiteral("/long.flac");
    entry.title = QStringLiteral("Long");
    entry.durationMs = 6000;
    QVERIFY(encodeSine(entry.path, 6));

    const QByteArray sink = qgetenv("CASDAP_AUDIO_SINK");
    qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    Player player;
    qputenv("CASDAP_AUDIO_SINK", sink);
    QVERIFY(player.isValid());
    player.setPositionPollEnabled(false);
    QVERIFY(!player.positionPollEnabled());
    QSignalSpy started(&player, &Player::trackStarted);

    player.play({entry}, 0);
    QCOMPARE(player.status(), Player::Playing);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QTest::qWait(600);
    player.pause();
    QCOMPARE(player.status(), Player::Paused);
    QVERIFY(player.position() >= 400);

    player.playPause();
    QCOMPARE(player.status(), Player::Playing);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() > 4000, playTimeoutMs);
    player.previous();
    QCOMPARE(player.status(), Player::Playing);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QTRY_VERIFY_WITH_TIMEOUT(player.position() < 1000, playTimeoutMs);
    QTest::qWait(300);
    QCOMPARE(started.count(), 1);
    player.stop();
}

void PlayerTest::rating()
{
    Player player(m_library.get());
    player.queue()->set(m_tracks, 1);
    const qint64 id = m_tracks.at(1).id;
    QCOMPARE(m_library->rating(id), 0);
    QCOMPARE(player.current().value(QStringLiteral("rating")).toInt(), 0);

    QSignalSpy currentSpy(&player, &Player::currentChanged);
    player.rate();
    QCOMPARE(m_library->rating(id), 1);
    QCOMPARE(player.current().value(QStringLiteral("rating")).toInt(), 1);
    QCOMPARE(currentSpy.count(), 1);

    player.rate();
    QCOMPARE(m_library->rating(id), 0);

    Player noLibrary;
    noLibrary.queue()->set(m_tracks, 1);
    noLibrary.rate();
    QCOMPARE(m_library->rating(id), 0);
}

void PlayerTest::stateRoundTrip()
{
    QVERIFY(State::defaultPath().endsWith(QStringLiteral("/casdap/state.json")));
    const QString path = m_dir.path() + QStringLiteral("/state/state.json");
    {
        Player player;
        State state(&player, path);
        QCOMPARE(state.path(), path);
        player.queue()->set(m_tracks, 1);
        player.queue()->setShuffle(true);
        player.queue()->setRepeat(Queue::All);
        QVERIFY(state.saveNow());
    }
    QVERIFY(QFileInfo::exists(path));

    QJsonObject object;
    QVERIFY(State::read(path, object));
    QCOMPARE(object.value(QStringLiteral("index")).toInt(), 1);
    QVERIFY(!object.contains(QStringLiteral("queue")));
    QJsonObject queueFile;
    QVERIFY(State::read(State::queuePathFor(path), queueFile));
    QCOMPARE(static_cast<int>(queueFile.value(QStringLiteral("ids")).toArray().size()), 3);
    QCOMPARE(static_cast<int>(queueFile.value(QStringLiteral("paths")).toArray().size()), 3);
    QCOMPARE(object.value(QStringLiteral("queueRev")).toInteger(), queueFile.value(QStringLiteral("rev")).toInteger());

    Player player;
    State state(&player, path);
    QVERIFY(state.load());
    QSignalSpy started(&player, &Player::trackStarted);
    QCOMPARE(player.queue()->count(), 3);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QVERIFY(player.queue()->shuffle());
    QCOMPARE(player.queue()->repeat(), Queue::All);
    QCOMPARE(player.queue()->entry(1).path, m_tracks.at(1).path);
    QCOMPARE(player.queue()->entry(1).id, m_tracks.at(1).id);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);

    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 1);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.queue()->currentIndex(), 1);

    Player empty;
    State missing(&empty, m_dir.path() + QStringLiteral("/missing.json"));
    QVERIFY(!missing.load());
    QCOMPARE(empty.queue()->count(), 0);
}

void PlayerTest::stateHandoff()
{
    const QString path = m_dir.path() + QStringLiteral("/handoff/state.json");
    auto restored = [&path] {
        Player player;
        State state(&player, path);
        return state.load() ? player.status() : Player::Stopped;
    };
    auto saved = [&path](const QString &key) {
        QJsonObject object;
        State::read(path, object);
        return object.value(key);
    };

    {
        Player player;
        State state(&player, path);
        player.play(m_tracks, 1);
        QCOMPARE(player.status(), Player::Playing);
        QVERIFY(QMetaObject::invokeMethod(QCoreApplication::instance(), "aboutToQuit"));
    }
    QCOMPARE(saved(QStringLiteral("status")).toString(), QStringLiteral("playing"));
    QCOMPARE(saved(QStringLiteral("handoff")).toBool(), true);
    QVERIFY(saved(QStringLiteral("savedAt")).toDouble() > 0);
    QCOMPARE(restored(), Player::Playing);

    {
        Player player;
        State state(&player, path);
        player.play(m_tracks, 1);
        state.quit();
    }
    QCOMPARE(saved(QStringLiteral("handoff")).toBool(), true);
    QCOMPARE(restored(), Player::Playing);

    {
        Player player;
        State state(&player, path);
        player.play(m_tracks, 1);
        QVERIFY(state.saveNow());
    }
    QCOMPARE(saved(QStringLiteral("status")).toString(), QStringLiteral("playing"));
    QCOMPARE(saved(QStringLiteral("handoff")).toBool(), false);
    QCOMPARE(restored(), Player::Paused);

    {
        Player player;
        State state(&player, path);
        player.play(m_tracks, 1);
        QVERIFY(state.saveExit());
    }
    QJsonObject aged;
    QVERIFY(State::read(path, aged));
    aged.insert(QStringLiteral("savedAt"), QDateTime::currentSecsSinceEpoch() - 61);
    QVERIFY(State::write(path, aged));
    QCOMPARE(restored(), Player::Paused);

    {
        Player player;
        State state(&player, path);
        player.play(m_tracks, 1);
        player.pause();
        QCOMPARE(player.status(), Player::Paused);
        QVERIFY(state.saveExit());
    }
    QCOMPARE(saved(QStringLiteral("status")).toString(), QStringLiteral("paused"));
    QCOMPARE(saved(QStringLiteral("handoff")).toBool(), true);
    QCOMPARE(restored(), Player::Paused);
}

void PlayerTest::eqFilter()
{
    {
        qputenv("CASDAP_EQ_DISABLE", QByteArrayLiteral("1"));
        Player player;
        qunsetenv("CASDAP_EQ_DISABLE");
        QVERIFY(player.isValid());
        QVERIFY(!player.audioFilter());
    }

    Player player;
    QVERIFY(player.audioFilter());
    GstElement *preamp = gst_bin_get_by_name(GST_BIN(player.audioFilter()), "casdap-preamp");
    GstElement *eq = gst_bin_get_by_name(GST_BIN(player.audioFilter()), "casdap-eq");
    QVERIFY(preamp);
    QVERIFY(eq);
    QCOMPARE(gst_child_proxy_get_children_count(GST_CHILD_PROXY(eq)), guint(EqProfile::bandCount));
    GObject *band0 = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(eq), 0);
    GObject *band1 = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(eq), 1);
    QVERIFY(band0);
    QVERIFY(band1);

    double volume = 0.0;
    double gain = -1.0;
    g_object_get(preamp, "volume", &volume, nullptr);
    g_object_get(band0, "gain", &gain, nullptr);
    QCOMPARE(volume, 1.0);
    QCOMPARE(gain, 0.0);

    EqProfile profile = EqProfile::flat();
    profile.preamp = -6.0;
    profile.bands[0].type = EqBand::LowShelf;
    profile.bands[0].freq = 105.0;
    profile.bands[0].freqIndex = -1;
    profile.bands[0].gain = 4.5;
    profile.bands[0].q = 0.7;
    profile.bands[1].enabled = false;
    profile.bands[1].gain = 3.0;
    profile.bands[2].type = EqBand::HighShelf;
    profile.bands[2].freq = 10000.0;
    profile.bands[2].freqIndex = -1;
    profile.bands[2].gain = -2.0;
    profile.bands[2].q = 0.7;
    for (int i = 3; i < EqProfile::bandCount; ++i)
        profile.bands[i].enabled = false;

    player.play(m_tracks, 0);
    QCOMPARE(player.status(), Player::Playing);
    player.applyEq(profile);
    QCOMPARE(player.status(), Player::Playing);

    double freq = 0.0;
    double bandwidth = 0.0;
    gint type = -1;
    g_object_get(preamp, "volume", &volume, nullptr);
    g_object_get(band0, "gain", &gain, "freq", &freq, "bandwidth", &bandwidth, "type", &type, nullptr);
    QVERIFY(std::abs(volume - std::pow(10.0, -0.3)) < 1e-6);
    QVERIFY(std::abs(gain - 4.5) < 1e-6);
    QVERIFY(std::abs(freq - 105.0) < 1e-6);
    QVERIFY(std::abs(bandwidth - 150.0) < 1e-6);
    QCOMPARE(type, gint(EqBand::LowShelf));
    g_object_get(band1, "gain", &gain, nullptr);
    QCOMPARE(gain, 0.0);
    GObject *band2 = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(eq), 2);
    QVERIFY(band2);
    g_object_get(band2, "gain", &gain, "freq", &freq, "type", &type, nullptr);
    QVERIFY(std::abs(gain + 2.0) < 1e-6);
    QVERIFY(std::abs(freq - 10000.0) < 1e-6);
    QCOMPARE(type, gint(EqBand::HighShelf));

    player.applyEq(EqProfile::off());
    g_object_get(preamp, "volume", &volume, nullptr);
    g_object_get(band0, "gain", &gain, nullptr);
    QCOMPARE(volume, 1.0);
    QCOMPARE(gain, 0.0);
    g_object_get(band2, "gain", &gain, nullptr);
    QCOMPARE(gain, 0.0);
    player.stop();

    g_object_unref(band0);
    g_object_unref(band1);
    g_object_unref(band2);
    gst_object_unref(eq);
    gst_object_unref(preamp);
}

void PlayerTest::queueSlots()
{
    Queue queue;
    queue.set(namedEntries(5), 2);
    QCOMPARE(queue.orderedRows(), rows({0, 1, 2, 3, 4}));
    QCOMPARE(queue.currentSlot(), 2);
    QCOMPARE(queue.rowAt(4), 4);
    QCOMPARE(queue.rowAt(5), -1);
    QCOMPARE(queue.slotOf(3), 3);
    QCOMPARE(queue.upcoming(3), rows({3, 4}));
    queue.setRepeat(Queue::All);
    QCOMPARE(queue.upcoming(3), rows({3, 4, 0}));
    queue.setRepeat(Queue::One);
    QCOMPARE(queue.upcoming(3), rows({3, 4}));
    queue.setRepeat(Queue::Off);

    queue.playNextRow(0);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.entry(2).title, QStringLiteral("e0"));
    QCOMPARE(queue.nextIndex(), 2);

    queue.playNextRow(4);
    QCOMPARE(queue.entry(2).title, QStringLiteral("e4"));
    QCOMPARE(queue.entry(3).title, QStringLiteral("e0"));
    QCOMPARE(queue.currentIndex(), 1);

    QSignalSpy orderSpy(&queue, &Queue::orderChanged);
    QSignalSpy movedSpy(queue.playOrder(), &QAbstractItemModel::rowsMoved);
    queue.playNextRow(2);
    queue.playNextRow(1);
    QCOMPARE(orderSpy.count(), 0);
    QCOMPARE(queue.entry(2).title, QStringLiteral("e4"));

    queue.moveSlot(3, 4);
    QCOMPARE(queue.entry(4).title, QStringLiteral("e0"));
    QCOMPARE(queue.entry(3).title, QStringLiteral("e3"));
    QCOMPARE(orderSpy.count(), 0);
    QCOMPARE(movedSpy.count(), 1);
    QCOMPARE(queue.playOrder()->get(4).value(QStringLiteral("title")).toString(), QStringLiteral("e0"));
}

void PlayerTest::queueShuffleEdits()
{
    Queue queue;
    queue.set(namedEntries(5), 2);
    queue.setShuffle(true);
    QCOMPARE(queue.order().size(), 5);
    QCOMPARE(queue.order().first(), 2);
    QCOMPARE(queue.currentSlot(), 0);
    QCOMPARE(queue.rowAt(0), 2);
    QCOMPARE(queue.slotOf(2), 0);

    QSignalSpy orderSpy(&queue, &Queue::orderChanged);
    QVERIFY(queue.setOrder({2, 0, 1, 3, 4}));
    QCOMPARE(orderSpy.count(), 1);
    QCOMPARE(queue.orderedRows(), rows({2, 0, 1, 3, 4}));
    QCOMPARE(queue.nextIndex(), 0);

    queue.moveSlot(2, 1);
    QCOMPARE(queue.orderedRows(), rows({2, 1, 0, 3, 4}));
    QCOMPARE(queue.nextIndex(), 1);
    QCOMPARE(queue.entry(1).title, QStringLiteral("e1"));
    QCOMPARE(queue.entry(0).title, QStringLiteral("e0"));

    queue.playNextRow(4);
    QCOMPARE(queue.orderedRows(), rows({2, 4, 1, 0, 3}));
    QCOMPARE(queue.nextIndex(), 4);
    QCOMPARE(queue.entry(4).title, QStringLiteral("e4"));

    queue.remove(1);
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.entry(1).title, QStringLiteral("e2"));
    QCOMPARE(queue.orderedRows(), rows({1, 3, 0, 2}));
    QCOMPARE(queue.currentSlot(), 0);
    QCOMPARE(queue.nextIndex(), 3);
    QCOMPARE(queue.upcoming(3), rows({3, 0, 2}));
    queue.setRepeat(Queue::All);
    QCOMPARE(queue.upcoming(5), rows({3, 0, 2}));
    queue.setRepeat(Queue::Off);

    queue.setShuffle(false);
    QCOMPARE(queue.orderedRows(), rows({0, 1, 2, 3}));
    QCOMPARE(queue.currentIndex(), 1);
    QCOMPARE(queue.entry(1).title, QStringLiteral("e2"));
    QVERIFY(!queue.setOrder({0, 1, 2, 3}));

    queue.setShuffle(true);
    QCOMPARE(queue.order().first(), 1);
    QVERIFY(!queue.setOrder({0, 0, 1, 2}));
    QVERIFY(!queue.setOrder({1, 0, 2}));
    QVERIFY(queue.setOrder({3, 2, 1, 0}));

    queue.set(namedEntries(4), 1, {3, 2, 1, 0});
    QCOMPARE(queue.order(), QList<int>({3, 2, 1, 0}));
    queue.set(namedEntries(4), 1, {9, 0, 1, 2});
    QCOMPARE(queue.order().size(), 4);
    QCOMPARE(queue.order().first(), 1);
}

void PlayerTest::addVerbs()
{
    Player player(m_library.get());
    QSignalSpy added(&player, &Player::added);
    QCOMPARE(player.enqueueTracks({}), 0);
    QCOMPARE(player.enqueueTracks({QVariant(qint64(999999))}), 0);
    QCOMPARE(player.playNextTracks({QVariant(qint64(999999))}), 0);
    QCOMPARE(added.count(), 0);
    QCOMPARE(player.queue()->count(), 0);

    QCOMPARE(player.enqueueTracks({QVariant(m_tracks.at(0).id), QVariant(qint64(999999)),
                                   QVariant(m_tracks.at(1).id)}),
             2);
    QCOMPARE(added.count(), 1);
    QCOMPARE(added.at(0).at(0).toInt(), 2);
    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.position(), qint64(0));
    QCOMPARE(player.queue()->entry(1).id, m_tracks.at(1).id);

    QCOMPARE(player.playNextTracks({QVariant(m_tracks.at(2).id)}), 1);
    QCOMPARE(player.queue()->count(), 3);
    QCOMPARE(player.queue()->entry(1).id, m_tracks.at(2).id);
    QCOMPARE(player.queue()->nextIndex(), 1);
    QCOMPARE(player.status(), Player::Paused);

    QCOMPARE(player.enqueueTracks({QVariant(m_tracks.at(0).id)}), 1);
    QCOMPARE(player.queue()->count(), 4);
    QCOMPARE(player.queue()->entry(3).id, m_tracks.at(0).id);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(added.count(), 3);

    player.stop();
    QCOMPARE(player.enqueueTracks({QVariant(m_tracks.at(1).id)}), 1);
    QCOMPARE(player.queue()->count(), 1);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);
    player.stop();

    Player noLibrary;
    QCOMPARE(noLibrary.enqueueTracks({QVariant(m_tracks.at(0).id)}), 0);
    QCOMPARE(noLibrary.playNextTracks({QVariant(m_tracks.at(0).id)}), 0);
}

void PlayerTest::addUnderShuffle()
{
    Player player(m_library.get());
    player.restore(m_tracks, 1, 0);
    player.toggleShuffle();
    QCOMPARE(player.queue()->currentSlot(), 0);

    QCOMPARE(player.enqueueTracks({QVariant(m_tracks.at(0).id)}), 1);
    QCOMPARE(player.queue()->count(), 4);
    QCOMPARE(player.queue()->entry(3).id, m_tracks.at(0).id);
    QVERIFY(player.queue()->slotOf(3) > player.queue()->currentSlot());
    QCOMPARE(player.queue()->order().size(), 4);

    QCOMPARE(player.playNextTracks({QVariant(m_tracks.at(2).id)}), 1);
    QCOMPARE(player.queue()->count(), 5);
    QCOMPARE(player.queue()->entry(2).id, m_tracks.at(2).id);
    QCOMPARE(player.queue()->nextIndex(), 2);
    QCOMPARE(player.queue()->rowAt(1), 2);
    QCOMPARE(player.status(), Player::Paused);
    player.stop();
}

void PlayerTest::stateOrder()
{
    const QString path = m_dir.path() + QStringLiteral("/order/state.json");
    const QString queuePath = State::queuePathFor(path);
    {
        Player player;
        State state(&player, path);
        player.queue()->setShuffle(true);
        player.queue()->set(m_tracks, 1);
        QVERIFY(player.queue()->setOrder({1, 2, 0}));
        QVERIFY(state.saveNow());
    }
    QJsonObject object;
    QVERIFY(State::read(queuePath, object));
    QCOMPARE(object.value(QStringLiteral("order")).toArray(), QJsonArray({1, 2, 0}));

    {
        Player player;
        State state(&player, path);
        QVERIFY(state.load());
        QVERIFY(player.queue()->shuffle());
        QCOMPARE(player.queue()->order(), QList<int>({1, 2, 0}));
        QCOMPARE(player.queue()->nextIndex(), 2);
        player.stop();
    }

    object.insert(QStringLiteral("order"), QJsonArray({0, 0, 1}));
    QVERIFY(State::write(queuePath, object));
    {
        Player player;
        State state(&player, path);
        QVERIFY(state.load());
        QList<int> order = player.queue()->order();
        QCOMPARE(order.size(), 3);
        QCOMPARE(order.first(), 1);
        std::sort(order.begin(), order.end());
        QCOMPARE(order, QList<int>({0, 1, 2}));
        player.stop();
    }

    {
        Player player;
        State state(&player, path);
        player.queue()->set(m_tracks, 0);
        QVERIFY(state.saveNow());
    }
    QVERIFY(State::read(queuePath, object));
    QCOMPARE(object.value(QStringLiteral("order")).toArray(), QJsonArray());
}

void PlayerTest::errorKeepsPaused()
{
    QueueEntry missing;
    missing.path = m_dir.path() + QStringLiteral("/missing-one.flac");
    missing.title = QStringLiteral("missing");
    Player player;
    QSignalSpy started(&player, &Player::trackStarted);
    QSignalSpy errors(&player, &Player::error);

    player.restore({missing, m_tracks.at(0), m_tracks.at(1)}, 0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QVERIFY(errors.count() >= 1);
    QCOMPARE(started.at(0).at(0).toInt(), 1);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QTest::qWait(200);
    QCOMPARE(player.status(), Player::Paused);
    player.stop();
}

void PlayerTest::errorAllRows()
{
    QList<QueueEntry> entries;
    for (int i = 0; i < 3; ++i) {
        QueueEntry missing;
        missing.path = m_dir.path() + QStringLiteral("/missing-%1.flac").arg(i);
        missing.title = QStringLiteral("missing %1").arg(i);
        entries.append(missing);
    }
    Player player;
    QSignalSpy started(&player, &Player::trackStarted);
    QTest::ignoreMessage(QtWarningMsg, "casdap: no playable track");
    player.restore(entries, 0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(started.count(), 0);

    QTest::ignoreMessage(QtWarningMsg, "casdap: no playable track");
    player.queue()->setRepeat(Queue::All);
    player.restore(entries, 2, 0);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(player.queue()->currentIndex(), 2);
}

void PlayerTest::phoneGuard()
{
    auto player = syncPlayer();
    QVERIFY(player->isValid());
    FakeBluezModel fake;
    QVERIFY(fake.applyPreset(QStringLiteral("active")));
    player->setBluez(&fake);
    QVERIFY(player->transportActive());
    fake.clearCommands();
    QSignalSpy requested(player.get(), &Player::localPlayRequested);

    player->play({m_long}, 0);
    QCOMPARE(player->status(), Player::Paused);
    QVERIFY(player->playPending());
    QCOMPARE(requested.count(), 1);
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("pause")}));

    player->playPause();
    QCOMPARE(requested.count(), 1);
    QCOMPARE(player->status(), Player::Paused);

    fake.setTransportActive(false);
    QCOMPARE(player->status(), Player::Playing);
    QVERIFY(!player->playPending());

    fake.setTransportActive(true);
    QCOMPARE(player->status(), Player::Paused);
    player->resume();
    QVERIFY(player->playPending());
    QCOMPARE(fake.commands().size(), 2);
    player->pause();
    QVERIFY(!player->playPending());
    fake.setTransportActive(false);
    QCOMPARE(player->status(), Player::Paused);

    player->playAt(0);
    QCOMPARE(player->status(), Player::Playing);
    QCOMPARE(fake.commands().size(), 2);
    player->stop();
    player->setBluez(nullptr);
}

void PlayerTest::phoneGuardTimeout()
{
    auto player = syncPlayer();
    FakeBluezModel fake;
    fake.setTransportActive(true);
    player->setBluez(&fake);

    QElapsedTimer timer;
    timer.start();
    player->play({m_long}, 0);
    QCOMPARE(player->status(), Player::Paused);
    QTRY_COMPARE_WITH_TIMEOUT(player->status(), Player::Playing, 3000);
    QVERIFY(timer.elapsed() >= 900);
    QVERIFY(fake.transportActive());
    QVERIFY(!player->playPending());
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("pause")}));
    player->stop();
    player->setBluez(nullptr);
}

void PlayerTest::handoffWaitsForTransport()
{
    auto player = syncPlayer();
    player->setTransportReady(false);
    player->restore({m_long}, 0, 0, true);
    QCOMPARE(player->status(), Player::Paused);
    player->setTransportReady(true);
    QCOMPARE(player->status(), Player::Playing);
    player->stop();

    player->setTransportReady(false);
    player->restore({m_long}, 0, 0, true);
    QCOMPARE(player->status(), Player::Paused);
    player->setTransportActive(true);
    player->setTransportReady(true);
    QCOMPARE(player->status(), Player::Paused);
    QVERIFY(!player->playPending());

    player->restore({m_long}, 0, 0, true);
    QCOMPARE(player->status(), Player::Paused);
    player->setTransportActive(false);
    QCOMPARE(player->status(), Player::Paused);
    player->stop();

    const QString path = m_dir.path() + QStringLiteral("/guard/state.json");
    {
        auto source = syncPlayer();
        State state(source.get(), path);
        source->play({m_long}, 0);
        QCOMPARE(source->status(), Player::Playing);
        QVERIFY(state.saveExit());
        source->stop();
    }
    auto resumed = syncPlayer();
    FakeBluezModel fake;
    fake.setTransportActive(true);
    resumed->setBluez(&fake);
    State state(resumed.get(), path);
    QVERIFY(state.load());
    QCOMPARE(resumed->status(), Player::Paused);
    QVERIFY(fake.commands().isEmpty());
    resumed->stop();
    resumed->setBluez(nullptr);
}

void PlayerTest::queueSignals()
{
    Queue queue;
    PlayOrderModel *order = queue.playOrder();
    QSignalSpy resets(&queue, &QAbstractItemModel::modelReset);
    QSignalSpy orderResets(order, &QAbstractItemModel::modelReset);
    QSignalSpy orderSpy(&queue, &Queue::orderChanged);
    QSignalSpy slotSpy(&queue, &Queue::currentSlotChanged);
    QSignalSpy indexSpy(&queue, &Queue::indexChanged);

    queue.set(namedEntries(6), 1);
    QCOMPARE(resets.count(), 1);
    QCOMPARE(orderResets.count(), 1);
    QCOMPARE(orderSpy.count(), 0);
    QCOMPARE(indexSpy.count(), 1);
    QCOMPARE(slotSpy.count(), 1);
    QCOMPARE(order->count(), 6);
    QVERIFY(consistent(queue));
    const QHash<int, QByteArray> names = order->roleNames();
    for (const char *role : {"row", "slot", "number", "id", "title", "artist", "album", "durationMs", "isCurrent"})
        QVERIFY(names.values().contains(QByteArray(role)));

    orderSpy.clear();
    indexSpy.clear();
    slotSpy.clear();
    QSignalSpy data(order, &QAbstractItemModel::dataChanged);
    QSignalSpy queueData(&queue, &QAbstractItemModel::dataChanged);
    queue.setCurrentIndex(3);
    QCOMPARE(orderSpy.count(), 0);
    QCOMPARE(indexSpy.count(), 1);
    QCOMPARE(slotSpy.count(), 1);
    QCOMPARE(data.count(), 2);
    QCOMPARE(queueData.count(), 2);
    QCOMPARE(resets.count(), 1);
    QVERIFY(order->data(order->index(3), PlayOrderModel::IsCurrentRole).toBool());
    QVERIFY(!order->data(order->index(1), PlayOrderModel::IsCurrentRole).toBool());

    queue.setShuffle(true);
    QCOMPARE(orderSpy.count(), 1);
    QCOMPARE(orderResets.count(), 2);
    QCOMPARE(queue.currentSlot(), 0);
    QVERIFY(consistent(queue));

    QSignalSpy inserted(order, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(order, &QAbstractItemModel::rowsRemoved);
    QSignalSpy moved(order, &QAbstractItemModel::rowsMoved);
    queue.enqueue(namedEntries(1));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(orderResets.count(), 2);
    QVERIFY(queue.slotOf(6) > queue.currentSlot());
    QVERIFY(consistent(queue));

    queue.remove(queue.rowAt(4));
    QCOMPARE(removed.count(), 1);
    QCOMPARE(queue.count(), 6);
    QVERIFY(consistent(queue));

    queue.moveSlot(2, 5);
    QCOMPARE(moved.count(), 1);
    QVERIFY(consistent(queue));

    const int current = queue.currentIndex();
    queue.playNext(namedEntries(2));
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(queue.rowAt(queue.currentSlot() + 1), current + 1);
    QCOMPARE(queue.rowAt(queue.currentSlot() + 2), current + 2);
    QVERIFY(consistent(queue));

    queue.playNextRow(queue.rowAt(6));
    QCOMPARE(moved.count(), 2);
    QVERIFY(consistent(queue));

    queue.setShuffle(false);
    QVERIFY(consistent(queue));
    queue.enqueue(namedEntries(2));
    QCOMPARE(inserted.count(), 3);
    QVERIFY(consistent(queue));
    queue.move(0, 4);
    QCOMPARE(moved.count(), 3);
    QVERIFY(consistent(queue));
    queue.remove(queue.currentIndex());
    QCOMPARE(removed.count(), 2);
    QVERIFY(consistent(queue));
    QCOMPARE(resets.count(), 1);
}

void PlayerTest::addStoppedShuffle()
{
    Player player(m_library.get());
    player.queue()->setShuffle(true);
    player.restore(m_tracks, 1, 0);
    player.stop();
    QCOMPARE(player.status(), Player::Stopped);

    QCOMPARE(player.enqueueTracks({QVariant(m_tracks.at(0).id), QVariant(m_tracks.at(2).id)}), 2);
    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.queue()->currentSlot(), 0);
    QVERIFY(player.queue()->shuffle());
    QCOMPARE(player.queue()->order().size(), 2);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.position(), qint64(0));
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(0).id);

    player.stop();
    QCOMPARE(player.playNextTracks({QVariant(m_tracks.at(1).id)}), 1);
    QCOMPARE(player.queue()->count(), 1);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);
    player.stop();
}

void PlayerTest::playSource()
{
    const qint64 albumId = scalar(m_library->database(), QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"),
                                  {m_tracks.at(0).id})
                               .toLongLong();
    QVERIFY(albumId > 0);
    const int albumSize = static_cast<int>(m_library->albumTracks(albumId).size());
    QVERIFY(albumSize >= 3);

    Player player(m_library.get());
    QSignalSpy added(&player, &Player::added);
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("play"), m_tracks.at(1).id),
             albumSize);
    QCOMPARE(player.queue()->count(), albumSize);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);
    QCOMPARE(player.status(), Player::Playing);
    QCOMPARE(added.count(), 0);

    player.pause();
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("next")), albumSize);
    QCOMPARE(player.queue()->count(), 2 * albumSize);
    QCOMPARE(added.count(), 1);
    QCOMPARE(added.at(0).at(0).toInt(), albumSize);
    QCOMPARE(player.status(), Player::Paused);

    QCOMPARE(player.playSource(QStringLiteral("song"), m_tracks.at(2).id, QStringLiteral("add")), 1);
    QCOMPARE(player.queue()->count(), 2 * albumSize + 1);
    QCOMPARE(player.queue()->entry(2 * albumSize).id, m_tracks.at(2).id);
    QCOMPARE(added.count(), 2);

    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("shuffle")), albumSize);
    QVERIFY(player.queue()->shuffle());
    QCOMPARE(player.queue()->count(), albumSize);
    QCOMPARE(player.queue()->currentSlot(), 0);
    QCOMPARE(player.status(), Player::Playing);

    player.stop();
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("add")), albumSize);
    QCOMPARE(player.queue()->count(), albumSize);
    QCOMPARE(player.status(), Player::Paused);

    QTest::ignoreMessage(QtWarningMsg, "casdap player: unknown source mode \"jump\"");
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("jump")), 0);
    QCOMPARE(player.playSource(QStringLiteral("album"), 999999, QStringLiteral("play")), 0);
    QCOMPARE(player.queue()->count(), albumSize);
    player.stop();

    Player noLibrary;
    QCOMPARE(noLibrary.playSource(QStringLiteral("album"), albumId, QStringLiteral("play")), 0);
}

void PlayerTest::stateQueueWorker()
{
    const QString path = m_dir.path() + QStringLiteral("/worker/state.json");
    Player player;
    State state(&player, path);
    QCOMPARE(state.queuePath(), m_dir.path() + QStringLiteral("/worker/queue.json"));
    player.queue()->set(namedEntries(20), 3);
    const qint64 first = state.queueRev();
    QVERIFY(first > 0);
    QTest::qWait(500);
    QVERIFY(!QFileInfo::exists(state.queuePath()));
    QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(state.queuePath()), 3000);
    QTRY_COMPARE_WITH_TIMEOUT(state.writtenQueueRev(), first, 3000);
    QJsonObject object = readJson(state.queuePath());
    QCOMPARE(object.value(QStringLiteral("rev")).toInteger(), first);
    QCOMPARE(object.value(QStringLiteral("index")).toInt(), 3);
    QCOMPARE(static_cast<int>(object.value(QStringLiteral("ids")).toArray().size()), 20);
    QCOMPARE(object.value(QStringLiteral("paths")).toArray().at(4).toString(), QStringLiteral("/nonexistent/e4.flac"));

    player.queue()->setCurrentIndex(5);
    QCOMPARE(state.queueRev(), first);

    player.queue()->enqueue(namedEntries(2));
    player.queue()->enqueue(namedEntries(1));
    const qint64 second = state.queueRev();
    QVERIFY(second > first);
    QTRY_COMPARE_WITH_TIMEOUT(state.writtenQueueRev(), second, 3000);
    QCOMPARE(static_cast<int>(readJson(state.queuePath()).value(QStringLiteral("ids")).toArray().size()), 23);

    player.queue()->remove(0);
    QVERIFY(state.queueRev() > second);
    QVERIFY(state.saveNow());
    QCOMPARE(state.writtenQueueRev(), state.queueRev());
    QCOMPARE(static_cast<int>(readJson(state.queuePath()).value(QStringLiteral("ids")).toArray().size()), 22);
    const QJsonObject small = readJson(path);
    QCOMPARE(small.value(QStringLiteral("queueRev")).toInteger(), state.queueRev());
    QVERIFY(!small.contains(QStringLiteral("queue")));
    QVERIFY(!small.contains(QStringLiteral("order")));
    QTest::qWait(1300);
    QCOMPARE(state.writtenQueueRev(), state.queueRev());
}

void PlayerTest::stateMigration()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate/state.json");
    QJsonArray items;
    for (const QueueEntry &entry : m_tracks)
        items.append(QJsonObject::fromVariantMap(entry.toMap()));
    QJsonObject legacy;
    legacy.insert(QStringLiteral("queue"), items);
    legacy.insert(QStringLiteral("index"), 2);
    legacy.insert(QStringLiteral("position"), 0);
    legacy.insert(QStringLiteral("shuffle"), false);
    legacy.insert(QStringLiteral("order"), QJsonArray());
    legacy.insert(QStringLiteral("repeat"), 1);
    legacy.insert(QStringLiteral("status"), QStringLiteral("paused"));
    legacy.insert(QStringLiteral("savedAt"), QDateTime::currentSecsSinceEpoch());
    legacy.insert(QStringLiteral("handoff"), false);
    legacy.insert(QStringLiteral("stack"), QJsonArray({QJsonObject{{QStringLiteral("name"), QStringLiteral("home")},
                                                                   {QStringLiteral("id"), -1}}}));
    QVERIFY(State::write(path, legacy));
    const QString queuePath = State::queuePathFor(path);
    QVERIFY(!QFileInfo::exists(queuePath));

    {
        Player player(m_library.get());
        State state(&player, path);
        QVERIFY(state.load());
        QCOMPARE(player.queue()->count(), 3);
        QCOMPARE(player.queue()->currentIndex(), 2);
        QCOMPARE(player.queue()->repeat(), Queue::All);
        QCOMPARE(player.queue()->entry(2).title, m_tracks.at(2).title);
        QCOMPARE(static_cast<int>(state.stack().size()), 1);
        QVERIFY(QFileInfo::exists(queuePath));
        player.stop();
    }
    const QJsonObject small = readJson(path);
    QVERIFY(!small.contains(QStringLiteral("queue")));
    QCOMPARE(small.value(QStringLiteral("index")).toInt(), 2);
    QCOMPARE(static_cast<int>(small.value(QStringLiteral("stack")).toArray().size()), 1);
    const QJsonObject queueFile = readJson(queuePath);
    QCOMPARE(small.value(QStringLiteral("queueRev")).toInteger(), queueFile.value(QStringLiteral("rev")).toInteger());
    QCOMPARE(static_cast<int>(queueFile.value(QStringLiteral("paths")).toArray().size()), 3);

    Player player(m_library.get());
    State state(&player, path);
    QVERIFY(state.load());
    QCOMPARE(player.queue()->count(), 3);
    QCOMPARE(player.queue()->currentIndex(), 2);
    QCOMPARE(player.queue()->entry(1).id, m_tracks.at(1).id);
    QCOMPARE(state.queueRev(), queueFile.value(QStringLiteral("rev")).toInteger());
    player.stop();
}

void PlayerTest::stateRevMismatch()
{
    const QString path = m_dir.path() + QStringLiteral("/rev/state.json");
    qint64 rev = 0;
    {
        Player player;
        State state(&player, path);
        player.queue()->set(m_tracks, 2);
        QVERIFY(state.saveNow());
        rev = state.queueRev();
    }
    QJsonObject object = readJson(path);
    object.insert(QStringLiteral("index"), 1);
    object.insert(QStringLiteral("position"), 1500);
    object.insert(QStringLiteral("status"), QStringLiteral("playing"));
    object.insert(QStringLiteral("handoff"), true);
    object.insert(QStringLiteral("queueRev"), rev + 5);
    QVERIFY(State::write(path, object));
    {
        Player player;
        State state(&player, path);
        QVERIFY(state.load());
        QCOMPARE(player.queue()->currentIndex(), 2);
        QCOMPARE(player.position(), qint64(0));
        QCOMPARE(player.status(), Player::Paused);
        QCOMPARE(state.queueRev(), rev);
        player.stop();
    }

    object.insert(QStringLiteral("queueRev"), rev);
    object.insert(QStringLiteral("handoff"), false);
    QVERIFY(State::write(path, object));
    {
        Player player;
        State state(&player, path);
        QVERIFY(state.load());
        QCOMPARE(player.queue()->currentIndex(), 1);
        QCOMPARE(player.position(), qint64(1500));
        QCOMPARE(player.status(), Player::Paused);
        player.stop();
    }

    QVERIFY(QFile::remove(path));
    {
        Player player;
        State state(&player, path);
        QVERIFY(state.load());
        QCOMPARE(player.queue()->currentIndex(), 2);
        QCOMPARE(player.status(), Player::Paused);
        player.stop();
    }
}

void PlayerTest::stateRestoreByPath()
{
    const QString path = m_dir.path() + QStringLiteral("/bypath/state.json");
    const QString lost = QStringLiteral("/nonexistent/Lost Song.flac");
    QJsonObject queueFile;
    queueFile.insert(QStringLiteral("rev"), 7);
    queueFile.insert(QStringLiteral("index"), 0);
    queueFile.insert(QStringLiteral("ids"), QJsonArray({999999, m_tracks.at(1).id, m_tracks.at(1).id, 424242}));
    queueFile.insert(QStringLiteral("paths"),
                     QJsonArray({m_tracks.at(0).path, m_tracks.at(1).path, m_tracks.at(2).path, lost}));
    queueFile.insert(QStringLiteral("order"), QJsonArray());
    QVERIFY(State::write(State::queuePathFor(path), queueFile));
    QJsonObject small;
    small.insert(QStringLiteral("index"), 0);
    small.insert(QStringLiteral("queueRev"), 7);
    QVERIFY(State::write(path, small));

    Player player(m_library.get());
    State state(&player, path);
    QVERIFY(state.load());
    QCOMPARE(player.queue()->count(), 4);
    QCOMPARE(player.queue()->entry(0).id, m_tracks.at(0).id);
    QCOMPARE(player.queue()->entry(0).title, m_tracks.at(0).title);
    QCOMPARE(player.queue()->entry(1).id, m_tracks.at(1).id);
    QCOMPARE(player.queue()->entry(2).id, m_tracks.at(2).id);
    QCOMPARE(player.queue()->entry(2).album, m_tracks.at(2).album);
    QCOMPARE(player.queue()->entry(3).id, qint64(0));
    QCOMPARE(player.queue()->entry(3).path, lost);
    QCOMPARE(player.queue()->entry(3).title, QStringLiteral("Lost Song"));
    QCOMPARE(state.queueRev(), qint64(7));
    player.stop();
}

QTEST_GUILESS_MAIN(PlayerTest)
#include "tst_player.moc"
