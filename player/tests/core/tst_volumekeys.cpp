#include "AlsaVolume.h"
#include "OutputFakes.h"
#include "Settings.h"
#include "VolumeKeys.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QSocketNotifier>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <memory>

#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace microdap;

namespace {

constexpr int retry = 50;

const MixerRange fiio{0, 120, -60.0, 0.0};
const QString fiioSink = QStringLiteral("alsa_output.usb-FIIO_SNOWSKY_TINY_B-01.analog-stereo");

class FakeKeys : public KeySource
{
public:
    void press(int direction)
    {
        emit step(direction);
    }
};

class FakeSink : public SinkVolume
{
public:
    void step(int percent) override
    {
        steps.append(percent);
    }

    void set(int percent) override
    {
        sets.append(percent);
    }

    void setCap(int percent) override
    {
        caps.append(percent);
    }

    void dropPending() override
    {
        ++drops;
    }

    void report(int level)
    {
        emit levelChanged(level);
    }

    QList<int> steps;
    QList<int> sets;
    QList<int> caps;
    int drops = 0;
};

using Props = QList<QPair<QByteArray, QByteArray>>;

std::unique_ptr<pa_proplist, void (*)(pa_proplist *)> proplistOf(const Props &props)
{
    std::unique_ptr<pa_proplist, void (*)(pa_proplist *)> out(pa_proplist_new(), &pa_proplist_free);
    for (const auto &[key, value] : props)
        pa_proplist_sets(out.get(), key.constData(), value.constData());
    return out;
}

pa_cvolume volumeOf(std::initializer_list<pa_volume_t> values)
{
    pa_cvolume volume;
    pa_cvolume_init(&volume);
    for (pa_volume_t value : values)
        volume.values[volume.channels++] = value;
    return volume;
}

}

class VolumeKeysTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void pressSteps();
    void otherEventsIgnored();
    void missingNodeRetried();
    void holdRepeats();
    void repeatValueIgnored();
    void eofRetriesWithoutSpinning();
    void closeStopsRepeat();
    void stepTimesVolumeStep();
    void invalidVolumeStep();
    void levelForwarded();
    void steppedClamps();
    void steppedEveryChannel();
    void steppedAdds();
    void levelIsMaxChannel();
    void alsaPercentRoundTrips();
    void alsaStepZeroIsFloor();
    void alsaStepClamps();
    void alsaStepAtLeastOne();
    void alsaStepNearest();
    void alsaPollErrorCloses_data();
    void alsaPollErrorCloses();
    void alsaEventsFailureCloses();
    void alsaNoDeviceWriteCloses();
    void alsaCloseSilent();
    void alsaReopenById();
    void alsaPacing();
    void alsaPressWhileClosed();
    void alsaWatchRewrites();
    void alsaWatchGivesUp();
    void alsaWatchKeepsLaterEnd();
    void alsaPressAtLimitSkipsWrite();
    void alsaMuteFloorMapped();
    void alsaNonLinearRejected();
    void dacCardOf_data();
    void dacCardOf();
    void bluezInputMatched();
    void dacHelpersFailWithoutServer();
    void dacFailuresDroppedWithObject();
    void sinkRateCache();
    void sinkRateWithoutServer();
    void routedPulseForwards();
    void routedAlsaWritesPcm();
    void routedDropsPulseLevels();
    void routedDownNeverUnmutes();
    void routedStoreSyncOrder();
    void routedStoreSyncAfterLastPress();
    void routedStoreSyncGated();
    void routedOwnedStepPerCard();
    void routedStoreSyncNudge();
    void routedSkipsWithoutMixer();
    void routedDropsPulsePending();
    void pulseCapStopsSteps();
    void pulseLeveledAtCap();
    void alsaCapStep();
    void routedPulseCapAndSet();
    void routedAlsaCap();
    void routedAlsaCapPullsRestoreDown();
    void alsaPullDownBudget();
    void routedPowerOnOncePerBoot();

private:
    void addNode(const QString &event, const QString &name);
    int writer(const QString &event);
    void send(int fd, int type, int code, int value);
    std::unique_ptr<EvdevKeys> make();

    std::unique_ptr<QTemporaryDir> m_dir;
    QList<int> m_fds;
};

void VolumeKeysTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QVERIFY(QDir().mkpath(m_dir->filePath(QStringLiteral("dev"))));
    qputenv("MICRODAP_DEV_INPUT", QFile::encodeName(m_dir->filePath(QStringLiteral("dev"))));
    qputenv("MICRODAP_SYS_INPUT", QFile::encodeName(m_dir->filePath(QStringLiteral("sys"))));
    qputenv("MICRODAP_VOLUME_NAMES", "microdap-test-up:microdap-test-down");
    qputenv("MICRODAP_RUNTIME_DIR", QFile::encodeName(m_dir->filePath(QStringLiteral("run"))));
    qputenv("MICRODAP_BOOT_ID", QFile::encodeName(m_dir->filePath(QStringLiteral("boot_id"))));
}

void VolumeKeysTest::cleanup()
{
    for (int fd : std::as_const(m_fds))
        ::close(fd);
    m_fds.clear();
    m_dir.reset();
    qunsetenv("PULSE_SERVER");
}

void VolumeKeysTest::addNode(const QString &event, const QString &name)
{
    const QString device = m_dir->filePath(QStringLiteral("sys/") + event + QStringLiteral("/device"));
    QVERIFY(QDir().mkpath(device));
    QFile file(device + QStringLiteral("/name"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(name.toUtf8() + '\n');
    file.close();
    const QByteArray path = QFile::encodeName(m_dir->filePath(QStringLiteral("dev/") + event));
    QCOMPARE(::mkfifo(path.constData(), 0600), 0);
}

int VolumeKeysTest::writer(const QString &event)
{
    const QByteArray path = QFile::encodeName(m_dir->filePath(QStringLiteral("dev/") + event));
    const int fd = ::open(path.constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0)
        m_fds.append(fd);
    return fd;
}

void VolumeKeysTest::send(int fd, int type, int code, int value)
{
    input_event event{};
    event.type = quint16(type);
    event.code = quint16(code);
    event.value = value;
    QCOMPARE(::write(fd, &event, sizeof(event)), ssize_t(sizeof(event)));
}

std::unique_ptr<EvdevKeys> VolumeKeysTest::make()
{
    return std::make_unique<EvdevKeys>(retry);
}

void VolumeKeysTest::pressSteps()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    addNode(QStringLiteral("event5"), QStringLiteral("microdap-test-down"));
    const int up = writer(QStringLiteral("event3"));
    const int down = writer(QStringLiteral("event5"));
    QVERIFY(up >= 0 && down >= 0);
    auto keys = make();
    QCOMPARE(keys->openCount(), 2);
    QSignalSpy spy(keys.get(), &KeySource::step);
    send(up, EV_KEY, KEY_VOLUMEUP, 1);
    send(up, EV_SYN, SYN_REPORT, 0);
    send(up, EV_KEY, KEY_VOLUMEUP, 0);
    send(down, EV_KEY, KEY_VOLUMEDOWN, 1);
    send(down, EV_KEY, KEY_VOLUMEDOWN, 0);
    QTRY_COMPARE(spy.count(), 2);
    QCOMPARE(spy.at(0).at(0).toInt(), 1);
    QCOMPARE(spy.at(1).at(0).toInt(), -1);
    QTest::qWait(150);
    QCOMPARE(spy.count(), 2);
}

void VolumeKeysTest::otherEventsIgnored()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    const int up = writer(QStringLiteral("event3"));
    QVERIFY(up >= 0);
    auto keys = make();
    QSignalSpy spy(keys.get(), &KeySource::step);
    send(up, EV_KEY, KEY_A, 1);
    send(up, EV_KEY, KEY_A, 0);
    send(up, EV_KEY, KEY_VOLUMEUP, 0);
    send(up, EV_SW, KEY_VOLUMEUP, 1);
    send(up, EV_KEY, KEY_MUTE, 1);
    QTest::qWait(150);
    QCOMPARE(spy.count(), 0);
}

