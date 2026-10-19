#include "Crossfade.h"
#include "Deck.h"
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
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <gst/gst.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <ctime>
#include <initializer_list>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>
#include <vector>

using namespace microdap;

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

bool encodeSine(const QString &path, int seconds, int rate = 44100)
{
    const QString description =
        QStringLiteral("audiotestsrc num-buffers=%1 samplesperbuffer=%2 wave=sine freq=440 "
                       "! audio/x-raw,rate=%3 ! audioconvert ! flacenc ! filesink location=\"%4\"")
            .arg(seconds * 100)
            .arg(rate / 100)
            .arg(rate)
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
    void playSourceShuffleOn();
    void stateQueueWorker();
    void stateMigration();
    void stateRevMismatch();
    void stateRestoreByPath();
    void sinkDescriptionText();
    void chainWithoutPolicy();
    void chainRoundTrip();
    void rateTail();
    void bitPerfectProperties();
    void pendingLatestWins();
    void pendingQueueEdits();
    void pendingPauseParks();
    void resumeFromChain();
    void rebuildAtKeeps();
    void seekWhileParked();
    void exclusiveErrors();
    void pwSinkError();
    void formats();
    void exclusiveSwallowsAfterError();
    void sameListenRebuild();
    void playingBuildAtPosition();
    void badSinkConfigure();
    void guardBeforeAdmit();
    void rateTailNoWarnings();
    void resumeSeeksBeforeAudio();
    void gaplessPerChain();
    void gaplessRealTime();
    void cueProbeUntouched();
    void replayGainPerTrack();
    void replayGainWhilePaused();
    void crossfadeOverlap();
    void crossfadeRateChangeGapless();
    void crossfadeAlbumGapless();
    void crossfadeSkipDropsOutgoing();
    void crossfadeSeekBeforeOverlap();
    void crossfadeSeekInOverlap();
    void crossfadeEarlyEnd();
    void crossfadeStalePlan();
    void crossfadeOffsetAfterJoin();
    void crossfadeErrorWhileMixing();
    void dsdRoutes();
    void dsdBoundary();
    void deckMissing();
    void dsd128Refused();
    void outputLostPauses();

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
    const QByteArray sink = qgetenv("MICRODAP_AUDIO_SINK");
    qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    auto player = std::make_unique<Player>(library);
    qputenv("MICRODAP_AUDIO_SINK", sink);
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

qint64 bootSeconds()
{
    timespec now{};
    clock_gettime(CLOCK_BOOTTIME, &now);
    return now.tv_sec;
}

QJsonObject readJson(const QString &path)
{
    QJsonObject object;
    State::read(path, object);
    return object;
}

using Chain = Player::OutputChain;

class FakePolicy : public OutputPolicy
{
public:
    Chain chainFor(GstState) const override { return want; }
    Admit admit(const Player::PendingBuild &build) override
    {
        admitted.append(build);
        return {now, chain};
    }
    QString exclusiveSink() const override { return QStringLiteral("fakesink sync=false"); }
    QString exclusiveCard() const override { return card; }
    QString bitPerfectSink() const override { return bitPerfect; }
    int fixedRate() const override { return rate; }
    void outputError(int code) override { errors.append(code); }

    Chain want = Chain::Pw;
    Chain chain = Chain::Pw;
    bool now = true;
    int rate = 0;
    QString card = QStringLiteral("B");
    QString bitPerfect;
    QList<Player::PendingBuild> admitted;
    QList<int> errors;
};

std::unique_ptr<Player> withEnv(const char *name, const QByteArray &value)
{
    const bool set = qEnvironmentVariableIsSet(name);
    const QByteArray before = qgetenv(name);
    qputenv(name, value);
    auto player = std::make_unique<Player>();
    if (set)
        qputenv(name, before);
    else
        qunsetenv(name);
    return player;
}

std::unique_ptr<Player> exclusivePlayer(
    const QByteArray &description = QByteArrayLiteral("fakesink sync=false name=microdap-ex"))
{
    return withEnv("MICRODAP_EXCLUSIVE_SINK", description);
}

QAtomicInt glibWarnings;

void countWarnings(const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer)
{
    if (level & (G_LOG_LEVEL_WARNING | G_LOG_LEVEL_CRITICAL)) {
        glibWarnings.fetchAndAddOrdered(1);
        qInfo() << "glib" << domain << message;
    }
    g_log_default_handler(domain, level, message, nullptr);
}

GstElement *pipelineOf(GstElement *element)
{
    GstObject *top = GST_OBJECT(gst_object_ref(element));
    while (GstObject *parent = gst_object_get_parent(top)) {
        gst_object_unref(top);
        top = parent;
    }
    return GST_ELEMENT(top);
}

GstElement *tailChild(Player &player, const char *name)
{
    GstElement *top = pipelineOf(player.audioSink());
    GstElement *tail = gst_bin_get_by_name(GST_BIN(top), "tail");
    gst_object_unref(top);
    if (!tail)
        return nullptr;
    GstElement *element = gst_bin_get_by_name(GST_BIN(tail), name);
    gst_object_unref(tail);
    if (element)
        gst_object_unref(element);
    return element;
}

GstElement *builtFilter(Player &player)
{
    return tailChild(player, "filter");
}

GstElement *builtSink(Player &player)
{
    return tailChild(player, "sink");
}

struct Capture
{
    std::mutex lock;
    std::map<GstClockTime, QByteArray> buffers;

    void add(GstBuffer *buffer)
    {
        if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer))
            return;
        GstMapInfo map;
        if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
            return;
        const QByteArray bytes(reinterpret_cast<const char *>(map.data), qsizetype(map.size));
        gst_buffer_unmap(buffer, &map);
        std::lock_guard guard(lock);
        buffers.emplace(GST_BUFFER_PTS(buffer), bytes);
    }

    std::size_t size()
    {
        std::lock_guard guard(lock);
        return buffers.size();
    }
};

GstPadProbeReturn captureProbe(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    static_cast<Capture *>(data)->add(GST_PAD_PROBE_INFO_BUFFER(info));
    return GST_PAD_PROBE_OK;
}

float peakOf(GstBuffer *buffer)
{
    GstMapInfo map;
    if (!buffer || !gst_buffer_map(buffer, &map, GST_MAP_READ))
        return 0.0f;
    float peak = 0.0f;
    const auto *samples = reinterpret_cast<const float *>(map.data);
    for (gsize i = 0; i < map.size / sizeof(float); ++i)
        peak = std::max(peak, std::abs(samples[i]));
    gst_buffer_unmap(buffer, &map);
    return peak;
}

struct Levels
{
    std::mutex lock;
    std::vector<std::pair<int, float>> peaks;
    int item = -1;

    void add(int at, float peak)
    {
        std::lock_guard guard(lock);
        peaks.emplace_back(at, peak);
    }
};

GstPadProbeReturn levelProbe(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *levels = static_cast<Levels *>(data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM) {
        if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) == GST_EVENT_STREAM_START) {
            std::lock_guard guard(levels->lock);
            ++levels->item;
        }
        return GST_PAD_PROBE_OK;
    }
    const float peak = peakOf(GST_PAD_PROBE_INFO_BUFFER(info));
    std::lock_guard guard(levels->lock);
    levels->peaks.emplace_back(levels->item, peak);
    return GST_PAD_PROBE_OK;
}

GstElement *deckVolume(Player &player)
{
    GstElement *top = pipelineOf(player.audioSink());
    GstElement *volume = gst_bin_get_by_name(GST_BIN(top), "volume");
    gst_object_unref(top);
    if (volume)
        gst_object_unref(volume);
    return volume;
}

double volumeOf(GstElement *volume)
{
    gdouble value = 0;
    g_object_get(volume, "volume", &value, nullptr);
    return value;
}

struct MixerWatch
{
    std::atomic<int> added{0};
    std::atomic<int> removed{0};
};

void mixerAdded(GstBin *, GstElement *element, gpointer data)
{
    if (g_strcmp0(GST_OBJECT_NAME(element), Crossfade::mixerName) == 0)
        ++static_cast<MixerWatch *>(data)->added;
}

void mixerRemoved(GstBin *, GstElement *element, gpointer data)
{
    if (g_strcmp0(GST_OBJECT_NAME(element), Crossfade::mixerName) == 0)
        ++static_cast<MixerWatch *>(data)->removed;
}

void watchMixer(Player &player, MixerWatch &watch)
{
    GstElement *top = pipelineOf(player.audioSink());
    g_signal_connect(top, "element-added", G_CALLBACK(mixerAdded), &watch);
    g_signal_connect(top, "element-removed", G_CALLBACK(mixerRemoved), &watch);
    gst_object_unref(top);
}

int childCount(Player &player)
{
    GstElement *top = pipelineOf(player.audioSink());
    const int n = GST_BIN_NUMCHILDREN(GST_BIN(top));
    gst_object_unref(top);
    return n;
}

QueueEntry sineEntry(const QString &path, qint64 id, int seconds)
{
    QueueEntry entry;
    entry.id = id;
    entry.path = path;
    entry.title = QFileInfo(path).completeBaseName();
    entry.durationMs = seconds * 1000;
    return entry;
}

QStringList factories(GstElement *bin, bool recurse)
{
    QStringList names;
    GstIterator *it = recurse ? gst_bin_iterate_recurse(GST_BIN(bin)) : gst_bin_iterate_elements(GST_BIN(bin));
    GValue item = G_VALUE_INIT;
    while (gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
        auto *element = GST_ELEMENT(g_value_get_object(&item));
        if (GstElementFactory *factory = gst_element_get_factory(element))
            names.append(QString::fromUtf8(GST_OBJECT_NAME(factory)));
        g_value_reset(&item);
    }
    g_value_unset(&item);
    gst_iterator_free(it);
    std::sort(names.begin(), names.end());
    return names;
}

