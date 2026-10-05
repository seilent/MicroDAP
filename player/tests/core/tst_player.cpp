#include "Library.h"
#include "Player.h"
#include "Queue.h"
#include "State.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

#include <algorithm>
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

private:
    int historyCount() const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
    QList<QueueEntry> m_tracks;
};

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
    QCOMPARE(static_cast<int>(object.value(QStringLiteral("queue")).toArray().size()), 3);

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

QTEST_GUILESS_MAIN(PlayerTest)
#include "tst_player.moc"