void VolumeKeysTest::missingNodeRetried()
{
    auto keys = make();
    QCOMPARE(keys->openCount(), 0);
    QSignalSpy spy(keys.get(), &KeySource::step);
    QTest::qWait(3 * retry);
    addNode(QStringLiteral("event7"), QStringLiteral("microdap-test-down"));
    const int down = writer(QStringLiteral("event7"));
    QVERIFY(down >= 0);
    QTRY_COMPARE(keys->openCount(), 1);
    send(down, EV_KEY, KEY_VOLUMEDOWN, 1);
    send(down, EV_KEY, KEY_VOLUMEDOWN, 0);
    QTRY_COMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), -1);
}

void VolumeKeysTest::holdRepeats()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    const int up = writer(QStringLiteral("event3"));
    QVERIFY(up >= 0);
    auto keys = make();
    QList<qint64> at;
    QElapsedTimer clock;
    connect(keys.get(), &KeySource::step, this, [&at, &clock](int direction) {
        QCOMPARE(direction, 1);
        at.append(clock.elapsed());
    });
    clock.start();
    send(up, EV_KEY, KEY_VOLUMEUP, 1);
    QTRY_COMPARE(at.size(), 1);
    QTest::qWait(EvdevKeys::holdMs - 100);
    QCOMPARE(at.size(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(at.size() >= 4, 2000);
    send(up, EV_KEY, KEY_VOLUMEUP, 0);
    QTest::qWait(50);
    const qsizetype released = at.size();
    QVERIFY(at.at(1) - at.at(0) >= EvdevKeys::holdMs - 30);
    for (qsizetype i = 2; i < at.size(); ++i)
        QVERIFY(at.at(i) - at.at(i - 1) >= EvdevKeys::repeatMs - 15);
    QTest::qWait(3 * EvdevKeys::repeatMs);
    QCOMPARE(at.size(), released);
}

void VolumeKeysTest::repeatValueIgnored()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    const int up = writer(QStringLiteral("event3"));
    QVERIFY(up >= 0);
    auto keys = make();
    QSignalSpy spy(keys.get(), &KeySource::step);
    send(up, EV_KEY, KEY_VOLUMEUP, 1);
    for (int i = 0; i < 5; ++i)
        send(up, EV_KEY, KEY_VOLUMEUP, 2);
    send(up, EV_KEY, KEY_VOLUMEUP, 0);
    QTRY_COMPARE(spy.count(), 1);
    QTest::qWait(EvdevKeys::holdMs + 2 * EvdevKeys::repeatMs);
    QCOMPARE(spy.count(), 1);
}

void VolumeKeysTest::eofRetriesWithoutSpinning()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    const int up = writer(QStringLiteral("event3"));
    QVERIFY(up >= 0);
    auto keys = make();
    QCOMPARE(keys->openCount(), 1);
    send(up, EV_KEY, KEY_VOLUMEUP, 1);
    send(up, EV_KEY, KEY_VOLUMEUP, 0);
    QSignalSpy spy(keys.get(), &KeySource::step);
    QTRY_COMPARE(spy.count(), 1);
    ::close(up);
    m_fds.removeAll(up);
    QTRY_COMPARE(keys->openCount(), 2);
    QTest::qWait(10 * retry);
    QCOMPARE(keys->openCount(), 2);
    const int again = writer(QStringLiteral("event3"));
    QVERIFY(again >= 0);
    send(again, EV_KEY, KEY_VOLUMEUP, 1);
    send(again, EV_KEY, KEY_VOLUMEUP, 0);
    QTRY_COMPARE(spy.count(), 2);
}

void VolumeKeysTest::closeStopsRepeat()
{
    addNode(QStringLiteral("event3"), QStringLiteral("microdap-test-up"));
    const int up = writer(QStringLiteral("event3"));
    QVERIFY(up >= 0);
    auto keys = make();
    QSignalSpy spy(keys.get(), &KeySource::step);
    send(up, EV_KEY, KEY_VOLUMEUP, 1);
    QTRY_COMPARE(spy.count(), 1);
    ::close(up);
    m_fds.removeAll(up);
    QTRY_COMPARE(keys->openCount(), 2);
    QTest::qWait(EvdevKeys::holdMs + 2 * EvdevKeys::repeatMs);
    QCOMPARE(spy.count(), 1);
}

void VolumeKeysTest::stepTimesVolumeStep()
{
    Settings settings(nullptr);
    auto *keys = new FakeKeys;
    auto *sink = new FakeSink;
    VolumeKeys volume(keys, sink, &settings);
    QCOMPARE(settings.volumeStep(), 5);
    keys->press(1);
    settings.setVolumeStep(2);
    keys->press(-1);
    settings.setVolumeStep(10);
    keys->press(1);
    keys->press(-1);
    QCOMPARE(sink->steps, QList<int>({5, -2, 10, -10}));
}

void VolumeKeysTest::invalidVolumeStep()
{
    Settings settings(nullptr);
    settings.setValue(QStringLiteral("audio.volume_step"), 7u);
    QCOMPARE(settings.value(QStringLiteral("audio.volume_step")).toUInt(), 7u);
    auto *keys = new FakeKeys;
    auto *sink = new FakeSink;
    VolumeKeys volume(keys, sink, &settings);
    keys->press(1);
    QCOMPARE(sink->steps, QList<int>({5}));
}

void VolumeKeysTest::levelForwarded()
{
    Settings settings(nullptr);
    auto *sink = new FakeSink;
    VolumeKeys volume(new FakeKeys, sink, &settings);
    QSignalSpy spy(&volume, &VolumeKeys::levelChanged);
    sink->report(42);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 42);
}

void VolumeKeysTest::steppedClamps()
{
    const pa_cvolume high = PulseSinkVolume::stepped(volumeOf({64000, 64000}), 10);
    QCOMPARE(high.values[0], PA_VOLUME_NORM);
    QCOMPARE(high.values[1], PA_VOLUME_NORM);
    const pa_cvolume low = PulseSinkVolume::stepped(volumeOf({1000, 1000}), -10);
    QCOMPARE(low.values[0], PA_VOLUME_MUTED);
    QCOMPARE(low.values[1], PA_VOLUME_MUTED);
    const pa_cvolume full = PulseSinkVolume::stepped(volumeOf({PA_VOLUME_NORM}), 5);
    QCOMPARE(full.values[0], PA_VOLUME_NORM);
    const pa_cvolume over = PulseSinkVolume::stepped(volumeOf({PA_VOLUME_NORM + 1000}), 2);
    QCOMPARE(over.values[0], PA_VOLUME_NORM);
}

void VolumeKeysTest::steppedEveryChannel()
{
    const pa_cvolume half = PulseSinkVolume::stepped(volumeOf({32768, 32768}), 5);
    QCOMPARE(half.channels, quint8(2));
    QCOMPARE(half.values[0], pa_volume_t(36045));
    QCOMPARE(half.values[1], pa_volume_t(36045));
    const pa_cvolume balanced = PulseSinkVolume::stepped(volumeOf({30000, 20000, 10000}), -2);
    QCOMPARE(balanced.channels, quint8(3));
    QCOMPARE(balanced.values[0], pa_volume_t(30000 - 1311));
    QCOMPARE(balanced.values[1], pa_volume_t(20000 - 1311));
    QCOMPARE(balanced.values[2], pa_volume_t(10000 - 1311));
}