bool prerolled(Player &player)
{
    QElapsedTimer timer;
    timer.start();
    while ((player.sinkFormat().isEmpty() || player.sourceFormat().isEmpty()) && timer.elapsed() < playTimeoutMs)
        QTest::qWait(20);
    if (player.sinkFormat().isEmpty() || player.sourceFormat().isEmpty() || !player.audioSink())
        return false;
    GstElement *top = pipelineOf(player.audioSink());
    GstState state = GST_STATE_VOID_PENDING;
    const GstStateChangeReturn ret = gst_element_get_state(top, &state, nullptr, 5 * GST_SECOND);
    gst_object_unref(top);
    return ret == GST_STATE_CHANGE_SUCCESS && state == GST_STATE_PAUSED;
}

void postSinkError(Player &player, GstResourceError code)
{
    GstIterator *it = gst_bin_iterate_sinks(GST_BIN(player.audioSink()));
    GValue item = G_VALUE_INIT;
    GstElement *child = nullptr;
    if (gst_iterator_next(it, &item) == GST_ITERATOR_OK)
        child = GST_ELEMENT(g_value_dup_object(&item));
    g_value_unset(&item);
    gst_iterator_free(it);
    QVERIFY(child);
    GError *error = g_error_new_literal(GST_RESOURCE_ERROR, code, "The device has been disconnected.");
    gst_element_post_message(child, gst_message_new_error(GST_OBJECT(child), error, "test"));
    g_error_free(error);
    gst_object_unref(child);
}

bool reachesPlaying(Player &player)
{
    GstElement *top = pipelineOf(player.audioSink());
    QElapsedTimer timer;
    timer.start();
    GstState state = GST_STATE_VOID_PENDING;
    while (timer.elapsed() < playTimeoutMs) {
        gst_element_get_state(top, &state, nullptr, 0);
        if (state == GST_STATE_PLAYING)
            break;
        QTest::qWait(10);
    }
    gst_object_unref(top);
    return state == GST_STATE_PLAYING;
}


bool claimSeconds(const QString &path, int seconds, int rate = 44100)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    QByteArray head = file.read(26);
    if (head.size() != 26 || !head.startsWith("fLaC") || (quint8(head.at(4)) & 0x7f) != 0)
        return false;
    const quint64 total = quint64(seconds) * quint64(rate);
    quint8 *info = reinterpret_cast<quint8 *>(head.data()) + 8;
    info[13] = quint8((info[13] & 0xf0) | ((total >> 32) & 0x0f));
    info[14] = quint8(total >> 24);
    info[15] = quint8(total >> 16);
    info[16] = quint8(total >> 8);
    info[17] = quint8(total);
    return file.seek(0) && file.write(head) == head.size();
}

GstElement *pipelineChild(Player &player, const char *name)
{
    GstElement *top = pipelineOf(player.audioSink());
    GstElement *child = gst_bin_get_by_name(GST_BIN(top), name);
    gst_object_unref(top);
    return child;
}
}

int PlayerTest::historyCount() const
{
    return scalar(m_library->database(), QStringLiteral("SELECT COUNT(*) FROM history")).toInt();
}

void PlayerTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("MICRODAP_AUDIO_SINK"))
        qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));

    QVERIFY(m_dir.isValid());
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC), m_music));

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

    const QByteArray sink = qgetenv("MICRODAP_AUDIO_SINK");
    qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    Player player;
    qputenv("MICRODAP_AUDIO_SINK", sink);
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
    QVERIFY(State::defaultPath().endsWith(QStringLiteral("/microdap/state.json")));
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
    QVERIFY(!aged.value(QStringLiteral("bootId")).toString().isEmpty());
    QJsonObject fresh = aged;
    fresh.insert(QStringLiteral("savedAt"), double(bootSeconds() - 5));
    QVERIFY(State::write(path, fresh));
    QCOMPARE(restored(), Player::Playing);
    QJsonObject otherBoot = fresh;
    otherBoot.insert(QStringLiteral("bootId"), QStringLiteral("00000000-0000-0000-0000-000000000000"));
    QVERIFY(State::write(path, otherBoot));
    QCOMPARE(restored(), Player::Paused);
    aged.insert(QStringLiteral("savedAt"), double(bootSeconds() - 61));
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

void PlayerTest::resumeSeeksBeforeAudio()
{
    QueueEntry entry;
    entry.path = m_dir.path() + QStringLiteral("/resume.flac");
    entry.title = QStringLiteral("Resume");
    entry.durationMs = 100000;
    QVERIFY(encodeSine(entry.path, 100));
    auto player = withEnv("MICRODAP_AUDIO_SINK",
                          QByteArrayLiteral("appsink name=microdap-resume sync=false caps=audio/x-raw,format=F32LE"));
    QVERIFY(player->audioSink());
    GstElement *sink = gst_bin_get_by_name(GST_BIN(player->audioSink()), "microdap-resume");
    QVERIFY(sink);
    std::unique_ptr<GstElement, void (*)(GstElement *)> sinkRef(sink, [](GstElement *e) { gst_object_unref(e); });

    std::atomic<qint64> minPts{std::numeric_limits<qint64>::max()};
    std::atomic<int> buffers{0};
    auto pull = [&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            GstSample *sample = nullptr;
            g_signal_emit_by_name(sink, "try-pull-sample", GstClockTime(200 * GST_MSECOND), &sample);
            if (!sample)
                continue;
            GstBuffer *buffer = gst_sample_get_buffer(sample);
            if (buffer && GST_BUFFER_PTS_IS_VALID(buffer)) {
                const qint64 pts = qint64(GST_BUFFER_PTS(buffer) / GST_MSECOND);
                if (pts < minPts.load())
                    minPts.store(pts);
                ++buffers;
            }
            gst_sample_unref(sample);
        }
    };

    std::jthread puller(pull);
    player->restore({entry}, 0, 90000, true, {});
    QCOMPARE(player->status(), Player::Playing);
    QTRY_VERIFY_WITH_TIMEOUT(buffers.load() > 20, playTimeoutMs);
    player->stop();
    puller.request_stop();
    puller.join();
    QVERIFY2(minPts.load() >= 89000, QByteArray::number(minPts.load()).constData());
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
    QTest::ignoreMessage(QtWarningMsg, "microdap: no playable track");
    player.restore(entries, 0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(player.status(), Player::Stopped, playTimeoutMs);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(started.count(), 0);

    QTest::ignoreMessage(QtWarningMsg, "microdap: no playable track");
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
    const QString art = scalar(m_library->database(), QStringLiteral("SELECT art_path FROM albums WHERE id = ?"),
                               {albumId})
                            .toString();
    QVERIFY(!art.isEmpty());
    QCOMPARE(player.current().value(QStringLiteral("artPath")).toString(), art);
    QCOMPARE(m_tracks.at(1).artPath, art);
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

    QTest::ignoreMessage(QtWarningMsg, "microdap player: unknown source mode \"jump\"");
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("jump")), 0);
    QCOMPARE(player.playSource(QStringLiteral("album"), 999999, QStringLiteral("play")), 0);
    QCOMPARE(player.queue()->count(), albumSize);
    player.stop();

    Player noLibrary;
    QCOMPARE(noLibrary.playSource(QStringLiteral("album"), albumId, QStringLiteral("play")), 0);
}

void PlayerTest::playSourceShuffleOn()
{
    const qint64 albumId = scalar(m_library->database(), QStringLiteral("SELECT album_id FROM tracks WHERE id = ?"),
                                  {m_tracks.at(0).id})
                               .toLongLong();
    QVERIFY(albumId > 0);
    const int albumSize = static_cast<int>(m_library->albumTracks(albumId).size());
    QVERIFY(albumSize >= 3);

    const auto isPermutation = [](const QList<int> &order, int n) {
        QList<int> sorted = order;
        std::sort(sorted.begin(), sorted.end());
        if (sorted.size() != n)
            return false;
        for (int i = 0; i < n; ++i) {
            if (sorted.at(i) != i)
                return false;
        }
        return true;
    };

    Player player(m_library.get());
    player.queue()->setShuffle(true);
    bool movedOff = false;
    for (int run = 0; run < 40; ++run) {
        QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("play")), albumSize);
        QVERIFY(player.queue()->shuffle());
        QCOMPARE(player.queue()->count(), albumSize);
        QCOMPARE(player.queue()->currentSlot(), 0);
        QCOMPARE(player.queue()->rowAt(0), player.queue()->currentIndex());
        QVERIFY(isPermutation(player.queue()->order(), albumSize));
        QVERIFY(consistent(*player.queue()));
        QCOMPARE(player.status(), Player::Playing);
        if (player.queue()->currentIndex() != 0)
            movedOff = true;
    }
    QVERIFY(movedOff);

    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("play"), m_tracks.at(2).id),
             albumSize);
    QVERIFY(player.queue()->shuffle());
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(2).id);
    QCOMPARE(player.queue()->currentSlot(), 0);
    QVERIFY(isPermutation(player.queue()->order(), albumSize));

    const QVariantList ids = {m_tracks.at(0).id, m_tracks.at(1).id, m_tracks.at(2).id};
    player.playTracks(ids, 1);
    QVERIFY(player.queue()->shuffle());
    QCOMPARE(player.queue()->count(), 3);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QCOMPARE(player.current().value(QStringLiteral("id")).toLongLong(), m_tracks.at(1).id);
    QCOMPARE(player.queue()->currentSlot(), 0);
    QVERIFY(isPermutation(player.queue()->order(), 3));

    player.queue()->setShuffle(false);
    QCOMPARE(player.playSource(QStringLiteral("album"), albumId, QStringLiteral("play")), albumSize);
    QVERIFY(!player.queue()->shuffle());
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(player.queue()->currentSlot(), 0);
    QVERIFY(player.queue()->order().isEmpty());
    player.stop();
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

