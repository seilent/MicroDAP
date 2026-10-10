#include "AlsaVolume.h"
#include "OutputFakes.h"
#include "Settings.h"
#include "VolumeKeys.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
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

using namespace casdap;

namespace {

constexpr int retry = 50;

const MixerRange fiio{0, 120, -60.0, 0.0};

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

    void report(int level)
    {
        emit levelChanged(level);
    }

    QList<int> steps;
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
    void alsaPressAtLimitSkipsWrite();
    void alsaMuteFloorMapped();
    void alsaNonLinearRejected();
    void dacCardOf_data();
    void dacCardOf();
    void bluezInputMatched();
    void dacHelpersFailWithoutServer();
    void dacFailuresDroppedWithObject();

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
    qputenv("CASDAP_DEV_INPUT", QFile::encodeName(m_dir->filePath(QStringLiteral("dev"))));
    qputenv("CASDAP_SYS_INPUT", QFile::encodeName(m_dir->filePath(QStringLiteral("sys"))));
    qputenv("CASDAP_VOLUME_NAMES", "casdap-test-up:casdap-test-down");
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
    addNode(QStringLiteral("event5"), QStringLiteral("casdap-test-down"));
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
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
    addNode(QStringLiteral("event7"), QStringLiteral("casdap-test-down"));
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
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
    addNode(QStringLiteral("event3"), QStringLiteral("casdap-test-up"));
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
    Settings settings(m_dir->filePath(QStringLiteral("settings.ini")));
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
    const QString path = m_dir->filePath(QStringLiteral("settings.ini"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("[General]\nvolumeStep=7\n");
    file.close();
    Settings settings(path);
    auto *keys = new FakeKeys;
    auto *sink = new FakeSink;
    VolumeKeys volume(keys, sink, &settings);
    keys->press(1);
    QCOMPARE(sink->steps, QList<int>({5}));
}

void VolumeKeysTest::levelForwarded()
{
    Settings settings(m_dir->filePath(QStringLiteral("settings.ini")));
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
    QVERIFY(!PulseSinkVolume::isBluezInput(proplistOf({{"node.name", "casdap-player"}}).get()));
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
    sink.setDefaultSink(QStringLiteral("alsa_output.usb-FIIO"));
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

QTEST_GUILESS_MAIN(VolumeKeysTest)
#include "tst_volumekeys.moc"
