#include "Player.h"
#include "Queue.h"
#include "State.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

#include <algorithm>
#include <functional>
#include <memory>

using namespace casdap;

namespace {

constexpr int queueSize = 8000;
constexpr int runs = 5;

QList<QueueEntry> syntheticEntries(int n)
{
    QList<QueueEntry> entries;
    entries.reserve(n);
    for (int i = 0; i < n; ++i) {
        QueueEntry entry;
        entry.id = i + 1;
        entry.path = QStringLiteral("/nonexistent/scale/album %1/%2 track %3.flac").arg(i / 7).arg(i % 7 + 1).arg(i);
        entry.title = QStringLiteral("track %1").arg(i);
        entry.artist = QStringLiteral("artist %1").arg(i / 7);
        entry.album = QStringLiteral("album %1").arg(i / 7);
        entry.durationMs = 180000 + i;
        entries.append(entry);
    }
    return entries;
}

double medianMs(const std::function<void()> &run, const std::function<void()> &prepare = {})
{
    QList<double> samples;
    for (int i = 0; i < runs; ++i) {
        if (prepare)
            prepare();
        QElapsedTimer timer;
        timer.start();
        run();
        samples.append(timer.nsecsElapsed() / 1e6);
    }
    std::sort(samples.begin(), samples.end());
    return samples.at(runs / 2);
}

void report(const char *name, double ms, double budget)
{
    qInfo().noquote() << QStringLiteral("casdap perf: %1 %2 ms (budget %3)").arg(QLatin1String(name)).arg(ms, 0, 'f', 2).arg(budget);
}

}

class ScaleQueueTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void queueSet();
    void playEntries();
    void indexMove();
    void trackChange();
    void stateSave();
    void queueWrite();
    void restore();

private:
    QTemporaryDir m_dir;
    QList<QueueEntry> m_entries;
};

void ScaleQueueTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));
    QVERIFY(m_dir.isValid());
    m_entries = syntheticEntries(queueSize);
}

void ScaleQueueTest::queueSet()
{
    Player player;
    State state(&player, m_dir.path() + QStringLiteral("/set/state.json"));
    Queue *queue = player.queue();
    QSignalSpy resets(queue, &QAbstractItemModel::modelReset);
    QSignalSpy orderResets(queue->playOrder(), &QAbstractItemModel::modelReset);
    QSignalSpy order(queue, &Queue::orderChanged);
    int start = 0;
    const double ms = medianMs([&] { queue->set(m_entries, start); }, [&] { start = (start + 997) % queueSize; });
    report("queue set 8000", ms, 60);
    QCOMPARE(resets.count(), runs);
    QCOMPARE(orderResets.count(), runs);
    QCOMPARE(order.count(), 0);
    QCOMPARE(queue->playOrder()->count(), queueSize);
    QVERIFY(ms < 60);

    queue->setShuffle(true);
    QCOMPARE(queue->currentSlot(), 0);
    QSignalSpy inserted(queue->playOrder(), &QAbstractItemModel::rowsInserted);
    QSignalSpy shuffleResets(queue->playOrder(), &QAbstractItemModel::modelReset);
    queue->enqueue(syntheticEntries(1));
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(shuffleResets.count(), 0);
}

void ScaleQueueTest::playEntries()
{
    Player player;
    State state(&player, m_dir.path() + QStringLiteral("/play/state.json"));
    const double ms = medianMs([&] {
        player.play(m_entries, 0);
        player.stop();
    });
    report("play 8000 through startAt", ms, 60);
    QVERIFY(ms < 60);
}

void ScaleQueueTest::indexMove()
{
    Player player;
    State state(&player, m_dir.path() + QStringLiteral("/index/state.json"));
    Queue *queue = player.queue();
    queue->set(m_entries, 0);
    queue->setShuffle(true);
    QSignalSpy order(queue, &Queue::orderChanged);
    QSignalSpy resets(queue->playOrder(), &QAbstractItemModel::modelReset);
    const qint64 rev = state.queueRev();
    int slot = 0;
    const double ms = medianMs([&] { queue->setCurrentIndex(queue->rowAt(slot)); }, [&] { slot += 37; });
    report("setCurrentIndex 8000", ms, 5);
    QCOMPARE(order.count(), 0);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(state.queueRev(), rev);
    QVERIFY(ms < 5);
}