void PlayerTest::sinkDescriptionText()
{
    const QString o181 = QStringLiteral("audioconvert dithering=none noise-shaping=none ! audio/x-raw,format=S32LE "
                                        "! alsasink device=hw:CARD=B,DEV=0");
    QCOMPARE(Player::sinkDescription(Chain::Pw, {}, {}, o181, QStringLiteral("B")), QStringLiteral("pulsesink"));
    QCOMPARE(Player::sinkDescription(Chain::Bp, {}, {}, o181, QStringLiteral("B")),
             QStringLiteral("pulsesink volume=1.0 mute=false"));
    QCOMPARE(Player::sinkDescription(Chain::Ex, {}, {}, o181, QStringLiteral("B")), o181);

    const QString sink = QStringLiteral("fakesink sync=false");
    const QString exclusive = QStringLiteral("alsasink device=hw:CARD=%1,DEV=0");
    QCOMPARE(Player::sinkDescription(Chain::Pw, sink, exclusive, o181, QStringLiteral("B")), sink);
    QCOMPARE(Player::sinkDescription(Chain::Bp, sink, exclusive, o181, QStringLiteral("B")), sink);
    QCOMPARE(Player::sinkDescription(Chain::Ex, sink, exclusive, o181, QStringLiteral("B")),
             QStringLiteral("alsasink device=hw:CARD=B,DEV=0"));
    QCOMPARE(Player::sinkDescription(Chain::Ex, {}, sink, o181, QStringLiteral("B")), sink);
}

void PlayerTest::chainWithoutPolicy()
{
    for (const bool withPolicy : {false, true}) {
        Player player;
        FakePolicy policy;
        if (withPolicy)
            player.setPolicy(&policy);
        GstElement *sink = player.audioSink();
        QVERIFY(!player.audioFilter());
        QVERIFY(sink);
        QCOMPARE(factories(sink, false), QStringList({QStringLiteral("fakesink")}));

        player.restore(m_tracks, 0, 0);
        QVERIFY(prerolled(player));
        QCOMPARE(player.builtChain(), Chain::Pw);
        QVERIFY(!builtFilter(player));
        QCOMPARE(builtSink(player), sink);

        player.rebuildAt(0, GST_STATE_PAUSED);
        QVERIFY(prerolled(player));
        QVERIFY(!builtFilter(player));
        QCOMPARE(builtSink(player), sink);
        QCOMPARE(player.audioSink(), sink);
        QCOMPARE(policy.admitted.size(), withPolicy ? 2 : 0);
        player.stop();
    }
}

void PlayerTest::chainRoundTrip()
{
    auto player = exclusivePlayer();
    FakePolicy policy;
    player->setPolicy(&policy);
    QVERIFY(!player->audioFilter());

    player->restore(m_tracks, 0, 0);
    QVERIFY(prerolled(*player));
    QVERIFY(!builtFilter(*player));

    policy.chain = Chain::Bp;
    player->rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(*player));
    QCOMPARE(player->builtChain(), Chain::Bp);
    GstElement *filter = builtFilter(*player);
    QVERIFY(filter);
    QCOMPARE(factories(filter, false), QStringList({QStringLiteral("audioconvert"), QStringLiteral("capsfilter")}));
    QCOMPARE(player->sinkFormat().value(QStringLiteral("format")).toString(), QStringLiteral("F32LE"));
    QCOMPARE(factories(builtSink(*player), false), QStringList({QStringLiteral("fakesink")}));

    policy.chain = Chain::Ex;
    player->rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(*player));
    QCOMPARE(player->builtChain(), Chain::Ex);
    QVERIFY(!builtFilter(*player));
    QVERIFY(!player->audioFilter());
    GstElement *named = gst_bin_get_by_name(GST_BIN(builtSink(*player)), "microdap-ex");
    QVERIFY(named);
    gst_object_unref(named);

    policy.chain = Chain::Pw;
    player->rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(*player));
    QCOMPARE(player->builtChain(), Chain::Pw);
    QVERIFY(!builtFilter(*player));
    named = gst_bin_get_by_name(GST_BIN(builtSink(*player)), "microdap-ex");
    QVERIFY(!named);
    player->stop();
}

void PlayerTest::rateTail()
{
    Player player;
    FakePolicy policy;
    player.setPolicy(&policy);
    QVERIFY(!player.audioFilter());

    policy.rate = 96000;
    player.restore(m_tracks, 0, 0);
    QVERIFY(prerolled(player));
    GstElement *filter = builtFilter(player);
    QVERIFY(filter);
    QVERIFY(factories(filter, true).contains(QStringLiteral("audioresample")));
    QVERIFY(!factories(filter, true).contains(QStringLiteral("audioiirfilter")));
    QCOMPARE(player.sinkFormat().value(QStringLiteral("rate")).toInt(), 96000);
    const int source = player.sourceFormat().value(QStringLiteral("rate")).toInt();
    QVERIFY(source > 0);
    QVERIFY(source != 96000);

    policy.rate = 0;
    player.rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(player));
    QVERIFY(!builtFilter(player));
    QCOMPARE(player.sinkFormat().value(QStringLiteral("rate")).toInt(), source);

    policy.rate = 96000;
    policy.chain = Chain::Bp;
    player.rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(player));
    QVERIFY(!factories(builtFilter(player), true).contains(QStringLiteral("audioresample")));
    QCOMPARE(player.sinkFormat().value(QStringLiteral("rate")).toInt(), source);

    policy.chain = Chain::Pw;
    player.rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(player));
    QVERIFY(factories(builtFilter(player), true).contains(QStringLiteral("audioresample")));
    QCOMPARE(player.sinkFormat().value(QStringLiteral("rate")).toInt(), 96000);
    player.stop();
}

void PlayerTest::bitPerfectProperties()
{
    const QString usb = QStringLiteral("alsa_output.usb-FIIO_KA17-00.analog-stereo");
    QCOMPARE(Player::bitPerfectProperties(usb, 44100),
             QStringLiteral("props, node.dont-fallback=(string)true, node.dont-reconnect=(string)true, "
                            "target.object=(string)\"%1\", node.force-rate=(string)44100").arg(usb));
    QCOMPARE(Player::bitPerfectProperties({}, 0),
             QStringLiteral("props, node.dont-fallback=(string)true, node.dont-reconnect=(string)true"));
    GstStructure *props = gst_structure_from_string(Player::bitPerfectProperties(usb, 96000).toUtf8().constData(), nullptr);
    QVERIFY(props);
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(props, "target.object")), usb);
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(props, "node.force-rate")), QStringLiteral("96000"));
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(props, "node.dont-reconnect")), QStringLiteral("true"));
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(props, "node.dont-fallback")), QStringLiteral("true"));
    gst_structure_free(props);

    if (!gst_element_factory_find("pulsesink"))
        QSKIP("pulsesink not installed");
    auto player = withEnv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("pulsesink"));
    FakePolicy policy;
    policy.chain = Chain::Bp;
    policy.bitPerfect = usb;
    player->setPolicy(&policy);
    player->restore(m_tracks, 0, 0);
    GstElement *sink = player->audioSink();
    QVERIFY(sink);
    GstElement *pulse = GST_IS_BIN(sink) ? nullptr : sink;
    if (!pulse) {
        GstIterator *it = gst_bin_iterate_recurse(GST_BIN(sink));
        GValue item = G_VALUE_INIT;
        while (!pulse && gst_iterator_next(it, &item) == GST_ITERATOR_OK) {
            auto *element = GST_ELEMENT(g_value_get_object(&item));
            if (g_object_class_find_property(G_OBJECT_GET_CLASS(element), "stream-properties"))
                pulse = element;
            g_value_reset(&item);
        }
        g_value_unset(&item);
        gst_iterator_free(it);
    }
    QVERIFY(pulse);
    gchar *device = nullptr;
    GstStructure *set = nullptr;
    g_object_get(pulse, "device", &device, "stream-properties", &set, nullptr);
    QCOMPARE(QString::fromUtf8(device), usb);
    QVERIFY(set);
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(set, "node.dont-reconnect")), QStringLiteral("true"));
    QCOMPARE(QString::fromUtf8(gst_structure_get_string(set, "target.object")), usb);
    g_free(device);
    gst_structure_free(set);
    player->stop();
}

void PlayerTest::pendingLatestWins()
{
    Player player;
    FakePolicy policy;
    policy.now = false;
    player.setPolicy(&policy);
    QSignalSpy started(&player, &Player::trackStarted);
    const QList<QueueEntry> entries = m_tracks + m_tracks;

    player.play(entries, 0);
    QCOMPARE(player.status(), Player::Playing);
    QVERIFY(player.pendingBuild());
    for (int i = 0; i < 5; ++i)
        player.next();
    QCOMPARE(policy.admitted.size(), 6);
    QCOMPARE(player.queue()->currentIndex(), 5);
    QCOMPARE(player.status(), Player::Playing);
    QVERIFY(player.pendingBuild());
    QCOMPARE(player.pendingBuild()->entry.path, entries.at(5).path);
    QCOMPARE(player.pendingBuild()->target, GST_STATE_PLAYING);
    QCOMPARE(player.builtChain(), Chain::None);
    QTest::qWait(200);
    QCOMPARE(started.count(), 0);

    player.buildPending(Chain::Pw);
    QVERIFY(!player.pendingBuild());
    QCOMPARE(player.builtChain(), Chain::Pw);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 5);
    QCOMPARE(policy.admitted.size(), 6);
    player.stop();
}