void VolumeKeysTest::steppedAdds()
{
    const pa_cvolume start = volumeOf({32768, 32768});
    const pa_cvolume twice = PulseSinkVolume::stepped(PulseSinkVolume::stepped(start, 5), 5);
    QCOMPARE(twice.values[0], pa_volume_t(32768 + 2 * 3277));
    QCOMPARE(twice.values[1], pa_volume_t(32768 + 2 * 3277));
    const pa_cvolume back = PulseSinkVolume::stepped(PulseSinkVolume::stepped(start, 10), -10);
    QCOMPARE(back.values[0], pa_volume_t(32768));
}

void VolumeKeysTest::levelIsMaxChannel()
{
    QCOMPARE(PulseSinkVolume::level(volumeOf({32768, 36045})), 55);
    QCOMPARE(PulseSinkVolume::level(volumeOf({PA_VOLUME_NORM})), 100);
    QCOMPARE(PulseSinkVolume::level(volumeOf({PA_VOLUME_MUTED, PA_VOLUME_MUTED})), 0);
}

void VolumeKeysTest::alsaPercentRoundTrips()
{
    QVERIFY(qFuzzyCompare(AlsaVolume::percentOfDb(0.0), 100.0));
    QVERIFY(qFuzzyCompare(AlsaVolume::percentOfDb(-60.0), 10.0));
    QVERIFY(qFuzzyCompare(AlsaVolume::dbOfPercent(10.0), -60.0));
    QVERIFY(qFuzzyIsNull(AlsaVolume::dbOfPercent(100.0)));
    for (int half = -120; half <= 0; ++half) {
        const double db = half / 2.0;
        QVERIFY(qAbs(AlsaVolume::dbOfPercent(AlsaVolume::percentOfDb(db)) - db) < 1e-9);
    }
    for (int percent = 10; percent <= 100; ++percent)
        QVERIFY(qAbs(AlsaVolume::percentOfDb(AlsaVolume::dbOfPercent(percent)) - percent) < 1e-9);
    for (int step = 0; step <= 120; ++step) {
        const double percent = AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(step, fiio));
        QCOMPARE(AlsaVolume::stepFor(step, 0, fiio), step);
        QVERIFY(qAbs(AlsaVolume::dbOfPercent(percent) - (step / 2.0 - 60.0)) < 1e-9);
    }
}

void VolumeKeysTest::alsaStepZeroIsFloor()
{
    QCOMPARE(AlsaVolume::dbOfStep(0, fiio), -60.0);
    QCOMPARE(qRound(AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(0, fiio))), 10);
    QCOMPARE(AlsaVolume::dbOfStep(120, fiio), 0.0);
    QCOMPARE(AlsaVolume::dbOfStep(20, fiio), -50.0);
}

void VolumeKeysTest::alsaStepClamps()
{
    for (int delta : {2, 5, 10}) {
        QCOMPARE(AlsaVolume::stepFor(120, delta, fiio), 120);
        QCOMPARE(AlsaVolume::stepFor(119, delta, fiio), 120);
        QCOMPARE(AlsaVolume::stepFor(0, -delta, fiio), 0);
        QCOMPARE(AlsaVolume::stepFor(1, -delta, fiio), 0);
        QCOMPARE(AlsaVolume::stepFor(200, delta, fiio), 120);
        QCOMPARE(AlsaVolume::stepFor(-5, -delta, fiio), 0);
    }
}

void VolumeKeysTest::alsaStepAtLeastOne()
{
    for (int delta : {2, 5, 10}) {
        for (int step = 0; step < 120; ++step)
            QVERIFY(AlsaVolume::stepFor(step, delta, fiio) > step);
        for (int step = 1; step <= 120; ++step)
            QVERIFY(AlsaVolume::stepFor(step, -delta, fiio) < step);
    }
}

void VolumeKeysTest::alsaStepNearest()
{
    QCOMPARE(AlsaVolume::stepFor(60, 5, fiio), 68);
    QCOMPARE(AlsaVolume::stepFor(60, -5, fiio), 51);
    QCOMPARE(AlsaVolume::stepFor(0, 2, fiio), 10);
    QCOMPARE(AlsaVolume::stepFor(100, 10, fiio), 107);
}

void VolumeKeysTest::alsaPollErrorCloses_data()
{
    QTest::addColumn<int>("events");
    QTest::newRow("err") << int(POLLERR);
    QTest::newRow("hup") << int(POLLHUP);
    QTest::newRow("nval") << int(POLLNVAL);
    QTest::newRow("err with in") << int(POLLERR | POLLIN);
}

void VolumeKeysTest::alsaPollErrorCloses()
{
    QFETCH(int, events);
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    int enabledAtClose = -1;
    mixer->onClose = [&volume, &enabledAtClose] {
        enabledAtClose = 0;
        const QList<QSocketNotifier *> notifiers = volume.findChildren<QSocketNotifier *>();
        for (QSocketNotifier *notifier : notifiers) {
            if (notifier->isEnabled())
                ++enabledAtClose;
        }
    };
    QSignalSpy spy(&volume, &AlsaVolume::disconnected);
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.findChildren<QSocketNotifier *>().size(), 1);
    mixer->fire(short(events));
    QTRY_VERIFY(!volume.isOpen());
    QCOMPARE(enabledAtClose, 0);
    QCOMPARE(mixer->closes, 1);
    QTRY_COMPARE(spy.count(), 1);
    mixer->fire(short(events));
    QTest::qWait(50);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(mixer->closes, 1);
}

void VolumeKeysTest::alsaEventsFailureCloses()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy spy(&volume, &AlsaVolume::disconnected);
    QVERIFY(volume.open(QStringLiteral("B")));
    mixer->handleResult = -EIO;
    mixer->fire(POLLIN);
    QTRY_VERIFY(!volume.isOpen());
    QCOMPARE(mixer->closes, 1);
    QTRY_COMPARE(spy.count(), 1);
}

void VolumeKeysTest::alsaNoDeviceWriteCloses()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy spy(&volume, &AlsaVolume::disconnected);
    QVERIFY(volume.open(QStringLiteral("B")));
    mixer->setResult = -ENODEV;
    volume.pressed(5);
    QVERIFY(!volume.isOpen());
    QCOMPARE(mixer->writes.size(), 1);
    QCOMPARE(mixer->closes, 1);
    QCOMPARE(spy.count(), 0);
    QTRY_COMPARE(spy.count(), 1);
    volume.pressed(5);
    QTest::qWait(50);
    QCOMPARE(mixer->writes.size(), 1);
    QCOMPARE(spy.count(), 1);
}

void VolumeKeysTest::alsaCloseSilent()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy spy(&volume, &AlsaVolume::disconnected);
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.close();
    QVERIFY(!volume.isOpen());
    QCOMPARE(mixer->closes, 1);
    QTest::qWait(50);
    QCOMPARE(spy.count(), 0);
}

