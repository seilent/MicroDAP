#include "Settings.h"
#include "VolumeKeys.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace casdap;

namespace {

constexpr int retry = 50;

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

QTEST_GUILESS_MAIN(VolumeKeysTest)
#include "tst_volumekeys.moc"