void PlayerTest::pendingQueueEdits()
{
    Player player;
    FakePolicy policy;
    policy.now = false;
    player.setPolicy(&policy);
    QSignalSpy started(&player, &Player::trackStarted);

    player.play(m_tracks, 1);
    player.queue()->remove(0);
    QCOMPARE(player.queue()->currentIndex(), 0);
    QCOMPARE(policy.admitted.size(), 1);
    player.buildPending(Chain::Pw);
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 0);
    QCOMPARE(player.queue()->entry(0).path, m_tracks.at(1).path);
    player.stop();
    started.clear();

    player.play(m_tracks, 0);
    player.seek(1500);
    QCOMPARE(player.pendingBuild()->positionMs, qint64(1500));
    player.queue()->remove(0);
    QVERIFY(player.pendingBuild());
    QCOMPARE(player.pendingBuild()->entry.path, m_tracks.at(1).path);
    QCOMPARE(player.pendingBuild()->positionMs, qint64(0));
    QCOMPARE(player.queue()->currentIndex(), 0);
    player.buildPending(Chain::Pw);
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 0);
    QCOMPARE(player.queue()->entry(0).path, m_tracks.at(1).path);
    player.stop();
    started.clear();

    player.play(m_tracks, 2);
    player.seek(900);
    player.queue()->set(m_tracks, 0);
    player.buildPending(Chain::Pw);
    QCOMPARE(player.position(), qint64(0));
    QTRY_VERIFY_WITH_TIMEOUT(started.count() >= 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 0);
    player.stop();
    started.clear();

    player.play({m_tracks.at(0)}, 0);
    QVERIFY(player.pendingBuild());
    player.queue()->remove(0);
    QCOMPARE(player.status(), Player::Stopped);
    QVERIFY(!player.pendingBuild());
    player.buildPending(Chain::Pw);
    QTest::qWait(200);
    QCOMPARE(started.count(), 0);
    QCOMPARE(player.builtChain(), Chain::None);
}

void PlayerTest::pendingPauseParks()
{
    Player player;
    FakePolicy policy;
    policy.now = false;
    player.setPolicy(&policy);
    QSignalSpy started(&player, &Player::trackStarted);

    player.play(m_tracks, 1);
    player.seek(700);
    QCOMPARE(player.status(), Player::Playing);
    player.pause();
    QCOMPARE(player.status(), Player::Paused);
    QVERIFY(player.parked());
    QVERIFY(!player.pendingBuild());
    QCOMPARE(player.position(), qint64(700));
    QCOMPARE(player.queue()->currentIndex(), 1);

    policy.now = true;
    player.buildPending(Chain::Pw);
    QTest::qWait(200);
    QCOMPARE(started.count(), 0);
    QCOMPARE(player.builtChain(), Chain::None);
    QVERIFY(player.parked());
    QCOMPARE(policy.admitted.size(), 1);
    player.stop();
    QVERIFY(!player.parked());
}

void PlayerTest::resumeFromChain()
{
    auto player = syncPlayer();
    FakePolicy policy;
    player->setPolicy(&policy);

    player->restore({m_long}, 0, 0);
    QVERIFY(prerolled(*player));
    QCOMPARE(policy.admitted.size(), 1);
    player->resumeFrom();
    QCOMPARE(player->status(), Player::Playing);
    QCOMPARE(policy.admitted.size(), 1);
    QTest::qWait(300);
    player->pause();
    const qint64 at = player->position();
    QVERIFY(at > 0);

    policy.want = Chain::Bp;
    policy.chain = Chain::Bp;
    player->resumeFrom();
    QCOMPARE(policy.admitted.size(), 2);
    QCOMPARE(policy.admitted.last().target, GST_STATE_PLAYING);
    QCOMPARE(policy.admitted.last().positionMs, at);
    QCOMPARE(policy.admitted.last().entry.path, m_long.path);
    QCOMPARE(player->builtChain(), Chain::Bp);
    QCOMPARE(player->status(), Player::Playing);

    player->park(1200);
    QVERIFY(player->parked());
    QCOMPARE(player->status(), Player::Paused);
    QCOMPARE(player->builtChain(), Chain::None);
    player->resumeFrom();
    QCOMPARE(policy.admitted.size(), 3);
    QCOMPARE(policy.admitted.last().positionMs, qint64(1200));
    QCOMPARE(policy.admitted.last().target, GST_STATE_PLAYING);
    QVERIFY(!player->parked());
    QCOMPARE(player->status(), Player::Playing);
    QCOMPARE(player->position(), qint64(1200));
    player->stop();
}