void VolumeKeysTest::alsaReopenById()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    mixer->value = 80;
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.step(), 80);
    QCOMPARE(volume.rangeDb(), 60.0);
    QCOMPARE(volume.stepDb(), -20.0);
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(mixer->opens, QStringList({QStringLiteral("B")}));
    volume.close();
    mixer->value = 70;
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.step(), 80);
    volume.close();
    mixer->value = 50;
    QVERIFY(volume.open(QStringLiteral("C")));
    QCOMPARE(volume.step(), 50);
    mixer->value = 30;
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.step(), 30);
    QCOMPARE(mixer->opens, QStringList({QStringLiteral("B"), QStringLiteral("B"), QStringLiteral("C"),
                                        QStringLiteral("B")}));
    QCOMPARE(mixer->closes, 3);
    QVERIFY(mixer->writes.isEmpty());
    mixer->openResult = -ENODEV;
    volume.close();
    QVERIFY(!volume.open(QStringLiteral("B")));
    QVERIFY(!volume.isOpen());
}

void VolumeKeysTest::alsaPacing()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy levels(&volume, &AlsaVolume::levelChanged);
    QVERIFY(volume.open(QStringLiteral("B")));
    for (int i = 0; i < 5; ++i)
        volume.pressed(2);
    QCOMPARE(levels.count(), 5);
    QCOMPARE(mixer->writes.size(), 1);
    QVERIFY(mixer->writes.first() > 80);
    QTRY_COMPARE_WITH_TIMEOUT(mixer->writes.size(), 2, 1000);
    QCOMPARE(mixer->writes.last(), long(volume.step()));
    QCOMPARE(mixer->value, long(volume.step()));
    QCOMPARE(levels.last().at(0).toInt(), qRound(AlsaVolume::percentOfDb(volume.stepDb())));
    QTest::qWait(2 * AlsaVolume::defaultPacingMs);
    QCOMPARE(mixer->writes.size(), 2);
}

void VolumeKeysTest::alsaPressWhileClosed()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy levels(&volume, &AlsaVolume::levelChanged);
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.close();
    volume.pressed(5);
    QCOMPARE(levels.count(), 1);
    const int moved = volume.step();
    QCOMPARE(moved, AlsaVolume::stepFor(80, 5, fiio));
    QVERIFY(mixer->writes.isEmpty());
    mixer->value = 40;
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(mixer->writes, QList<long>({long(moved)}));
    QCOMPARE(mixer->value, long(moved));
    volume.close();
    volume.pressed(5);
    mixer->value = 40;
    QVERIFY(volume.open(QStringLiteral("C")));
    QCOMPARE(mixer->writes.size(), 1);
    QCOMPARE(volume.step(), 40);
}

void VolumeKeysTest::alsaWatchRewrites()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned), 5000);
    QSignalSpy foreign(&volume, &AlsaVolume::foreignStep);
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.setOwnedStep(90);
    QElapsedTimer clock;
    clock.start();
    volume.watchReturn(1500);
    QCOMPARE(mixer->writes, QList<long>({90}));
    QTest::qWait(50);
    QCOMPARE(foreign.count(), 0);
    mixer->foreign(60);
    QTRY_COMPARE(mixer->writes.size(), 2);
    QCOMPARE(mixer->writes.last(), 90L);
    QCOMPARE(foreign.count(), 1);
    QCOMPARE(foreign.last().at(0).toInt(), 60);
    volume.pressed(5);
    const int current = volume.step();
    QCOMPARE(current, AlsaVolume::stepFor(90, 5, fiio));
    mixer->foreign(50);
    QTRY_COMPARE(mixer->writes.size(), 3);
    QCOMPARE(mixer->writes.last(), long(current));
    QVERIFY(clock.elapsed() < 1500);
    QTest::qWait(int(2000 - clock.elapsed()));
    mixer->foreign(40);
    QTRY_COMPARE(foreign.count(), 3);
    QTest::qWait(200);
    QCOMPARE(mixer->writes, QList<long>({90, 90, long(current)}));
    QCOMPARE(volume.step(), current);
}

void VolumeKeysTest::alsaWatchKeepsLaterEnd()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned), 10);
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.setOwnedStep(90);
    QElapsedTimer clock;
    clock.start();
    volume.watchReturn(1500);
    volume.watchReturn(200);
    QCOMPARE(mixer->writes, QList<long>({90, 90}));
    QTest::qWait(600);
    mixer->foreign(60);
    QTRY_COMPARE(mixer->writes.size(), 3);
    QCOMPARE(mixer->writes.last(), 90L);
    QVERIFY(clock.elapsed() < 1500);
}

void VolumeKeysTest::alsaWatchGivesUp()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned), 5000);
    QSignalSpy foreign(&volume, &AlsaVolume::foreignStep);
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.setOwnedStep(90);
    volume.watchReturn(5000);
    QCOMPARE(mixer->writes.size(), 1);
    for (int i = 0; i <= AlsaVolume::maxRewrites + 1; ++i) {
        mixer->foreign(60 + i);
        QTRY_COMPARE(foreign.count(), i + 1);
    }
    QTest::qWait(50);
    QCOMPARE(mixer->writes.size(), 1 + AlsaVolume::maxRewrites);
    QCOMPARE(mixer->value, long(60 + AlsaVolume::maxRewrites + 1));
}

void VolumeKeysTest::alsaPressAtLimitSkipsWrite()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned));
    QSignalSpy levels(&volume, &AlsaVolume::levelChanged);
    mixer->value = 120;
    QVERIFY(volume.open(QStringLiteral("B")));
    volume.pressed(5);
    QCOMPARE(levels.count(), 1);
    QCOMPARE(levels.last().at(0).toInt(), 100);
    QTest::qWait(50);
    QVERIFY(mixer->writes.isEmpty());
    volume.setOwnedStep(0);
    volume.pressed(-5);
    QCOMPARE(levels.count(), 2);
    QCOMPARE(levels.last().at(0).toInt(), 10);
    QTest::qWait(50);
    QVERIFY(mixer->writes.isEmpty());
}

void VolumeKeysTest::alsaMuteFloorMapped()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    mixer->mixerRange = MixerRange{0, 120, -99999.99, 0.0};
    mixer->dbCurve = [](long step) { return step == 0 ? -99999.99 : -59.5 + (step - 1) * 0.5; };
    mixer->value = 0;
    AlsaVolume volume(std::move(owned));
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.rangeDb(), 59.5);
    QCOMPARE(volume.step(), 1);
    QCOMPARE(volume.stepDb(), -59.5);
    volume.setOwnedStep(0);
    QCOMPARE(volume.step(), 1);
    volume.setOwnedStep(60);
    volume.pressed(5);
    QCOMPARE(mixer->writes.size(), 1);
    QVERIFY(mixer->writes.first() > 60 && mixer->writes.first() < 75);
    volume.setOwnedStep(1);
    volume.close();
    volume.pressed(-10);
    QCOMPARE(volume.step(), 1);
}

void VolumeKeysTest::alsaNonLinearRejected()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    mixer->dbCurve = [](long step) { return 20.0 * std::log10(double(qMax(step, 1L)) / 120.0); };
    AlsaVolume volume(std::move(owned));
    QVERIFY(!volume.open(QStringLiteral("B")));
    QVERIFY(!volume.isOpen());
    QCOMPARE(mixer->opens.size(), 1);
    QCOMPARE(mixer->closes, 1);
    QVERIFY(!mixer->isOpen);
}

