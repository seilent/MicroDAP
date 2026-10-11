#include "OutputPorts.h"
#include "Player.h"
#include "Settings.h"
#include "State.h"

#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

using namespace casdap;

namespace {

constexpr int playTimeoutMs = 5000;

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

class FakeRestore : public RestorePort
{
public:
    explicit FakeRestore(Settings *settings)
        : m_settings(settings)
    {
    }

    bool levelSet() const override
    {
        return m_settings->outputLevel() != QLatin1StringView("off");
    }

    void restorePlaying(bool playing) override
    {
        calls.append(playing);
    }

    QList<bool> calls;

private:
    Settings *m_settings;
};

}

class StateTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void levelSetRestoresParked();
    void levelSetPausedRestoreNoReplay();
    void levelOffRestoresAsToday();

private:
    qint64 savePlaying(const QString &path, bool handoff);

    QTemporaryDir m_dir;
    QList<QueueEntry> m_entries;
};

void StateTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));
    QVERIFY(m_dir.isValid());
    for (int i = 0; i < 2; ++i) {
        QueueEntry entry;
        entry.path = m_dir.path() + QStringLiteral("/track%1.flac").arg(i);
        entry.title = QStringLiteral("Track %1").arg(i);
        entry.durationMs = 6000;
        QVERIFY(encodeSine(entry.path, 6));
        m_entries.append(entry);
    }
}

qint64 StateTest::savePlaying(const QString &path, bool handoff)
{
    const QByteArray sink = qgetenv("CASDAP_AUDIO_SINK");
    qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    Player player;
    qputenv("CASDAP_AUDIO_SINK", sink);
    State state(&player, path);
    player.play(m_entries, 1);
    if (!QTest::qWaitFor([&player] { return player.status() == Player::Playing && player.position() >= 500; },
                         playTimeoutMs))
        return -1;
    if (!state.save(handoff))
        return -1;
    QJsonObject object;
    if (!State::read(path, object))
        return -1;
    return static_cast<qint64>(object.value(QStringLiteral("position")).toDouble(-1));
}

void StateTest::levelSetRestoresParked()
{
    const QString path = m_dir.path() + QStringLiteral("/parked/state.json");
    const qint64 position = savePlaying(path, true);
    QVERIFY(position >= 500);

    Settings settings(m_dir.path() + QStringLiteral("/parked/settings.ini"));
    settings.setOutputLevel(QStringLiteral("exclusive"));
    FakeRestore restore(&settings);
    Player player;
    QSignalSpy started(&player, &Player::trackStarted);
    State state(&player, path);
    state.setRestore(&restore);
    QVERIFY(state.load());

    QCOMPARE(player.queue()->count(), 2);
    QCOMPARE(player.queue()->currentIndex(), 1);
    QVERIFY(player.parked());
    QCOMPARE(player.builtChain(), PlayerPort::OutputChain::None);
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.position(), position);
    QCOMPARE(restore.calls, QList<bool>{true});
    QTest::qWait(500);
    QCOMPARE(started.count(), 0);
    QVERIFY(player.parked());
    QCOMPARE(player.position(), position);
    QCOMPARE(restore.calls.size(), 1);
}

void StateTest::levelSetPausedRestoreNoReplay()
{
    const QString path = m_dir.path() + QStringLiteral("/paused/state.json");
    const qint64 position = savePlaying(path, false);
    QVERIFY(position >= 500);

    Settings settings(m_dir.path() + QStringLiteral("/paused/settings.ini"));
    settings.setOutputLevel(QStringLiteral("bitperfect"));
    FakeRestore restore(&settings);
    Player player;
    State state(&player, path);
    state.setRestore(&restore);
    QVERIFY(state.load());

    QCOMPARE(player.queue()->currentIndex(), 1);
    QVERIFY(player.parked());
    QCOMPARE(player.status(), Player::Paused);
    QCOMPARE(player.position(), position);
    QVERIFY(restore.calls.isEmpty());
}

void StateTest::levelOffRestoresAsToday()
{
    const QString path = m_dir.path() + QStringLiteral("/off/state.json");
    QVERIFY(savePlaying(path, true) >= 500);

    Settings settings(m_dir.path() + QStringLiteral("/off/settings.ini"));
    QCOMPARE(settings.outputLevel(), QStringLiteral("off"));
    FakeRestore restore(&settings);
    Player player;
    QSignalSpy started(&player, &Player::trackStarted);
    State state(&player, path);
    state.setRestore(&restore);
    QVERIFY(state.load());

    QCOMPARE(player.queue()->currentIndex(), 1);
    QVERIFY(!player.parked());
    QCOMPARE(player.status(), Player::Playing);
    QCOMPARE(player.builtChain(), PlayerPort::OutputChain::Pw);
    QTRY_COMPARE_WITH_TIMEOUT(started.count(), 1, playTimeoutMs);
    QVERIFY(restore.calls.isEmpty());

    const QString pausedPath = m_dir.path() + QStringLiteral("/off-paused/state.json");
    QVERIFY(savePlaying(pausedPath, false) >= 500);
    Player paused;
    State pausedState(&paused, pausedPath);
    pausedState.setRestore(&restore);
    QVERIFY(pausedState.load());
    QVERIFY(!paused.parked());
    QCOMPARE(paused.status(), Player::Paused);
    QCOMPARE(paused.builtChain(), PlayerPort::OutputChain::Pw);
    QVERIFY(restore.calls.isEmpty());
}

QTEST_GUILESS_MAIN(StateTest)
#include "tst_state.moc"