void PlayerTest::rebuildAtKeeps()
{
    Player player;
    FakePolicy policy;
    player.setPolicy(&policy);
    player.restore(m_tracks, 1, 0);
    QVERIFY(prerolled(player));

    player.rebuildAt(800, GST_STATE_PAUSED);
    QCOMPARE(policy.admitted.size(), 2);
    QCOMPARE(policy.admitted.last().entry.path, m_tracks.at(1).path);
    QCOMPARE(policy.admitted.last().positionMs, qint64(800));
    QCOMPARE(policy.admitted.last().target, GST_STATE_PAUSED);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QCOMPARE(player.position(), qint64(800));
    QCOMPARE(player.status(), Player::Paused);
    QVERIFY(prerolled(player));
    QCOMPARE(player.queue()->currentIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(std::abs(player.position() - 800) < 300, playTimeoutMs);
    player.stop();
}

void PlayerTest::seekWhileParked()
{
    Player player;
    FakePolicy policy;
    player.setPolicy(&policy);
    player.restore(m_tracks, 1, 0);
    QVERIFY(prerolled(player));

    player.park(1000);
    QVERIFY(player.parked());
    QCOMPARE(player.position(), qint64(1000));
    player.seek(1500);
    QCOMPARE(player.position(), qint64(1500));
    QVERIFY(player.parked());
    QCOMPARE(player.builtChain(), Chain::None);
    QCOMPARE(policy.admitted.size(), 1);
    QVERIFY(player.sinkFormat().isEmpty());

    policy.now = false;
    player.resumeFrom();
    QCOMPARE(policy.admitted.size(), 2);
    QCOMPARE(policy.admitted.last().positionMs, qint64(1500));
    QCOMPARE(policy.admitted.last().target, GST_STATE_PLAYING);
    QCOMPARE(policy.admitted.last().entry.path, m_tracks.at(1).path);
    QVERIFY(player.pendingBuild());
    player.stop();
    QVERIFY(!player.pendingBuild());
}

void PlayerTest::exclusiveErrors()
{
    auto player = exclusivePlayer();
    FakePolicy policy;
    policy.chain = Chain::Ex;
    player->setPolicy(&policy);
    QSignalSpy started(player.get(), &Player::trackStarted);

    player->restore(m_tracks, 0, 0);
    QVERIFY(prerolled(*player));
    QCOMPARE(player->builtChain(), Chain::Ex);
    postSinkError(*player, GST_RESOURCE_ERROR_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(policy.errors.size(), 1, playTimeoutMs);
    QCOMPARE(policy.errors.first(), int(GST_RESOURCE_ERROR_WRITE));
    postSinkError(*player, GST_RESOURCE_ERROR_WRITE);
    postSinkError(*player, GST_RESOURCE_ERROR_WRITE);
    QTest::qWait(300);
    QCOMPARE(policy.errors.size(), 1);
    QCOMPARE(policy.admitted.size(), 1);
    QCOMPARE(player->queue()->currentIndex(), 0);
    QCOMPARE(started.count(), 1);

    player->rebuildAt(0, GST_STATE_PAUSED);
    QVERIFY(prerolled(*player));
    postSinkError(*player, GST_RESOURCE_ERROR_OPEN_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(policy.errors.size(), 2, playTimeoutMs);
    QCOMPARE(policy.errors.last(), int(GST_RESOURCE_ERROR_OPEN_WRITE));
    postSinkError(*player, GST_RESOURCE_ERROR_OPEN_WRITE);
    QTest::qWait(300);
    QCOMPARE(policy.errors.size(), 2);
    QCOMPARE(policy.admitted.size(), 2);
    QCOMPARE(player->queue()->currentIndex(), 0);

    QueueEntry missing;
    missing.path = m_dir.path() + QStringLiteral("/missing-ex.flac");
    missing.title = QStringLiteral("missing");
    started.clear();
    player->restore({missing, m_tracks.at(0)}, 0, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(started.at(0).at(0).toInt(), 1);
    QCOMPARE(policy.admitted.last().entry.path, m_tracks.at(0).path);
    QCOMPARE(policy.errors.size(), 2);
    player->stop();
}

void PlayerTest::pwSinkError()
{
    for (const bool withPolicy : {false, true}) {
        Player player;
        FakePolicy policy;
        if (withPolicy)
            player.setPolicy(&policy);
        QSignalSpy errors(&player, &Player::error);
        QSignalSpy started(&player, &Player::trackStarted);
        player.restore(m_tracks, 0, 0);
        QVERIFY(prerolled(player));
        QCOMPARE(started.count(), 1);
        postSinkError(player, GST_RESOURCE_ERROR_WRITE);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, playTimeoutMs);
        QCOMPARE(player.queue()->currentIndex(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
        QCOMPARE(started.at(1).at(0).toInt(), 1);
        QCOMPARE(player.status(), Player::Paused);
        QVERIFY(policy.errors.isEmpty());
        player.stop();
    }
}

void PlayerTest::formats()
{
    Player player;
    QSignalSpy changed(&player, &Player::formatChanged);
    QVERIFY(player.sourceFormat().isEmpty());
    QVERIFY(player.sinkFormat().isEmpty());
    player.restore(m_tracks, 0, 0);
    QVERIFY(prerolled(player));
    QVERIFY(changed.count() >= 1);
    const QVariantMap source = player.sourceFormat();
    const QVariantMap sink = player.sinkFormat();
    QVERIFY(source.value(QStringLiteral("rate")).toInt() > 0);
    QVERIFY(!source.value(QStringLiteral("format")).toString().isEmpty());
    QVERIFY(source.value(QStringLiteral("channels")).toInt() > 0);
    QCOMPARE(sink.value(QStringLiteral("rate")).toInt(), source.value(QStringLiteral("rate")).toInt());
    QVERIFY(!sink.value(QStringLiteral("format")).toString().isEmpty());
    player.stop();
    QVERIFY(player.sourceFormat().isEmpty());
    QVERIFY(player.sinkFormat().isEmpty());
}

void PlayerTest::exclusiveSwallowsAfterError()
{
    auto player = exclusivePlayer();
    FakePolicy policy;
    policy.chain = Chain::Ex;
    player->setPolicy(&policy);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy errors(player.get(), &Player::error);

    player->restore(m_tracks, 0, 0);
    QVERIFY(prerolled(*player));
    postSinkError(*player, GST_RESOURCE_ERROR_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(policy.errors.size(), 1, playTimeoutMs);

    GstElement *top = pipelineOf(player->audioSink());
    GError *error = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FAILED, "Internal data stream error.");
    gst_element_post_message(top, gst_message_new_error(GST_OBJECT(top), error, "test"));
    g_error_free(error);
    gst_object_unref(top);
    QTest::qWait(300);
    QCOMPARE(player->queue()->currentIndex(), 0);
    QCOMPARE(errors.count(), 0);
    QCOMPARE(policy.errors.size(), 1);
    QCOMPARE(policy.admitted.size(), 1);
    QCOMPARE(started.count(), 1);
    player->stop();
}

void PlayerTest::sameListenRebuild()
{
    QueueEntry entry = m_long;
    entry.id = m_tracks.at(0).id;
    auto player = syncPlayer(m_library.get());
    FakePolicy policy;
    player->setPolicy(&policy);
    QSignalSpy started(player.get(), &Player::trackStarted);
    const int before = historyCount();

    player->play({entry}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(historyCount() - before, 1);
    QTest::qWait(300);

    player->park(player->position());
    QVERIFY(player->parked());
    player->resumeFrom();
    QCOMPARE(player->status(), Player::Playing);
    QVERIFY(reachesPlaying(*player));
    QTest::qWait(300);
    QCOMPARE(started.count(), 1);
    QCOMPARE(historyCount() - before, 1);

    player->rebuildAt(player->position(), GST_STATE_PLAYING);
    QVERIFY(reachesPlaying(*player));
    QTest::qWait(300);
    QCOMPARE(started.count(), 1);
    QCOMPARE(historyCount() - before, 1);
    QCOMPARE(policy.admitted.size(), 3);

    player->playAt(0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QCOMPARE(historyCount() - before, 2);
    player->stop();
}

void PlayerTest::playingBuildAtPosition()
{
    auto player = syncPlayer();
    FakePolicy policy;
    player->setPolicy(&policy);

    player->restore({m_long}, 0, 2000, true);
    QCOMPARE(player->status(), Player::Playing);
    QCOMPARE(policy.admitted.last().target, GST_STATE_PLAYING);
    GstElement *top = pipelineOf(player->audioSink());
    QElapsedTimer timer;
    timer.start();
    GstState state = GST_STATE_VOID_PENDING;
    while (timer.elapsed() < playTimeoutMs) {
        QCOMPARE(player->status(), Player::Playing);
        QVERIFY(player->position() >= 1900);
        gst_element_get_state(top, &state, nullptr, 0);
        if (state == GST_STATE_PLAYING)
            break;
        QTest::qWait(5);
    }
    gst_object_unref(top);
    QCOMPARE(state, GST_STATE_PLAYING);
    const qint64 at = player->position();
    QVERIFY2(std::abs(at - 2000) < 100, QByteArray::number(at).constData());
    player->stop();
}

void PlayerTest::badSinkConfigure()
{
    auto player = exclusivePlayer(QByteArrayLiteral("nosuchelement-microdap"));
    FakePolicy policy;
    policy.chain = Chain::Ex;
    player->setPolicy(&policy);
    QVERIFY(!player->audioFilter());
    QSignalSpy errors(player.get(), &Player::error);
    QSignalSpy started(player.get(), &Player::trackStarted);

    player->restore(m_tracks, 1, 0);
    QCOMPARE(policy.errors, QList<int>({int(GST_RESOURCE_ERROR_FAILED)}));
    QCOMPARE(errors.count(), 0);
    QCOMPARE(player->builtChain(), Chain::None);
    QVERIFY(player->pendingBuild());
    QCOMPARE(player->pendingBuild()->entry.path, m_tracks.at(1).path);
    QCOMPARE(player->queue()->currentIndex(), 1);
    QCOMPARE(player->status(), Player::Paused);

    player->buildPending(Chain::Pw);
    QVERIFY(prerolled(*player));
    QCOMPARE(player->builtChain(), Chain::Pw);
    QVERIFY(!builtFilter(*player));
    QCOMPARE(started.count(), 1);

    player->rebuildAt(0, GST_STATE_PAUSED);
    QCOMPARE(policy.errors.size(), 2);
    QCOMPARE(player->builtChain(), Chain::None);
    policy.chain = Chain::Pw;
    player->buildPending(Chain::Pw);
    QVERIFY(prerolled(*player));
    QVERIFY(!builtFilter(*player));
    QCOMPARE(errors.count(), 0);
    player->stop();

    auto bad = withEnv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("nosuchelement-microdap"));
    FakePolicy bpPolicy;
    bpPolicy.chain = Chain::Bp;
    bad->setPolicy(&bpPolicy);
    QVERIFY(!bad->audioFilter());
    QVERIFY(!bad->audioSink());
    bad->restore(m_tracks, 0, 0);
    QCOMPARE(bad->builtChain(), Chain::Pw);
    QVERIFY(!bad->audioFilter());
    QVERIFY(!bad->audioSink());
    QVERIFY(bpPolicy.errors.isEmpty());
    bad->stop();
}

void PlayerTest::guardBeforeAdmit()
{
    auto player = syncPlayer();
    FakeBluezModel fake;
    fake.setTransportActive(true);
    player->setBluez(&fake);
    FakePolicy policy;
    player->setPolicy(&policy);

    player->play({m_long}, 0);
    QCOMPARE(policy.admitted.size(), 1);
    QCOMPARE(policy.admitted.last().target, GST_STATE_PAUSED);
    QCOMPARE(player->status(), Player::Paused);
    QVERIFY(player->playPending());
    player->stop();

    player->park(500);
    QVERIFY(!player->parked());
    QCOMPARE(player->status(), Player::Stopped);
    QCOMPARE(player->position(), qint64(0));
    player->setBluez(nullptr);

    Player empty;
    empty.setPolicy(&policy);
    empty.park(500);
    QVERIFY(!empty.parked());
    QCOMPARE(empty.status(), Player::Stopped);
}

void PlayerTest::rateTailNoWarnings()
{
    Player player;
    FakePolicy policy;
    player.setPolicy(&policy);
    glibWarnings.storeRelaxed(0);
    const GLogFunc previous = g_log_set_default_handler(countWarnings, nullptr);

    player.restore(m_tracks, 0, 0);
    const bool first = prerolled(player);
    const int source = player.sinkFormat().value(QStringLiteral("rate")).toInt();
    policy.rate = 96000;
    player.rebuildAt(0, GST_STATE_PAUSED);
    const bool second = prerolled(player);
    const int fixed = player.sinkFormat().value(QStringLiteral("rate")).toInt();
    player.stop();

    g_log_set_default_handler(previous, nullptr);
    QVERIFY(first);
    QVERIFY(second);
    QVERIFY(source > 0);
    QCOMPARE(fixed, 96000);
    QCOMPARE(glibWarnings.loadRelaxed(), 0);
}

void PlayerTest::gaplessPerChain()
{
    for (const Chain chain : {Chain::Pw, Chain::Bp, Chain::Ex}) {
        auto player = exclusivePlayer();
        FakePolicy policy;
        policy.chain = chain;
        player->setPolicy(&policy);
        QSignalSpy started(player.get(), &Player::trackStarted);
        QSignalSpy eos(player.get(), &Player::endOfStream);
        QSignalSpy errors(player.get(), &Player::error);
        player->play(m_tracks, 0);
        QCOMPARE(player->builtChain(), chain);
        QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, playTimeoutMs);
        QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2}));
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
        QCOMPARE(errors.count(), 0);
        QCOMPARE(policy.admitted.size(), 1);
        QCOMPARE(player->status(), Player::Stopped);
    }
}

void PlayerTest::gaplessRealTime()
{
    QList<QueueEntry> entries;
    for (int i = 0; i < 3; ++i) {
        QueueEntry entry;
        entry.id = 100 + i;
        entry.title = QStringLiteral("real %1").arg(i);
        entry.path = m_dir.path() + QStringLiteral("/real%1.flac").arg(i);
        entry.durationMs = 2000;
        QVERIFY(encodeSine(entry.path, 2));
        entries.append(entry);
    }
    auto player = syncPlayer();
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    QElapsedTimer clock;
    clock.start();
    player->play(entries, 0);
    QCOMPARE(player->builtChain(), Chain::Pw);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, 3 * playTimeoutMs);
    const qint64 joined = clock.elapsed();
    QVERIFY2(joined >= 1800, qPrintable(QString::number(joined)));
    QCOMPARE(player->queue()->currentIndex(), 1);
    QTest::qWait(300);
    QVERIFY(player->position() < 1000);

    player->seek(1000);
    QElapsedTimer afterSeek;
    afterSeek.start();
    QTRY_VERIFY_WITH_TIMEOUT(player->position() >= 1100, 1000);
    QCOMPARE(player->queue()->currentIndex(), 1);
    QVERIFY2(player->position() < 1800, qPrintable(QString::number(player->position())));
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, 3 * playTimeoutMs);
    const qint64 third = afterSeek.elapsed();
    QVERIFY2(third >= 800, qPrintable(QString::number(third)));
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 3 * playTimeoutMs);
    const qint64 end = afterSeek.elapsed();
    QVERIFY2(end >= 2700, qPrintable(QString::number(end)));
    QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2}));
    QCOMPARE(errors.count(), 0);
}