void VolumeKeysTest::dacCardOf_data()
{
    QTest::addColumn<Props>("props");
    QTest::addColumn<int>("card");
    QTest::addColumn<QString>("reserve");

    QTest::newRow("fiio") << Props{{"api.alsa.card", "1"},
                                   {"alsa.card", "1"},
                                   {"api.dbus.ReserveDevice1", "Audio1"},
                                   {"api.dbus.ReserveDevice1.Priority", "-20"},
                                   {"device.name", "alsa_card.usb-FIIO_SNOWSKY_TINY_B_000000000000-01"}}
                          << 1 << QStringLiteral("Audio1");
    QTest::newRow("no reserve") << Props{{"alsa.card", "1"}, {"device.name", "alsa_card.usb-FIIO"}} << -1 << QString();
    QTest::newRow("no alsa card") << Props{{"api.alsa.card", "1"}, {"api.dbus.ReserveDevice1", "Audio1"}} << -1
                                  << QString();
    QTest::newRow("bad alsa card") << Props{{"alsa.card", "B"}, {"api.dbus.ReserveDevice1", "Audio1"}} << -1
                                   << QString();
    QTest::newRow("empty") << Props{} << -1 << QString();
}

void VolumeKeysTest::dacCardOf()
{
    QFETCH(Props, props);
    QFETCH(int, card);
    QFETCH(QString, reserve);
    const auto list = proplistOf(props);
    const DacCard out = PulseSinkVolume::dacCardOf(list.get());
    QCOMPARE(out.isValid(), card >= 0);
    QCOMPARE(out.alsaCard, card);
    QCOMPARE(out.reserveName, reserve);
    QCOMPARE(out.cardIndex, quint32(PA_INVALID_INDEX));
    QCOMPARE(PulseSinkVolume::dacCardOf(nullptr).isValid(), false);
}

void VolumeKeysTest::bluezInputMatched()
{
    QVERIFY(PulseSinkVolume::isBluezInput(
        proplistOf({{"node.name", "bluez_input.BC_27_7A_6F_A9_6F.2"}, {"media.name", "SeFL (codec aptX)"}}).get()));
    QVERIFY(!PulseSinkVolume::isBluezInput(proplistOf({{"node.name", "microdap-player"}}).get()));
    QVERIFY(!PulseSinkVolume::isBluezInput(proplistOf({{"media.name", "bluez_input.BC_27_7A_6F_A9_6F.2"}}).get()));
    QVERIFY(!PulseSinkVolume::isBluezInput(nullptr));
    QCOMPARE(PulseSinkVolume::alsaCardOf(proplistOf({{"alsa.card", "0"}}).get()), 0);
    QCOMPARE(PulseSinkVolume::alsaCardOf(proplistOf({{"alsa.card", "-1"}}).get()), -1);
    QCOMPARE(PulseSinkVolume::alsaCardOf(proplistOf({}).get()), -1);
}

void VolumeKeysTest::dacHelpersFailWithoutServer()
{
    qputenv("PULSE_SERVER", QFile::encodeName(QStringLiteral("unix:") + m_dir->filePath(QStringLiteral("none"))));
    PulseSinkVolume sink;
    int cards = 0;
    int volumes = 0;
    int mutes = 0;
    int inputs = 0;
    DacCard card;
    card.alsaCard = 1;
    card.reserveName = QStringLiteral("Audio1");
    bool volumeOk = true;
    bool muteOk = true;
    QList<quint32> found{7};
    sink.cardInfo(69, [&](const DacCard &out) {
        ++cards;
        card = out;
    });
    sink.sinkVolumeDb(QStringLiteral("alsa_output.usb-FIIO"), [&](bool ok, double) {
        ++volumes;
        volumeOk = ok;
    });
    sink.sinkMuted(QStringLiteral("alsa_output.usb-FIIO"), [&](bool ok, bool) {
        ++mutes;
        muteOk = ok;
    });
    sink.bluezInputs([&](const QList<quint32> &out) {
        ++inputs;
        found = out;
    });
    sink.setSinkVolumeDb(QStringLiteral("alsa_output.usb-FIIO"), -40.0);
    sink.unmuteSink(QStringLiteral("alsa_output.usb-FIIO"));
    sink.moveSinkInput(7, QStringLiteral("alsa_output.usb-FIIO"));
    QCOMPARE(cards + volumes + mutes + inputs, 0);
    QTRY_COMPARE(cards, 1);
    QTRY_COMPARE(volumes, 1);
    QTRY_COMPARE(mutes, 1);
    QTRY_COMPARE(inputs, 1);
    QTest::qWait(100);
    QCOMPARE(cards + volumes + mutes + inputs, 4);
    QVERIFY(!card.isValid());
    QVERIFY(!volumeOk);
    QVERIFY(!muteOk);
    QVERIFY(found.isEmpty());
}

void VolumeKeysTest::dacFailuresDroppedWithObject()
{
    qputenv("PULSE_SERVER", QFile::encodeName(QStringLiteral("unix:") + m_dir->filePath(QStringLiteral("none"))));
    int calls = 0;
    {
        PulseSinkVolume sink;
        sink.cardInfo(69, [&](const DacCard &) { ++calls; });
        sink.bluezInputs([&](const QList<quint32> &) { ++calls; });
    }
    QTest::qWait(100);
    QCOMPARE(calls, 0);
}

void VolumeKeysTest::sinkRateCache()
{
    SinkRate cache;
    QCOMPARE(cache.rate(), 0);
    pa_sample_spec spec{PA_SAMPLE_S32LE, 48000, 2};
    QVERIFY(cache.found(spec));
    QCOMPARE(cache.rate(), 48000);
    spec.rate = 44100;
    QVERIFY(!cache.found(spec));
    QCOMPARE(cache.rate(), 44100);
    spec.rate = 96000;
    cache.changed(spec);
    QCOMPARE(cache.rate(), 96000);
    pa_sample_spec invalid{PA_SAMPLE_INVALID, 0, 0};
    QVERIFY(!cache.found(invalid));
    cache.changed(invalid);
    QCOMPARE(cache.rate(), 96000);
    cache.lost();
    QCOMPARE(cache.rate(), 0);
    QVERIFY(!cache.found(invalid));
    spec.rate = 48000;
    QVERIFY(cache.found(spec));
    QCOMPARE(cache.rate(), 48000);
}

void VolumeKeysTest::sinkRateWithoutServer()
{
    qputenv("PULSE_SERVER", QFile::encodeName(QStringLiteral("unix:") + m_dir->filePath(QStringLiteral("none"))));
    PulseSinkVolume sink;
    QSignalSpy known(&sink, &PulseSinkVolume::sinkRateKnown);
    QTest::qWait(100);
    QCOMPARE(sink.defaultSinkRate(), 0);
    QCOMPARE(known.count(), 0);
}

void VolumeKeysTest::routedPulseForwards()
{
    Settings settings(nullptr);
    QStringList log;
    FakeDacSink dac(&log);
    AlsaVolume alsa(std::make_unique<FakeMixer>(&log), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QSignalSpy levels(&volume, &VolumeKeys::levelChanged);
    QVERIFY(alsa.open(QStringLiteral("B")));
    QCOMPARE(routed->route(), DacVolumePort::Route::Pulse);
    keys->press(1);
    keys->press(-1);
    QCOMPARE(pulse->steps, QList<int>({5, -5}));
    pulse->report(42);
    QCOMPARE(levels.count(), 1);
    QCOMPARE(levels.at(0).at(0).toInt(), 42);
    routed->setStoreAllowed(true);
    QTest::qWait(150);
    QVERIFY(log.isEmpty());
    QCOMPARE(alsa.step(), 80);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), 80);
    QCOMPARE(routed->ownedStep(QStringLiteral("C")), -1);
}