void ScaleQueueTest::trackChange()
{
    Player player;
    State state(&player, m_dir.path() + QStringLiteral("/track/state.json"));
    Queue *queue = player.queue();
    queue->set(m_entries, 0);
    int reads = 0;
    connect(&player, &Player::currentChanged, this, [&] {
        const QVariantMap map = player.current();
        reads += map.isEmpty() ? 0 : 1;
        const QVariantList upcoming = queue->upcoming(3);
        reads += static_cast<int>(upcoming.size());
    });
    connect(queue, &Queue::currentSlotChanged, this, [&] { reads += queue->currentSlot() >= 0 ? 1 : 0; });
    int row = 0;
    const double ms = medianMs([&] { queue->setCurrentIndex(row); }, [&] { row += 101; });
    report("track change handlers 8000", ms, 8);
    QVERIFY(reads > 0);
    QVERIFY(ms < 8);
}

void ScaleQueueTest::stateSave()
{
    Player player;
    const QString path = m_dir.path() + QStringLiteral("/save/state.json");
    State state(&player, path);
    player.queue()->set(m_entries, 10);
    QVERIFY(state.flushQueue());
    const double ms = medianMs([&] { QVERIFY(state.saveState(false)); });
    report("state.json save", ms, 2);
    QVERIFY(QFileInfo(path).size() < 4096);
    QVERIFY(ms < 2);
}

void ScaleQueueTest::queueWrite()
{
    Player player;
    State state(&player, m_dir.path() + QStringLiteral("/queue/state.json"));
    player.queue()->set(m_entries, 10);
    QueueSnapshot snapshot = state.queueSnapshot();
    snapshot.path = m_dir.path() + QStringLiteral("/queue/direct.json");
    const double worker = medianMs([&] { QVERIFY(State::writeQueue(snapshot)); });
    report("queue.json 8000 worker", worker, 80);
    QVERIFY(worker < 80);

    int step = 0;
    const double gui = medianMs([&] { state.writeQueueLater(); }, [&] {
        QTRY_VERIFY_WITH_TIMEOUT(step == 0 || state.writtenQueueRev() == state.queueRev(), 5000);
        player.queue()->setCurrentIndex(++step);
        player.queue()->moveSlot(0, 1);
    });
    report("queue.json 8000 gui part", gui, 1);
    QTRY_COMPARE_WITH_TIMEOUT(state.writtenQueueRev(), state.queueRev(), 5000);
    QJsonObject object;
    QVERIFY(State::read(state.queuePath(), object));
    QCOMPARE(static_cast<int>(object.value(QStringLiteral("ids")).toArray().size()), queueSize);
    QVERIFY(gui < 1);
}

void ScaleQueueTest::restore()
{
    const QString path = m_dir.path() + QStringLiteral("/restore/state.json");
    {
        Player player;
        State state(&player, path);
        player.queue()->set(m_entries, 4000);
        QVERIFY(state.saveNow());
    }
    std::unique_ptr<Player> player;
    std::unique_ptr<State> state;
    const double ms = medianMs(
        [&] { QVERIFY(state->load()); },
        [&] {
            if (player) {
                QCOMPARE(player->queue()->count(), queueSize);
                QCOMPARE(player->queue()->currentIndex(), 4000);
                player->stop();
            }
            state.reset();
            player = std::make_unique<Player>();
            state = std::make_unique<State>(player.get(), path);
        });
    QCOMPARE(player->queue()->count(), queueSize);
    player->stop();
    report("restore queue.json 8000", ms, 120);
    QVERIFY(ms < 120);
}

QTEST_GUILESS_MAIN(ScaleQueueTest)
#include "tst_scale_queue.moc"