void PlayerTest::cueProbeUntouched()
{
    for (const Chain chain : {Chain::Bp, Chain::Ex}) {
        const QByteArray description = QByteArrayLiteral("appsink name=microdap-cue sync=false max-buffers=1");
        auto player = chain == Chain::Ex ? exclusivePlayer(description) : withEnv("MICRODAP_AUDIO_SINK", description);
        FakePolicy policy;
        policy.chain = chain;
        player->setPolicy(&policy);
        player->restore({m_long}, 0, 0);
        QVERIFY(prerolled(*player));
        QCOMPARE(player->builtChain(), chain);

        GstElement *top = pipelineOf(player->audioSink());
        GstElement *cue = gst_bin_get_by_name(GST_BIN(top), "cueprobe");
        gst_object_unref(top);
        QVERIFY(cue);
        std::unique_ptr<GstElement, void (*)(GstElement *)> cueRef(cue, [](GstElement *e) { gst_object_unref(e); });
        GstElement *sink = gst_bin_get_by_name(GST_BIN(player->audioSink()), "microdap-cue");
        QVERIFY(sink);
        std::unique_ptr<GstElement, void (*)(GstElement *)> sinkRef(sink, [](GstElement *e) { gst_object_unref(e); });

        Capture in;
        Capture out;
        Capture captured;
        GstPad *cueSink = gst_element_get_static_pad(cue, "sink");
        GstPad *cueSrc = gst_element_get_static_pad(cue, "src");
        const gulong inProbe = gst_pad_add_probe(cueSink, GST_PAD_PROBE_TYPE_BUFFER, captureProbe, &in, nullptr);
        const gulong outProbe = gst_pad_add_probe(cueSrc, GST_PAD_PROBE_TYPE_BUFFER, captureProbe, &out, nullptr);
        std::jthread puller([&](std::stop_token stop) {
            while (!stop.stop_requested()) {
                GstSample *sample = nullptr;
                g_signal_emit_by_name(sink, "try-pull-sample", GstClockTime(200 * GST_MSECOND), &sample);
                if (!sample)
                    continue;
                captured.add(gst_sample_get_buffer(sample));
                gst_sample_unref(sample);
            }
        });
        player->resume();
        QTRY_VERIFY_WITH_TIMEOUT(out.size() > 20, playTimeoutMs);
        player->stop();
        puller.request_stop();
        puller.join();
        gst_pad_remove_probe(cueSink, inProbe);
        gst_pad_remove_probe(cueSrc, outProbe);
        gst_object_unref(cueSink);
        gst_object_unref(cueSrc);

        int same = 0;
        int sunk = 0;
        for (const auto &[pts, bytes] : out.buffers) {
            const auto before = in.buffers.find(pts);
            QVERIFY(before != in.buffers.end());
            QCOMPARE(before->second, bytes);
            ++same;
            const auto after = captured.buffers.find(pts);
            if (chain == Chain::Ex && after != captured.buffers.end()) {
                QCOMPARE(after->second, bytes);
                ++sunk;
            }
        }
        QVERIFY(same > 20);
        QVERIFY(captured.buffers.size() > 20);
        if (chain == Chain::Ex)
            QVERIFY(sunk > 20);
    }
}

void PlayerTest::replayGainPerTrack()
{
    auto player = withEnv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("appsink name=microdap-rg sync=false max-buffers=1"));
    player->setReplayGain(QStringLiteral("track"));
    QList<QueueEntry> entries = m_tracks.mid(0, 2);
    entries[0].trackGainDb = -6.0;
    entries[0].trackPeak = 1.0;
    entries[1].trackGainDb = 6.0;
    entries[1].trackPeak = 0.25;
    entries[1].albumGainDb = -9.0;
    const double first = std::pow(10.0, -6.0 / 20.0);
    const double second = std::pow(10.0, 6.0 / 20.0);

    GstElement *volume = deckVolume(*player);
    QVERIFY(volume);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(player->audioSink()), "microdap-rg");
    QVERIFY(sink);
    std::unique_ptr<GstElement, void (*)(GstElement *)> sinkRef(sink, [](GstElement *e) { gst_object_unref(e); });
    Levels in;
    std::vector<float> out;
    std::mutex outLock;
    GstPad *volumeSink = gst_element_get_static_pad(volume, "sink");
    const gulong probe = gst_pad_add_probe(
        volumeSink, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM),
        levelProbe, &in, nullptr);
    std::jthread puller([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            GstSample *sample = nullptr;
            g_signal_emit_by_name(sink, "try-pull-sample", GstClockTime(200 * GST_MSECOND), &sample);
            if (!sample)
                continue;
            const float peak = peakOf(gst_sample_get_buffer(sample));
            gst_sample_unref(sample);
            std::lock_guard guard(outLock);
            out.push_back(peak);
        }
    });
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    player->play(entries, 0);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    puller.request_stop();
    puller.join();
    gst_pad_remove_probe(volumeSink, probe);
    gst_object_unref(volumeSink);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));

    QCOMPARE(out.size(), in.peaks.size());
    int seen[2] = {0, 0};
    double gain[2] = {0, 0};
    for (std::size_t i = 0; i < out.size(); ++i) {
        const auto [item, peak] = in.peaks.at(i);
        QVERIFY(item == 0 || item == 1);
        if (peak < 0.01f)
            continue;
        const double ratio = out.at(i) / peak;
        if (seen[item]++ == 0)
            gain[item] = ratio;
        QVERIFY2(std::abs(ratio - (item == 0 ? first : second)) < 1e-4, qPrintable(QString::number(ratio)));
    }
    QVERIFY(seen[0] > 10 && seen[1] > 10);
    QVERIFY(std::abs(gain[0] - first) < 1e-4);
    QVERIFY(std::abs(gain[1] - second) < 1e-4);
}

void PlayerTest::replayGainWhilePaused()
{
    QueueEntry entry = m_long;
    entry.trackGainDb = -6.0;
    entry.albumGainDb = -3.0;
    entry.albumPeak = 0.5;
    auto player = syncPlayer();
    player->setReplayGain(QStringLiteral("album"));
    player->restore({entry}, 0, 0);
    QVERIFY(prerolled(*player));
    GstElement *volume = deckVolume(*player);
    QVERIFY(volume);
    QVERIFY(std::abs(volumeOf(volume) - std::pow(10.0, -3.0 / 20.0)) < 1e-6);
    player->setReplayGain(QStringLiteral("track"));
    QVERIFY(std::abs(volumeOf(volume) - std::pow(10.0, -6.0 / 20.0)) < 1e-6);
    player->setReplayGain(QStringLiteral("off"));
    QCOMPARE(volumeOf(volume), 1.0);
    player->resume();
    QVERIFY(reachesPlaying(*player));
    player->setReplayGain(QStringLiteral("track"));
    QCOMPARE(volumeOf(volume), 1.0);
    player->stop();
}

void PlayerTest::crossfadeOverlap()
{
    const QString a = m_dir.path() + QStringLiteral("/fade-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/fade-b.flac");
    QVERIFY(encodeSine(a, 3));
    QVERIFY(encodeSine(b, 3));
    auto player = withEnv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("appsink name=microdap-xf sync=true"));
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(player->audioSink()), "microdap-xf");
    QVERIFY(sink);
    std::unique_ptr<GstElement, void (*)(GstElement *)> sinkRef(sink, [](GstElement *e) { gst_object_unref(e); });
    std::mutex lock;
    qint64 frames = 0;
    std::vector<std::pair<qint64, float>> peaks;
    QStringList caps;
    GstPad *sinkPad = gst_element_get_static_pad(sink, "sink");
    gst_pad_add_probe(sinkPad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                      [](GstPad *, GstPadProbeInfo *info, gpointer data) {
                          GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
                          if (GST_EVENT_TYPE(event) == GST_EVENT_CAPS) {
                              GstCaps *c = nullptr;
                              gst_event_parse_caps(event, &c);
                              gchar *text = gst_caps_to_string(c);
                              static_cast<QStringList *>(data)->append(QString::fromUtf8(text));
                              g_free(text);
                          }
                          return GST_PAD_PROBE_OK;
                      },
                      &caps, nullptr);
    gst_object_unref(sinkPad);
    std::jthread puller([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            GstSample *sample = nullptr;
            g_signal_emit_by_name(sink, "try-pull-sample", GstClockTime(200 * GST_MSECOND), &sample);
            if (!sample)
                continue;
            gint channels = 1;
            if (GstCaps *caps = gst_sample_get_caps(sample))
                gst_structure_get_int(gst_caps_get_structure(caps, 0), "channels", &channels);
            GstBuffer *buffer = gst_sample_get_buffer(sample);
            const qint64 n = qint64(gst_buffer_get_size(buffer) / sizeof(float) / std::max(1, channels));
            const float peak = peakOf(buffer);
            gst_sample_unref(sample);
            std::lock_guard guard(lock);
            peaks.emplace_back(frames, peak);
            frames += n;
        }
    });
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    QList<int> mixersAtStart;
    connect(player.get(), &Player::trackStarted, this, [&] { mixersAtStart.append(watch.added.load()); });
    player->play({sineEntry(a, 1, 3), sineEntry(b, 2, 3)}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(watch.removed.load(), 1, 8000);
    QCOMPARE(player->status(), Player::Playing);
    QCOMPARE(childCount(*player), 2);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
    puller.request_stop();
    puller.join();
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(mixersAtStart, QList<int>({0, 1}));
    QCOMPARE(errors.count(), 0);
    QCOMPARE(watch.added.load(), 1);
    caps.removeDuplicates();
    QVERIFY2(caps.size() == 1, qPrintable(caps.join(QStringLiteral(" | "))));
    const qint64 rate = 44100;
    QVERIFY2(std::abs(frames - (6 * rate - rate)) <= 1, qPrintable(QString::number(frames)));
    const qint64 middle = 2 * rate + rate / 2;
    float mixed = 0.0f;
    float alone = 0.0f;
    for (const auto &[at, peak] : peaks) {
        if (at <= middle && at + 2205 > middle)
            mixed = std::max(mixed, peak);
        if (at > rate / 2 && at < rate)
            alone = std::max(alone, peak);
    }
    QVERIFY(alone > 0.5f);
    QVERIFY2(std::abs(mixed - alone * float(std::sqrt(2.0))) < 0.02f * alone,
             qPrintable(QStringLiteral("%1 %2").arg(mixed).arg(alone)));
}