void VolumeKeysTest::routedAlsaWritesPcm()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QSignalSpy levels(&volume, &VolumeKeys::levelChanged);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    QCOMPARE(routed->route(), DacVolumePort::Route::Alsa);
    keys->press(1);
    const int first = AlsaVolume::stepFor(80, 5, fiio);
    QCOMPARE(alsa.step(), first);
    QCOMPARE(mixer->writes, QList<long>({long(first)}));
    QCOMPARE(levels.count(), 1);
    QCOMPARE(levels.last().at(0).toInt(), qRound(AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(first, fiio))));
    settings.setVolumeStep(10);
    keys->press(1);
    const int second = AlsaVolume::stepFor(first, 10, fiio);
    QCOMPARE(alsa.step(), second);
    QCOMPARE(levels.count(), 2);
    QTRY_COMPARE(mixer->writes.last(), long(second));
    QVERIFY(pulse->steps.isEmpty());
    QCOMPARE(dac.calls, QStringList({QStringLiteral("unmute ") + fiioSink, QStringLiteral("unmute ") + fiioSink}));
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), second);
}

void VolumeKeysTest::routedDropsPulseLevels()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    AlsaVolume alsa(std::make_unique<FakeMixer>(), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QSignalSpy levels(&volume, &VolumeKeys::levelChanged);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    pulse->report(42);
    QCOMPARE(levels.count(), 0);
    keys->press(-1);
    pulse->report(37);
    QCOMPARE(levels.count(), 1);
    QCOMPARE(levels.last().at(0).toInt(),
             qRound(AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(AlsaVolume::stepFor(80, -5, fiio), fiio))));
    routed->setRoute(DacVolumePort::Route::Pulse, QString(), QString());
    pulse->report(42);
    QCOMPARE(levels.count(), 2);
    QCOMPARE(levels.last().at(0).toInt(), 42);
    keys->press(1);
    QCOMPARE(pulse->steps, QList<int>({5}));
    QCOMPARE(levels.count(), 2);
}

void VolumeKeysTest::routedDownNeverUnmutes()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    dac.muted = true;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    keys->press(-1);
    keys->press(-1);
    QTRY_COMPARE(mixer->writes.last(), long(alsa.step()));
    QVERIFY(dac.calls.isEmpty());
    QVERIFY(dac.muted);
    keys->press(1);
    QCOMPARE(dac.calls, QStringList({QStringLiteral("unmute ") + fiioSink}));
    QVERIFY(!dac.muted);
}

void VolumeKeysTest::routedStoreSyncOrder()
{
    Settings settings(nullptr);
    QStringList log;
    FakeDacSink dac(&log);
    AlsaVolume alsa(std::make_unique<FakeMixer>(&log), 10);
    auto *keys = new FakeKeys;
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    routed->setStoreAllowed(true);
    keys->press(1);
    const int step = alsa.step();
    const QString pcm = QStringLiteral("PCM %1").arg(step);
    QTRY_COMPARE_WITH_TIMEOUT(log.size(), 6, 1000);
    QCOMPARE(log, QStringList({pcm, QStringLiteral("unmute ") + fiioSink,
                               QStringLiteral("sink volume %1 %2").arg(fiioSink).arg(AlsaVolume::dbOfStep(step, fiio)),
                               QStringLiteral("sink volume done ") + fiioSink, QStringLiteral("PCM %1").arg(step + 1),
                               pcm}));
    QCOMPARE(dac.volumeDb, alsa.stepDb());
    QTest::qWait(RoutedSinkVolume::storeWatchMs + 100);
    QCOMPARE(log.size(), 6);
}

void VolumeKeysTest::routedStoreSyncAfterLastPress()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    AlsaVolume alsa(std::make_unique<FakeMixer>(), 10);
    auto *keys = new FakeKeys;
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 1000);
    VolumeKeys volume(keys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    routed->setStoreAllowed(true);
    const QString sync = QStringLiteral("sink volume ") + fiioSink;
    keys->press(-1);
    QTest::qWait(100);
    QCOMPARE(dac.calls.filter(sync).size(), 0);
    keys->press(-1);
    QElapsedTimer since;
    since.start();
    QTRY_COMPARE_WITH_TIMEOUT(dac.calls.filter(sync).size(), 1, 3000);
    QVERIFY(since.elapsed() >= 950);
    QCOMPARE(dac.volumeDb, alsa.stepDb());
    QTest::qWait(400);
    QCOMPARE(dac.calls.filter(sync).size(), 1);
}

void VolumeKeysTest::routedStoreSyncGated()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    const QString sync = QStringLiteral("sink volume ") + fiioSink;
    keys->press(-1);
    QTest::qWait(200);
    QCOMPARE(dac.calls.filter(sync).size(), 0);
    routed->setStoreAllowed(true);
    QTRY_COMPARE(dac.calls.filter(sync).size(), 1);
    keys->press(-1);
    routed->setStoreAllowed(false);
    QTest::qWait(200);
    QCOMPARE(dac.calls.filter(sync).size(), 1);
    const qsizetype writes = mixer->writes.size();
    routed->storeSync(QStringLiteral("B"));
    QCOMPARE(dac.calls.filter(sync).size(), 2);
    QTRY_COMPARE(mixer->writes.size(), writes + 2);
    QCOMPARE(mixer->writes.last(), long(alsa.step()));
    routed->setStoreAllowed(true);
    QTest::qWait(200);
    QCOMPARE(dac.calls.filter(sync).size(), 2);
    routed->storeSync(QStringLiteral("C"));
    QCOMPARE(dac.calls.filter(sync).size(), 2);
    keys->press(-1);
    routed->setRoute(DacVolumePort::Route::Pulse, QString(), QString());
    routed->storeSync(QStringLiteral("B"));
    QTest::qWait(200);
    QCOMPARE(dac.calls.filter(sync).size(), 2);
}

void VolumeKeysTest::routedOwnedStepPerCard()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), 80);
    keys->press(1);
    const int moved = alsa.step();
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), moved);
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink + QStringLiteral(".3"));
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), moved);
    keys->press(1);
    QCOMPARE(dac.calls.last(), QStringLiteral("unmute ") + fiioSink + QStringLiteral(".3"));
    const int again = alsa.step();
    mixer->value = 40;
    QVERIFY(alsa.open(QStringLiteral("C")));
    keys->press(1);
    QCOMPARE(alsa.step(), 40);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), again);
    QCOMPARE(routed->ownedStep(QStringLiteral("C")), 40);
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("C"), QStringLiteral("alsa_output.usb-Apple"));
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), again);
    routed->setOwnedStep(QStringLiteral("B"), 30);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), 30);
    QCOMPARE(alsa.step(), 40);
    routed->setOwnedStep(QStringLiteral("C"), 50);
    QCOMPARE(alsa.step(), 50);
    QCOMPARE(routed->ownedStep(QStringLiteral("C")), 50);
    routed->setRoute(DacVolumePort::Route::Pulse, QString(), QString());
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), 30);
    QCOMPARE(routed->ownedStep(QStringLiteral("C")), 50);
    QCOMPARE(routed->ownedStep(QStringLiteral("D")), -1);
}

void VolumeKeysTest::routedStoreSyncNudge()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    mixer->value = 120;
    AlsaVolume alsa(std::move(owned), 10);
    auto *routed = new RoutedSinkVolume(new FakeSink, &dac, &alsa, 50);
    VolumeKeys volume(new FakeKeys, routed, &settings);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    routed->storeSync(QStringLiteral("B"));
    QVERIFY(mixer->writes.isEmpty());
    QTRY_COMPARE(mixer->writes, QList<long>({119, 120}));
    mixer->foreign(70);
    QTRY_COMPARE(mixer->writes.size(), 3);
    QCOMPARE(mixer->writes.last(), 120L);
    QTest::qWait(RoutedSinkVolume::storeWatchMs + 100);
    mixer->foreign(60);
    QTest::qWait(100);
    QCOMPARE(mixer->writes.size(), 3);
    routed->storeSync(QStringLiteral("B"));
    QTRY_COMPARE(mixer->writes.size(), 4);
    QCOMPARE(mixer->writes.last(), 120L);
    QTest::qWait(RoutedSinkVolume::storeWatchMs + 100);
    QCOMPARE(mixer->writes.size(), 4);
    dac.setOk = false;
    mixer->foreign(60);
    routed->storeSync(QStringLiteral("B"));
    QTRY_COMPARE(dac.calls.last(), QStringLiteral("sink volume done ") + fiioSink);
    QTest::qWait(100);
    QCOMPARE(mixer->writes.size(), 4);
}

void VolumeKeysTest::routedSkipsWithoutMixer()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    QSignalSpy levels(&volume, &VolumeKeys::levelChanged);
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    routed->setStoreAllowed(true);
    keys->press(1);
    routed->storeSync(QStringLiteral("B"));
    QTest::qWait(150);
    QVERIFY(dac.calls.isEmpty());
    QVERIFY(mixer->writes.isEmpty());
    QVERIFY(pulse->steps.isEmpty());
    QCOMPARE(levels.count(), 0);
    QVERIFY(alsa.open(QStringLiteral("C")));
    keys->press(1);
    routed->storeSync(QStringLiteral("B"));
    QTest::qWait(150);
    QVERIFY(dac.calls.isEmpty());
    QVERIFY(mixer->writes.isEmpty());
    QCOMPARE(alsa.step(), 80);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), -1);
    QVERIFY(alsa.open(QStringLiteral("B")));
    alsa.close();
    keys->press(1);
    QCOMPARE(levels.count(), 1);
    QCOMPARE(alsa.step(), AlsaVolume::stepFor(80, 5, fiio));
    routed->storeSync(QStringLiteral("B"));
    QTest::qWait(150);
    QCOMPARE(dac.calls, QStringList({QStringLiteral("unmute ") + fiioSink}));
}

void VolumeKeysTest::routedDropsPulsePending()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    AlsaVolume alsa(std::make_unique<FakeMixer>(), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    keys->press(1);
    routed->setRoute(DacVolumePort::Route::Pulse, QString(), QString());
    QCOMPARE(pulse->drops, 0);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    QCOMPARE(pulse->drops, 1);
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink + QStringLiteral(".3"));
    routed->setRoute(DacVolumePort::Route::Pulse, QString(), QString());
    QCOMPARE(pulse->drops, 1);
    QCOMPARE(pulse->steps, QList<int>({5}));
}

void VolumeKeysTest::pulseCapStopsSteps()
{
    const pa_volume_t cap = pa_volume_t(PA_VOLUME_NORM * 70 / 100);
    const pa_cvolume at = volumeOf({cap, cap});
    const pa_cvolume up = PulseSinkVolume::capped(PulseSinkVolume::stepped(at, 5), 70);
    QCOMPARE(up.values[0], cap);
    QCOMPARE(up.values[1], cap);
    const pa_cvolume near = PulseSinkVolume::capped(PulseSinkVolume::stepped(volumeOf({cap - 1000, cap - 1000}), 5), 70);
    QCOMPARE(near.values[0], cap);
    const pa_cvolume down = PulseSinkVolume::capped(PulseSinkVolume::stepped(at, -5), 70);
    QVERIFY(down.values[0] < cap);
    const pa_cvolume above = PulseSinkVolume::capped(volumeOf({PA_VOLUME_NORM, PA_VOLUME_NORM / 2}), 70);
    QCOMPARE(above.values[0], cap);
    QCOMPARE(above.values[1], pa_volume_t(qRound64(double(PA_VOLUME_NORM / 2) * cap / PA_VOLUME_NORM)));
    QCOMPARE(PulseSinkVolume::level(above), 70);
    const pa_cvolume below = PulseSinkVolume::capped(volumeOf({1000, 2000}), 70);
    QCOMPARE(below.values[0], pa_volume_t(1000));
    QCOMPARE(below.values[1], pa_volume_t(2000));
    QCOMPARE(PulseSinkVolume::level(PulseSinkVolume::capped(volumeOf({PA_VOLUME_NORM}), 100)), 100);
    for (int percent = 50; percent <= 100; percent += 5) {
        const pa_volume_t raw = pa_volume_t(percent * 65536 / 100);
        QCOMPARE(PulseSinkVolume::capped(volumeOf({PA_VOLUME_NORM}), percent).values[0], raw);
        QCOMPARE(PulseSinkVolume::leveled(volumeOf({1000}), 100, percent).values[0], raw);
    }
}

void VolumeKeysTest::pulseLeveledAtCap()
{
    QCOMPARE(PulseSinkVolume::level(PulseSinkVolume::leveled(volumeOf({1000, 1000}), 90, 70)), 70);
    QCOMPARE(PulseSinkVolume::level(PulseSinkVolume::leveled(volumeOf({1000, 1000}), 40, 70)), 40);
    QCOMPARE(PulseSinkVolume::leveled(volumeOf({1000, 1000}), 0, 70).values[0], PA_VOLUME_MUTED);
    const pa_cvolume muted = PulseSinkVolume::leveled(volumeOf({PA_VOLUME_MUTED, PA_VOLUME_MUTED}), 50, 100);
    QCOMPARE(PulseSinkVolume::level(muted), 50);
    QCOMPARE(muted.values[1], muted.values[0]);
}

void VolumeKeysTest::alsaCapStep()
{
    const int cap70 = AlsaVolume::capStepOf(70, fiio);
    QVERIFY(AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(cap70, fiio)) <= 70.0);
    QVERIFY(AlsaVolume::percentOfDb(AlsaVolume::dbOfStep(cap70 + 1, fiio)) > 70.0);
    QCOMPARE(AlsaVolume::capStepOf(100, fiio), 120);
    QCOMPARE(AlsaVolume::capStepOf(50, fiio), 83);

    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned), 10);
    QSignalSpy levels(&volume, &AlsaVolume::levelChanged);
    QVERIFY(volume.open(QStringLiteral("B")));
    QVERIFY(!volume.setCap(70));
    QVERIFY(mixer->writes.isEmpty());
    for (int i = 0; i < 20; ++i) {
        volume.pressed(5);
        QTest::qWait(15);
    }
    QCOMPARE(volume.step(), cap70);
    QTRY_COMPARE(mixer->writes.last(), long(cap70));
    const int writes = mixer->writes.size();
    volume.pressed(5);
    QTest::qWait(30);
    QCOMPARE(mixer->writes.size(), writes);
    QVERIFY(levels.last().at(0).toInt() <= 70);
    volume.setPercent(95);
    QCOMPARE(volume.step(), cap70);
    volume.setPercent(40);
    QCOMPARE(qRound(AlsaVolume::percentOfDb(volume.stepDb())), 40);
    QTRY_COMPARE(mixer->writes.last(), long(volume.step()));
}

void VolumeKeysTest::routedPulseCapAndSet()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    AlsaVolume alsa(std::make_unique<FakeMixer>(), 10);
    auto *pulse = new FakeSink;
    RoutedSinkVolume routed(pulse, &dac, &alsa, 50);
    routed.setCap(70);
    routed.set(90);
    QCOMPARE(pulse->caps, QList<int>({70}));
    QCOMPARE(pulse->sets, QList<int>({90}));
    QCOMPARE(alsa.cap(), 70);
    QCOMPARE(RoutedSinkVolume::lastCap(100), 70);
    routed.setCap(55);
    QCOMPARE(RoutedSinkVolume::lastCap(100), 55);
    QVERIFY(QFile::remove(RoutedSinkVolume::capFile()));
    QCOMPARE(RoutedSinkVolume::lastCap(100), 100);
}