void PlayerTest::crossfadeRateChangeGapless()
{
    const QString a = m_dir.path() + QStringLiteral("/rate-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/rate-b.flac");
    QVERIFY(encodeSine(a, 2, 44100));
    QVERIFY(encodeSine(b, 2, 48000));
    for (const bool known : {true, false}) {
        QueueEntry first = sineEntry(a, 1, 2);
        QueueEntry second = sineEntry(b, 2, 2);
        if (known) {
            first.sampleRate = 44100;
            second.sampleRate = 48000;
        }
        auto player = syncPlayer();
        player->setCrossfade(1000);
        MixerWatch watch;
        watchMixer(*player, watch);
        QSignalSpy started(player.get(), &Player::trackStarted);
        QSignalSpy eos(player.get(), &Player::endOfStream);
        QSignalSpy errors(player.get(), &Player::error);
        player->play({first, second}, 0);
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
        QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
        QCOMPARE(errors.count(), 0);
        QCOMPARE(watch.added.load(), known ? 0 : 1);
        QCOMPARE(watch.removed.load(), known ? 0 : 1);
        QCOMPARE(childCount(*player), 2);
    }
}

void PlayerTest::crossfadeAlbumGapless()
{
    QList<QueueEntry> entries = m_tracks.mid(0, 2);
    for (int i = 0; i < entries.size(); ++i) {
        entries[i].albumId = 11;
        entries[i].discNo = 1;
        entries[i].trackNo = i + 1;
        entries[i].durationMs = 2000;
    }
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    player->play(entries, 0);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(watch.added.load(), 0);
}

void PlayerTest::crossfadeSkipDropsOutgoing()
{
    QList<QueueEntry> entries;
    for (int i = 0; i < 3; ++i) {
        const QString path = m_dir.path() + QStringLiteral("/skip-%1.flac").arg(i);
        QVERIFY(encodeSine(path, 3));
        entries.append(sineEntry(path, i + 1, 3));
    }
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy errors(player.get(), &Player::error);
    player->play(entries, 0);
    QTRY_VERIFY_WITH_TIMEOUT(watch.added.load() == 1 && started.count() == 2, 6000);
    QCOMPARE(watch.removed.load(), 0);
    QCOMPARE(childCount(*player), 4);
    player->next();
    QCOMPARE(watch.removed.load(), 1);
    QCOMPARE(childCount(*player), 2);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, playTimeoutMs);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2}));
    QCOMPARE(player->queue()->currentIndex(), 2);
    QCOMPARE(errors.count(), 0);
    player->stop();
}

void PlayerTest::crossfadeSeekBeforeOverlap()
{
    const QString a = m_dir.path() + QStringLiteral("/seek-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/seek-b.flac");
    QVERIFY(encodeSine(a, 3));
    QVERIFY(encodeSine(b, 3));
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    player->play({sineEntry(a, 1, 3), sineEntry(b, 2, 3)}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(watch.added.load(), 1, playTimeoutMs);
    QCOMPARE(started.count(), 1);
    player->seek(500);
    QCOMPARE(watch.removed.load(), 1);
    QCOMPARE(childCount(*player), 2);
    QTRY_COMPARE_WITH_TIMEOUT(watch.added.load(), 2, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(watch.removed.load(), 2);
    QCOMPARE(errors.count(), 0);
}

void PlayerTest::crossfadeSeekInOverlap()
{
    const QString a = m_dir.path() + QStringLiteral("/over-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/over-b.flac");
    QVERIFY(encodeSine(a, 3));
    QVERIFY(encodeSine(b, 3));
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    player->play({sineEntry(a, 1, 3), sineEntry(b, 2, 3)}, 0);
    QTRY_VERIFY_WITH_TIMEOUT(watch.added.load() == 1 && started.count() == 2, 6000);
    player->seek(1500);
    QCOMPARE(watch.removed.load(), 1);
    QCOMPARE(childCount(*player), 2);
    QCOMPARE(player->queue()->currentIndex(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(player->position() >= 1500 && player->position() < 2500, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(errors.count(), 0);
}

void PlayerTest::crossfadeEarlyEnd()
{
    const QString a = m_dir.path() + QStringLiteral("/early-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/early-b.flac");
    QVERIFY(encodeSine(a, 3));
    QVERIFY(encodeSine(b, 2));
    QVERIFY(claimSeconds(a, 6));
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    int eosAtSecond = -1;
    connect(player.get(), &Player::trackStarted, this, [&](int index) {
        if (index == 1)
            eosAtSecond = int(eos.count());
    });
    player->play({sineEntry(a, 1, 3), sineEntry(b, 2, 2)}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(watch.added.load(), 1, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QCOMPARE(eosAtSecond, 0);
    QCOMPARE(watch.removed.load(), 1);
    QCOMPARE(player->status(), Player::Playing);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(errors.count(), 0);
}

void PlayerTest::crossfadeStalePlan()
{
    const QString a = m_dir.path() + QStringLiteral("/stale-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/stale-b.flac");
    const QString c = m_dir.path() + QStringLiteral("/stale-c.flac");
    QVERIFY(encodeSine(a, 2));
    QVERIFY(encodeSine(b, 6));
    QVERIFY(encodeSine(c, 2));
    QVERIFY(claimSeconds(a, 8));
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy errors(player.get(), &Player::error);
    int childrenAtThird = -1;
    connect(player.get(), &Player::trackStarted, this, [&](int index) {
        if (index == 2)
            childrenAtThird = childCount(*player);
    });
    player->play({sineEntry(a, 1, 2), sineEntry(b, 2, 6), sineEntry(c, 3, 2)}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QCOMPARE(watch.added.load(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 3, 10000);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2}));
    QCOMPARE(childrenAtThird, 4);
    QCOMPARE(watch.added.load(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(watch.removed.load(), 1, playTimeoutMs);
    QCOMPARE(errors.count(), 0);
    player->stop();
}

void PlayerTest::crossfadeOffsetAfterJoin()
{
    QList<QueueEntry> entries;
    for (int i = 0; i < 4; ++i) {
        const QString path = m_dir.path() + QStringLiteral("/join-%1.flac").arg(i);
        QVERIFY(encodeSine(path, 3));
        entries.append(sineEntry(path, i + 1, 3));
    }
    entries[1].albumId = 7;
    entries[1].album = QStringLiteral("Joined");
    entries[1].trackNo = 1;
    entries[2].albumId = 7;
    entries[2].album = QStringLiteral("Joined");
    entries[2].trackNo = 2;
    auto player = withEnv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("appsink name=microdap-join sync=true"));
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    GstElement *sink = gst_bin_get_by_name(GST_BIN(player->audioSink()), "microdap-join");
    QVERIFY(sink);
    std::unique_ptr<GstElement, void (*)(GstElement *)> sinkRef(sink, [](GstElement *e) { gst_object_unref(e); });
    std::atomic<qint64> frames{0};
    std::jthread puller([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            GstSample *sample = nullptr;
            g_signal_emit_by_name(sink, "try-pull-sample", GstClockTime(200 * GST_MSECOND), &sample);
            if (!sample)
                continue;
            gint channels = 1;
            if (GstCaps *caps = gst_sample_get_caps(sample))
                gst_structure_get_int(gst_caps_get_structure(caps, 0), "channels", &channels);
            frames += qint64(gst_buffer_get_size(gst_sample_get_buffer(sample)) / sizeof(float) / std::max(1, channels));
            gst_sample_unref(sample);
        }
    });
    struct Hold
    {
        std::mutex lock;
        std::condition_variable wake;
        GstPad *pad = nullptr;
        gulong id = 0;
        QString stream;
        bool fresh = false;
        bool held = false;
        bool open = false;
    } hold;
    const auto release = [&hold] {
        std::lock_guard guard(hold.lock);
        hold.open = true;
        hold.wake.notify_all();
    };
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    connect(player.get(), &Player::trackStarted, this, [&](int index) {
        if (index == 1 && !hold.pad) {
            GstElement *deck = pipelineChild(*player, "deck1");
            QVERIFY(deck);
            std::lock_guard guard(hold.lock);
            hold.pad = gst_element_get_static_pad(deck, "src");
            gst_object_unref(deck);
            if (GstEvent *sticky = gst_pad_get_sticky_event(hold.pad, GST_EVENT_STREAM_START, 0)) {
                const gchar *id = nullptr;
                gst_event_parse_stream_start(sticky, &id);
                hold.stream = QString::fromUtf8(id);
                gst_event_unref(sticky);
            }
            hold.id = gst_pad_add_probe(
                hold.pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM,
                [](GstPad *, GstPadProbeInfo *info, gpointer data) {
                    auto *held = static_cast<Hold *>(data);
                    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
                    std::unique_lock guard(held->lock);
                    if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START) {
                        const gchar *id = nullptr;
                        gst_event_parse_stream_start(event, &id);
                        held->fresh = held->fresh || QString::fromUtf8(id) != held->stream;
                    }
                    if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT && held->fresh && !held->held) {
                        held->held = true;
                        held->wake.wait_for(guard, std::chrono::seconds(5), [held] { return held->open; });
                    }
                    return GST_PAD_PROBE_OK;
                },
                &hold, nullptr);
        }
        if (index == 2)
            QTimer::singleShot(0, this, release);
    });
    player->play(entries, 0);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 20000);
    release();
    puller.request_stop();
    puller.join();
    QVERIFY(hold.held);
    gst_pad_remove_probe(hold.pad, hold.id);
    gst_object_unref(hold.pad);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1, 2, 3}));
    QCOMPARE(watch.added.load(), 2);
    QCOMPARE(errors.count(), 0);
    const qint64 rate = 44100;
    QVERIFY2(std::abs(frames.load() - (12 * rate - 2 * rate)) <= 2, qPrintable(QString::number(frames.load())));
}

void PlayerTest::crossfadeErrorWhileMixing()
{
    const QString a = m_dir.path() + QStringLiteral("/mixerr-a.flac");
    const QString b = m_dir.path() + QStringLiteral("/mixerr-b.flac");
    QVERIFY(encodeSine(a, 3));
    QVERIFY(encodeSine(b, 3));
    auto player = syncPlayer();
    player->setCrossfade(1000);
    MixerWatch watch;
    watchMixer(*player, watch);
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy eos(player.get(), &Player::endOfStream);
    QSignalSpy errors(player.get(), &Player::error);
    connect(player.get(), &Player::trackStarted, this, [&](int index) {
        if (index != 1)
            return;
        GstElement *deck = pipelineChild(*player, "deck");
        QVERIFY(deck);
        GError *err = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_DECODE, "cut");
        gst_element_post_message(deck, gst_message_new_error(GST_OBJECT(deck), err, nullptr));
        g_error_free(err);
        gst_object_unref(deck);
    });
    player->play({sineEntry(a, 1, 3), sineEntry(b, 2, 3)}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 2, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(watch.removed.load(), 1, 500);
    QCOMPARE(childCount(*player), 2);
    QCOMPARE(player->queue()->currentIndex(), 1);
    QVERIFY(player->position() < 900);
    QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, 8000);
    QCOMPARE(startedIndexes(started), QList<int>({0, 1}));
    QCOMPARE(errors.count(), 0);
}

void PlayerTest::dsdRoutes()
{
    QVERIFY(Player::isDsd(QStringLiteral("/m/a.DSF")));
    QVERIFY(Player::isDsd(QStringLiteral("/m/a.dff")));
    QVERIFY(!Player::isDsd(QStringLiteral("/m/a.flac")));
    const QList<std::pair<Chain, Chain>> routes{{Chain::Pw, Chain::Pw}, {Chain::Bp, Chain::Pw}, {Chain::Ex, Chain::Ex}};
    for (const auto &[asked, built] : routes) {
        for (const char *file : {"dsd64.dsf", "dsd64.dff"}) {
            auto player = exclusivePlayer();
            FakePolicy policy;
            policy.chain = asked;
            player->setPolicy(&policy);
            QueueEntry entry;
            entry.path = QStringLiteral(MICRODAP_FIXTURE_FORMATS "/") + QLatin1String(file);
            entry.title = QLatin1String(file);
            player->restore({entry}, 0, 0);
            QVERIFY2(prerolled(*player), file);
            QCOMPARE(player->builtChain(), built);
            QVERIFY(player->dsd());
            QCOMPARE(player->sourceFormat().value(QStringLiteral("format")).toString(), QStringLiteral("DSDU8"));
            QCOMPARE(player->sinkFormat().value(QStringLiteral("rate")).toInt(), 88200);
            player->stop();
            QVERIFY(!player->dsd());
        }
    }
    QCOMPARE(Deck::dsdPcmRateFor(352800), 88200);
    QCOMPARE(Deck::dsdPcmRateFor(705600), 176400);
    QCOMPARE(Deck::dsdPcmRateFor(1411200), 176400);
}

void PlayerTest::dsdBoundary()
{
    QueueEntry dsd;
    dsd.path = QStringLiteral(MICRODAP_FIXTURE_FORMATS "/dsd64.dff");
    dsd.title = QStringLiteral("DFF");
    for (const QList<QueueEntry> &entries : {QList<QueueEntry>{m_tracks.at(0), dsd, dsd},
                                             QList<QueueEntry>{dsd, m_tracks.at(0)}}) {
        Player player;
        FakePolicy policy;
        player.setPolicy(&policy);
        QSignalSpy started(&player, &Player::trackStarted);
        QSignalSpy eos(&player, &Player::endOfStream);
        QSignalSpy errors(&player, &Player::error);
        player.play(entries, 0);
        QTRY_COMPARE_WITH_TIMEOUT(eos.count(), 1, playTimeoutMs);
        QCOMPARE(startedIndexes(started).size(), entries.size());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(policy.admitted.size(), 2);
        QCOMPARE(policy.admitted.at(1).entry.path, entries.at(1).path);
        QCOMPARE(player.status(), Player::Stopped);
    }
}

void PlayerTest::deckMissing()
{
    GstRegistry *registry = gst_registry_get();
    GstElementFactory *factory = gst_element_factory_find("dsdconvert");
    QVERIFY(factory);
    gst_registry_remove_feature(registry, GST_PLUGIN_FEATURE(factory));
    const auto restore = qScopeGuard([registry, factory] {
        gst_registry_add_feature(registry, GST_PLUGIN_FEATURE(factory));
        gst_object_unref(factory);
    });
    QueueEntry dsd;
    dsd.path = QStringLiteral(MICRODAP_FIXTURE_FORMATS "/dsd64.dff");
    dsd.title = QStringLiteral("DFF");
    auto player = syncPlayer();
    QSignalSpy started(player.get(), &Player::trackStarted);
    QSignalSpy errors(player.get(), &Player::error);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("microdap player: deck")));
    player->play({dsd, m_long}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, playTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QCOMPARE(startedIndexes(started), QList<int>({1}));
    QCOMPARE(player->queue()->currentIndex(), 1);
    QCOMPARE(player->status(), Player::Playing);
    player->stop();
}

void PlayerTest::dsd128Refused()
{
    QByteArray data;
    {
        QFile file(QStringLiteral(MICRODAP_FIXTURE_FORMATS "/dsd64.dff"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        data = file.readAll();
    }
    const qsizetype fs = data.indexOf("FS  ");
    QVERIFY(fs > 0);
    const quint32 rate = 5644800;
    for (int i = 0; i < 4; ++i)
        data[fs + 12 + i] = static_cast<char>((rate >> ((3 - i) * 8)) & 0xff);
    QueueEntry dsd;
    dsd.path = m_dir.path() + QStringLiteral("/dsd128.dff");
    dsd.title = QStringLiteral("DSD128");
    {
        QFile file(dsd.path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(file.write(data), data.size());
    }
    for (const Chain chain : {Chain::Pw, Chain::Ex}) {
        auto player = exclusivePlayer(QByteArrayLiteral("fakesink sync=true name=microdap-ex"));
        FakePolicy policy;
        policy.chain = chain;
        player->setPolicy(&policy);
        QSignalSpy started(player.get(), &Player::trackStarted);
        QSignalSpy notices(player.get(), &Player::notice);
        if (chain == Chain::Pw)
            QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("dsd128 and up not supported")));
        player->play({dsd, m_tracks.at(0)}, 0);
        if (chain == Chain::Pw) {
            QTRY_COMPARE_WITH_TIMEOUT(notices.count(), 1, playTimeoutMs);
            QCOMPARE(notices.first().at(0).toString(), QStringLiteral("dsd128 and up not supported"));
            QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
            QCOMPARE(startedIndexes(started), QList<int>({1}));
        } else {
            QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
            QCOMPARE(startedIndexes(started), QList<int>({0}));
            QCOMPARE(notices.count(), 0);
        }
        player->stop();
    }
}

void PlayerTest::outputLostPauses()
{
    auto player = syncPlayer();
    player->play({m_long}, 0);
    QTRY_COMPARE_WITH_TIMEOUT(player->status(), Player::Playing, playTimeoutMs);
    player->outputLost();
    QCOMPARE(player->status(), Player::Paused);

    FakeBluezModel fake;
    player->setBluez(&fake);
    fake.clearCommands();
    player->outputLost();
    QVERIFY(fake.commands().isEmpty());
    QVERIFY(fake.applyPreset(QStringLiteral("active")));
    fake.clearCommands();
    player->outputLost();
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("pause")}));
    QCOMPARE(player->status(), Player::Paused);
    player->setBluez(nullptr);
    player->stop();
}

QTEST_GUILESS_MAIN(PlayerTest)
#include "tst_player.moc"