void VolumeKeysTest::routedAlsaCap()
{
    Settings settings(nullptr);
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    mixer->value = 110;
    AlsaVolume alsa(std::move(owned), 10);
    auto *keys = new FakeKeys;
    auto *pulse = new FakeSink;
    auto *routed = new RoutedSinkVolume(pulse, &dac, &alsa, 50);
    VolumeKeys volume(keys, routed, &settings);
    routed->setStoreAllowed(true);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed->setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    const int cap70 = AlsaVolume::capStepOf(70, fiio);
    routed->setCap(70);
    QCOMPARE(mixer->writes, QList<long>({long(cap70)}));
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), cap70);
    keys->press(1);
    QTest::qWait(30);
    QCOMPARE(mixer->writes.size(), 1);
    routed->set(100);
    QCOMPARE(alsa.step(), cap70);
    routed->setOwnedStep(QStringLiteral("B"), 118);
    QCOMPARE(alsa.step(), cap70);
    QCOMPARE(routed->ownedStep(QStringLiteral("B")), cap70);
    keys->press(-1);
    QVERIFY(alsa.step() < cap70);
    QTRY_COMPARE(mixer->writes.last(), long(alsa.step()));
    routed->setCap(50);
    QCOMPARE(alsa.step(), AlsaVolume::capStepOf(50, fiio));
    QTRY_COMPARE(mixer->writes.last(), long(AlsaVolume::capStepOf(50, fiio)));
    QTRY_VERIFY(dac.calls.contains(QStringLiteral("sink volume %1 %2").arg(fiioSink).arg(alsa.stepDb())));
    QVERIFY(pulse->steps.isEmpty());
}

void VolumeKeysTest::routedAlsaCapPullsRestoreDown()
{
    FakeDacSink dac;
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume alsa(std::move(owned), 10);
    RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
    routed.setCap(70);
    QVERIFY(alsa.open(QStringLiteral("B")));
    routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
    const int cap70 = AlsaVolume::capStepOf(70, fiio);
    QVERIFY(mixer->writes.isEmpty());
    mixer->foreign(115);
    QTRY_COMPARE(mixer->writes.size(), 1);
    QCOMPARE(mixer->writes.last(), long(80));
    alsa.watchReturn(200);
    mixer->foreign(119);
    QTRY_COMPARE(mixer->writes.last(), long(80));
    mixer->foreign(100);
    QTest::qWait(30);
    alsa.close();
    mixer->value = 117;
    QVERIFY(alsa.open(QStringLiteral("C")));
    QCOMPARE(alsa.step(), cap70);
    QCOMPARE(mixer->writes.last(), long(cap70));
}

void VolumeKeysTest::alsaPullDownBudget()
{
    auto owned = std::make_unique<FakeMixer>();
    FakeMixer *mixer = owned.get();
    AlsaVolume volume(std::move(owned), 10);
    volume.setCap(70);
    QVERIFY(volume.open(QStringLiteral("B")));
    QCOMPARE(volume.step(), 80);
    QVERIFY(mixer->writes.isEmpty());
    for (int i = 0; i < 2 * AlsaVolume::maxRewrites; ++i) {
        mixer->foreign(110 + i % 2);
        QTest::qWait(5);
    }
    QCOMPARE(mixer->writes, QList<long>(AlsaVolume::maxRewrites, 80));
    QVERIFY(mixer->value > 80);
    QTRY_COMPARE_WITH_TIMEOUT(mixer->writes.size(), AlsaVolume::maxRewrites + 1, 2 * AlsaVolume::pullWindowMs);
    QCOMPARE(mixer->writes.last(), long(80));
    QCOMPARE(mixer->value, long(80));
}

void VolumeKeysTest::routedPowerOnOncePerBoot()
{
    const QString bootFile = m_dir->filePath(QStringLiteral("boot_id"));
    const auto writeBoot = [&bootFile](const QByteArray &id) {
        QFile file(bootFile);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(id + '\n');
    };
    writeBoot("boot-one");
    const int cap70 = AlsaVolume::capStepOf(70, fiio);
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        FakeMixer *mixer = owned.get();
        AlsaVolume alsa(std::move(owned), 10);
        QSignalSpy levels(&alsa, &AlsaVolume::levelChanged);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setCap(70);
        routed.setPowerOn(90);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(alsa.step(), cap70);
        QTRY_COMPARE(mixer->writes.last(), long(cap70));
        QCOMPARE(levels.count(), 0);
        QFile marker(RoutedSinkVolume::powerOnMarker());
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(marker.readAll().trimmed(), QByteArray("boot-one"));
        routed.setRoute(DacVolumePort::Route::Pulse, QString(), QString());
        routed.setOwnedStep(QStringLiteral("B"), 60);
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(alsa.step(), 60);
    }
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        FakeMixer *mixer = owned.get();
        mixer->value = 60;
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setPowerOn(90);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(alsa.step(), 60);
        QTest::qWait(30);
        QVERIFY(mixer->writes.isEmpty());
    }
    writeBoot("boot-two");
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setPowerOn(40);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(qRound(AlsaVolume::percentOfDb(alsa.stepDb())), 40);
    }
    writeBoot("boot-three");
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        FakeMixer *mixer = owned.get();
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(alsa.step(), 80);
        QTest::qWait(30);
        QVERIFY(mixer->writes.isEmpty());
        QFile marker(RoutedSinkVolume::powerOnMarker());
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(marker.readAll().trimmed(), QByteArray("boot-three"));
    }
    const auto writeSystem = [](const QByteArray &text) {
        QFile file(RoutedSinkVolume::systemPowerOnMarker());
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text);
    };
    writeBoot("boot-four");
    writeSystem("boot-four\n" + fiioSink.toLatin1() + '\n');
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        FakeMixer *mixer = owned.get();
        mixer->value = 60;
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setPowerOn(90);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(alsa.step(), 60);
        QTest::qWait(30);
        QVERIFY(mixer->writes.isEmpty());
        QFile marker(RoutedSinkVolume::powerOnMarker());
        QVERIFY(marker.open(QIODevice::ReadOnly));
        QCOMPARE(marker.readAll().trimmed(), QByteArray("boot-four"));
    }
    writeBoot("boot-five");
    writeSystem("boot-five\nalsa_output.platform-sound.HiFi__Speaker__sink\n");
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setPowerOn(40);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(qRound(AlsaVolume::percentOfDb(alsa.stepDb())), 40);
    }
    writeBoot("boot-six");
    writeSystem("boot-five\n" + fiioSink.toLatin1() + '\n');
    {
        FakeDacSink dac;
        auto owned = std::make_unique<FakeMixer>();
        AlsaVolume alsa(std::move(owned), 10);
        RoutedSinkVolume routed(new FakeSink, &dac, &alsa, 50);
        routed.setPowerOn(40);
        QVERIFY(alsa.open(QStringLiteral("B")));
        routed.setRoute(DacVolumePort::Route::Alsa, QStringLiteral("B"), fiioSink);
        QCOMPARE(qRound(AlsaVolume::percentOfDb(alsa.stepDb())), 40);
    }
}

QTEST_GUILESS_MAIN(VolumeKeysTest)
#include "tst_volumekeys.moc"
