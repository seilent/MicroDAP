#include "DacEq.h"
#include "Eq.h"
#include "FakeUsbIo.h"
#include "OutputFakes.h"
#include "Settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

#include <cerrno>
#include <chrono>
#include <memory>
#include <thread>

using namespace microdap;

namespace {

constexpr int waitMs = 5000;
const QString usb = QStringLiteral("usb");
const QString fiioSink = QStringLiteral("alsa_output.usb-FIIO_SNOWSKY_TINY_B_000000000000-01.analog-stereo");

bool writeFile(const QString &path, const QByteArray &text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(text) == text.size();
}

bool link(const QString &target, const QString &path)
{
    return QDir().mkpath(QFileInfo(path).absolutePath()) && QFile::link(target, path);
}

FiioPaths pathsIn(const QString &root)
{
    FiioPaths paths;
    paths.runDir = (root + QStringLiteral("/run")).toStdString();
    paths.runDirExists = QDir().mkpath(root + QStringLiteral("/run"));
    paths.stateDir = (root + QStringLiteral("/state/microdap")).toStdString();
    paths.configDir = (root + QStringLiteral("/config/microdap")).toStdString();
    paths.sysRoot = (root + QStringLiteral("/sys")).toStdString();
    paths.asoundDir = (root + QStringLiteral("/asound")).toStdString();
    return paths;
}

DacEq::Bounds testBounds()
{
    DacEq::Bounds bounds;
    bounds.coalesceMs = 150;
    bounds.firstResolveMs = 300;
    bounds.gateRetryMs = 200;
    bounds.worker.driverWaitMs = 300;
    bounds.worker.lockMs = 200;
    bounds.worker.retryMs = 100;
    return bounds;
}

EqProfile testProfile(double gain = 3.0)
{
    EqProfile profile = EqProfile::flat();
    profile.bands[0].enabled = true;
    profile.bands[0].type = EqBand::Peak;
    profile.bands[0].freq = 1000.0;
    profile.bands[0].freqIndex = -1;
    profile.bands[0].gain = gain;
    profile.bands[0].q = 1.41;
    return profile;
}

QString frameText(const std::vector<uint8_t> &data)
{
    if (data.size() < 7)
        return {};
    const bool set = data[1] == 0xAA;
    const int len = data[6];
    const QByteArray payload(reinterpret_cast<const char *>(data.data() + 7), len);
    QString text = QStringLiteral("%1 %2").arg(set ? QStringLiteral("set") : QStringLiteral("get"))
                       .arg(data[5], 2, 16, QLatin1Char('0'));
    if (!payload.isEmpty())
        text += QLatin1Char(' ') + QString::fromLatin1(payload.toHex());
    return text;
}

QMutex captureMutex;
QStringList *capturedLines = nullptr;
QtMessageHandler previousHandler = nullptr;

void captureLine(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&captureMutex);
        if (capturedLines)
            capturedLines->append(message);
        previous = previousHandler;
    }
    if (previous)
        previous(type, context, message);
}

struct LogCapture
{
    LogCapture()
    {
        QMutexLocker locker(&captureMutex);
        capturedLines = &messages;
        previousHandler = qInstallMessageHandler(captureLine);
    }

    ~LogCapture()
    {
        QMutexLocker locker(&captureMutex);
        qInstallMessageHandler(previousHandler);
        capturedLines = nullptr;
    }

    int count(const QString &line)
    {
        QMutexLocker locker(&captureMutex);
        return int(messages.count(line));
    }

    QStringList messages;
};

struct Rig
{
    explicit Rig(DacEq::Bounds bounds = testBounds())
        : paths(pathsIn(root.path()))
    {
        qputenv("MICRODAP_EQ_DIR", (root.path() + QStringLiteral("/eqconfig")).toUtf8());
        sys = root.path() + QStringLiteral("/sys");
        devPath = sys + QStringLiteral("/devices/platform/xhci/3-1");
        iface = devPath + QStringLiteral("/3-1:1.0");
        ready = plug();
        settings = std::make_unique<Settings>(root.path() + QStringLiteral("/settings.ini"));
        eq = std::make_unique<EqControl>(settings.get());
        auto owned = std::make_unique<FakeUsbIo>();
        fake = owned.get();
        DacEq::Ports ports;
        ports.eq = eq.get();
        ports.dac = &sink;
        ports.player = &player;
        ports.usb = std::move(owned);
        ports.paths = paths;
        ports.releasing = [this] { return releasing; };
        dac = std::make_unique<DacEq>(bounds, std::move(ports));
        QObject::connect(eq.get(), &EqControl::profileChanged, dac.get(), &DacEq::onProfileChanged);
        QObject::connect(eq.get(), &EqControl::profileApplied, eq.get(),
                         [this](const EqProfile &profile) { applied.append(profile); });
        DacCard card;
        card.cardIndex = 7;
        card.alsaCard = 1;
        card.reserveName = QStringLiteral("Audio1");
        sink.sinkCards.insert(fiioSink, card);
    }

    ~Rig()
    {
        dac.reset();
    }

    bool plug()
    {
        return writeFile(devPath + QStringLiteral("/idVendor"), "2972\n")
               && writeFile(devPath + QStringLiteral("/idProduct"), "0149\n")
               && writeFile(devPath + QStringLiteral("/busnum"), "3\n")
               && writeFile(devPath + QStringLiteral("/devnum"), "2\n")
               && (QFileInfo::exists(sys + QStringLiteral("/bus/usb/devices/3-1"))
                   || link(devPath, sys + QStringLiteral("/bus/usb/devices/3-1")))
               && writeFile(iface + QStringLiteral("/bInterfaceClass"), "03\n")
               && QDir().mkpath(iface + QStringLiteral("/ep_05")) && QDir().mkpath(iface + QStringLiteral("/ep_86"))
               && QDir().mkpath(devPath + QStringLiteral("/3-1:1.1"))
               && (QFileInfo::exists(sys + QStringLiteral("/class/sound/card1/device"))
                   || link(devPath + QStringLiteral("/3-1:1.1"), sys + QStringLiteral("/class/sound/card1/device")))
               && writeFile(asound(QStringLiteral("card1/id")), "B\n") && pcm("closed\n");
    }

    bool idGone()
    {
        return QFile::remove(asound(QStringLiteral("card1/id")));
    }

    bool idBack()
    {
        return writeFile(asound(QStringLiteral("card1/id")), "B\n");
    }

    bool otherCard()
    {
        const QString other = sys + QStringLiteral("/devices/platform/xhci/3-2");
        return writeFile(other + QStringLiteral("/idVendor"), "1234\n")
               && writeFile(other + QStringLiteral("/idProduct"), "5678\n") && QDir().mkpath(other + QStringLiteral("/3-2:1.1"))
               && link(other + QStringLiteral("/3-2:1.1"), sys + QStringLiteral("/class/sound/card2/device"))
               && writeFile(asound(QStringLiteral("card2/id")), "X\n");
    }

    bool pcm(const QByteArray &text)
    {
        return writeFile(asound(QStringLiteral("card1/pcm0p/sub0/status")), text);
    }

    QString asound(const QString &rest) const
    {
        return root.path() + QStringLiteral("/asound/") + rest;
    }

    bool bindDriver() const
    {
        const QString driver = sys + QStringLiteral("/bus/usb/drivers/usbhid");
        return QDir().mkpath(driver) && link(driver, iface + QStringLiteral("/driver"));
    }

    bool unbindDriver() const
    {
        return QFile::remove(iface + QStringLiteral("/driver"));
    }

    bool rawSet(uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        if (fake->open("/dev/bus/usb/003/002") != 0 || fake->claim(0) != 0)
            return false;
        std::vector<uint8_t> out = frameSet(1, cmd, payload);
        std::vector<uint8_t> in(fiioPacketSize);
        const bool ok = fake->bulk(0x05, out.data(), int(out.size()), 200) == int(out.size())
                        && fake->bulk(0x86, in.data(), fiioPacketSize, 500) == fiioPacketSize;
        fake->release(0);
        fake->close();
        return ok;
    }

    bool wedge()
    {
        if (fake->open("/dev/bus/usb/003/002") != 0 || fake->claim(0) != 0)
            return false;
        std::vector<uint8_t> out = frameGet(1, fiioCmdBand, {1});
        fake->bulk(0x05, out.data(), int(out.size()), 200);
        fake->bulk(0x05, out.data(), int(out.size()), 200);
        fake->release(0);
        fake->close();
        return fake->wedged();
    }

    std::size_t mark() const
    {
        return fake->calls().size();
    }

    QStringList outs(std::size_t from = 0) const
    {
        QStringList list;
        const std::vector<FakeUsbIo::Call> calls = fake->calls();
        for (std::size_t i = from; i < calls.size(); ++i) {
            if (calls[i].op == FakeUsbIo::Op::Out)
                list.append(frameText(calls[i].data));
        }
        return list;
    }

    int countSince(std::size_t from, FakeUsbIo::Op op) const
    {
        int n = 0;
        const std::vector<FakeUsbIo::Call> calls = fake->calls();
        for (std::size_t i = from; i < calls.size(); ++i)
            n += calls[i].op == op ? 1 : 0;
        return n;
    }

    void seed(const QString &level, bool effective, const QString &active = usb)
    {
        dac->setLevel(level);
        dac->onEffectiveChanged(effective);
        dac->onActiveOutputChanged(active);
        dac->onPlayStateChanged();
    }

    void holdLevel()
    {
        seed(QStringLiteral("bitperfect"), true);
        dac->start();
        dac->onFirstApplied();
        dac->onCardChanged(QStringLiteral("B"), 1);
    }

    bool takeOn()
    {
        eq->setCustom(usb, testProfile());
        holdLevel();
        return QTest::qWaitFor([this] { return dac->status() == QLatin1StringView("on"); }, waitMs);
    }

    bool idle(int ms = 300)
    {
        QTest::qWait(ms);
        return true;
    }

    QTemporaryDir root;
    FiioPaths paths;
    QString sys;
    QString devPath;
    QString iface;
    bool ready = false;
    bool releasing = false;
    std::unique_ptr<Settings> settings;
    std::unique_ptr<EqControl> eq;
    FakeDacSink sink;
    FakePlayer player;
    FakeUsbIo *fake = nullptr;
    std::unique_ptr<DacEq> dac;
    QList<EqProfile> applied;
};

const QStringList takeFromBypass{
    QStringLiteral("get 0b"),
    QStringLiteral("get 16"),
    QStringLiteral("get 30 a2"),
    QStringLiteral("set 16 a2"),
    QStringLiteral("get 17"),
    QStringLiteral("get 15 00"),
    QStringLiteral("get 15 01"),
    QStringLiteral("get 15 02"),
    QStringLiteral("get 15 03"),
    QStringLiteral("get 15 04"),
    QStringLiteral("get 15 05"),
    QStringLiteral("get 15 06"),
    QStringLiteral("get 15 07"),
    QStringLiteral("get 15 08"),
    QStringLiteral("get 15 09"),
    QStringLiteral("set 30 a24d4943524f444150"),
    QStringLiteral("set 17 0000"),
};

int bandSets(const QStringList &outs)
{
    int n = 0;
    for (const QString &line : outs)
        n += line.startsWith(QLatin1StringView("set 15 ")) ? 1 : 0;
    return n;
}

}

class DacEqTest : public QObject
{
    Q_OBJECT

private slots:
    void startLevelOffForces();
    void startLevelSetWaitsForResolve();
    void startLevelSetIgnoresWordApplied();
    void startDacHeldBeforeLoad();
    void takeOrder();
    void effectiveOffBypass();
    void effectiveMissingBypass();
    void effectiveUnsupported();
    void updateGetsPresetFirst();
    void coalescing();
    void abandonWithinOneTransaction();
    void slotReleased();
    void releasingThenTake();
    void dacHeldKeptAfterFailedBypass();
    void dacHeldClearedOn240();
    void dacHeldClearedCardGone();
    void generationPresentKeeps();
    void failedRetriesOnChange();
    void sinkAppearedForces();
    void cardRemovedIdPresent();
    void cardRemovedIdGone();
    void trackerIgnoresWhileEffective();
    void rearmOnEffectiveFalse();
    void forceOverPreset0();
    void replugFullTake();
    void resetGatePausedOnUsb();
    void resetGateExHeld();
    void resetGateTwice();
    void resetAfterStopped();
    void resetCountClearedOnGone();
    void resetFailedNotRetried();
    void orderDriverWaitThenLock();
    void lockPeerRetryThenFail();
    void claimRetryOnce();
    void startReleaseNothingSync();
    void startReleaseAfterAck();
    void quitSendsBypassThenJoins();
    void quitJoinBlocked();
    void guiNeverBlocks();
    void switchingAllowsReleaseOnly();
    void profileChangeDuringTake();
    void startScanUnplug();
    void effectiveRecordUnplug();
    void rearmPresenceCheck();
    void replugAfterFailureRunsX();
    void failedBypassKeepsStatus();
    void effectiveChangeReruns();
    void switchEndReruns();
    void overflowGateOpenResetsFirst();
    void resetRefusedRearms();
    void takingThenNotEffective();
    void takingThenOutputAway();
    void takingThenReleaseForSwitch();
    void takingThenQuit();
    void dacReturnsNotEffective();
    void sinkAppearedNewRecord();
    void bypassUnlessMirror240();
    void cardGoneDuringReleaseQueued();
    void effectiveNoCardLogged();
    void refusedRearmsCapped();
    void gateRetriesWhilePending();
};

void DacEqTest::startLevelOffForces()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QVERIFY(rig.dac->dacHeld());
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(), QStringList{QStringLiteral("get 16")});
    QVERIFY(!rig.dac->dacHeld());
    QVERIFY(!rig.eq->dacHeld());
    QCOMPARE(rig.dac->mode(), QStringLiteral("bypass"));
}

void DacEqTest::startLevelSetIgnoresWordApplied()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("bitperfect"), false, QStringLiteral("speaker"));
    rig.dac->start();
    rig.dac->onApplied(QStringLiteral("spk-gain-102"), true);
    rig.dac->onApplied(QStringLiteral("hp-gain-normal"), false);
    QTest::qWait(150);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    rig.dac->onEffectiveChanged(true);
    rig.dac->onActiveOutputChanged(usb);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTest::qWait(150);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    rig.dac->onApplied(usb, true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    const QStringList outs = rig.outs();
    QCOMPARE(outs.value(0), QStringLiteral("get 0b"));
    QVERIFY(!outs.contains(QStringLiteral("set 16 f0")));
    const int opens = rig.fake->count(FakeUsbIo::Op::Open);
    rig.dac->onApplied(usb, true);
    rig.dac->onFirstApplied();
    QTest::qWait(250);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), opens);
}

void DacEqTest::startLevelSetWaitsForResolve()
{
    {
        Rig rig;
        QVERIFY(rig.ready);
        rig.eq->setCustom(usb, testProfile());
        rig.seed(QStringLiteral("bitperfect"), true);
        rig.dac->start();
        rig.dac->onCardChanged(QStringLiteral("B"), 1);
        QTest::qWait(150);
        QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
        rig.dac->onFirstApplied();
        QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
        const QStringList outs = rig.outs();
        QCOMPARE(outs.value(0), QStringLiteral("get 0b"));
        QVERIFY(!outs.contains(QStringLiteral("set 16 f0")));
    }
    {
        Rig rig;
        QVERIFY(rig.ready);
        rig.eq->setCustom(usb, testProfile());
        rig.seed(QStringLiteral("bitperfect"), false, QStringLiteral("speaker"));
        rig.dac->start();
        QTest::qWait(100);
        QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
        rig.dac->onFirstApplied();
        QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
        QCOMPARE(rig.outs(), QStringList{QStringLiteral("get 16")});
    }
    {
        Rig rig;
        QVERIFY(rig.ready);
        QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
        const std::size_t from = rig.mark();
        rig.seed(QStringLiteral("exclusive"), false, QStringLiteral("speaker"));
        QElapsedTimer clock;
        clock.start();
        rig.dac->start();
        QTest::qWait(150);
        QCOMPARE(rig.countSince(from, FakeUsbIo::Op::Open), 0);
        QTRY_COMPARE_WITH_TIMEOUT(rig.fake->preset(), fiioBypass, waitMs);
        QVERIFY(clock.elapsed() >= 290);
        QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    }
}

void DacEqTest::startDacHeldBeforeLoad()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("bitperfect"), false, QStringLiteral("speaker"));
    QSignalSpy held(rig.dac.get(), &DacEq::dacHeldChanged);
    rig.dac->start();
    QCOMPARE(held.count(), 1);
    QVERIFY(rig.dac->dacHeld());
    QVERIFY(rig.eq->dacHeld());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    rig.eq->setActiveOutput(usb);
    QVERIFY(!rig.applied.isEmpty());
    QVERIFY(rig.applied.last() == EqProfile::off());
    rig.dac->onFirstApplied();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    QVERIFY(rig.applied.last() == testProfile());
}

void DacEqTest::takeOrder()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    QStringList expected = takeFromBypass;
    expected.append(QStringLiteral("set 15 00001e03e8008d00"));
    const QStringList outs = rig.outs();
    QCOMPARE(outs.mid(0, expected.size()), expected);
    QCOMPARE(outs.size(), expected.size() + 9);
    QCOMPARE(bandSets(outs), 10);
    for (int i = 1; i < fiioBandCount; ++i)
        QVERIFY(outs.value(expected.size() + i - 1).startsWith(QStringLiteral("set 15 0%1").arg(i)));
    QCOMPARE(rig.fake->preset(), uint8_t(162));
    QCOMPARE(QString::fromStdString(rig.fake->name(162)), QStringLiteral("MICRODAP"));
    QVERIFY(QFileInfo::exists(QString::fromStdString(rig.paths.configDir) + QStringLiteral("/fiio-2972-0149-slot162-1.txt")));
    QVERIFY(!QFileInfo::exists(QString::fromStdString(fiioMarkerPath(rig.paths))));
    QCOMPARE(rig.dac->mode(), QStringLiteral("slot"));
    QVERIFY(rig.dac->dacHeld());
}

void DacEqTest::effectiveOffBypass()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0xA2}));
    const std::size_t from = rig.mark();
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
    QVERIFY(!rig.dac->dacHeld());
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from).size(), 1);
}

void DacEqTest::effectiveMissingBypass()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.settings->setEq(usb, rig.root.path() + QStringLiteral("/eq/Gone.txt"));
    QVERIFY(rig.eq->missing(usb));
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(), QStringList{QStringLiteral("set 16 f0")});
}

void DacEqTest::effectiveUnsupported()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.otherCard());
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("bitperfect"), true);
    rig.dac->start();
    rig.dac->onFirstApplied();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Open), 0, 100);
    rig.dac->onCardChanged(QStringLiteral("X"), 2);
    QCOMPARE(rig.dac->status(), QStringLiteral("unsupported"));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(), QStringList{QStringLiteral("get 16")});
    QCOMPARE(rig.dac->status(), QStringLiteral("unsupported"));
}

void DacEqTest::updateGetsPresetFirst()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    rig.eq->stepGain(usb, 0, 1);
    QCOMPARE(rig.dac->status(), QStringLiteral("applying"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs(from).size(), 2, waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 15 00001f03e8008d00")}));
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
}

void DacEqTest::coalescing()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    for (int i = 0; i < 5; ++i) {
        rig.eq->stepGain(usb, 0, 1);
        QTest::qWait(20);
    }
    QTest::qWait(60);
    QCOMPARE(rig.countSince(from, FakeUsbIo::Op::Open), 0);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 15 00002303e8008d00")}));
}

void DacEqTest::abandonWithinOneTransaction()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QCOMPARE(rig.dac->status(), QStringLiteral("applying"));
    rig.eq->cycleProfile(usb, 1);
    QCOMPARE(rig.eq->key(usb), QStringLiteral("off"));
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(), (QStringList{QStringLiteral("get 0b"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::slotReleased()
{
    {
        Rig rig;
        QVERIFY(rig.ready);
        QVERIFY(rig.takeOn());
        const std::size_t from = rig.mark();
        rig.eq->cycleProfile(usb, 1);
        QCOMPARE(rig.dac->mode(), QStringLiteral("releasing"));
        QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
        QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
        QCOMPARE(rig.dac->mode(), QStringLiteral("bypass"));
    }
    {
        Rig rig;
        QVERIFY(rig.ready);
        QVERIFY(rig.takeOn());
        const std::size_t from = rig.mark();
        rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
        rig.dac->onEffectiveChanged(false);
        QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
        QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
        QVERIFY(rig.idle());
        QCOMPARE(rig.outs(from).size(), 1);
    }
}

void DacEqTest::releasingThenTake()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.dac->onEffectiveChanged(false);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    rig.dac->onEffectiveChanged(true);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QCOMPARE(rig.dac->mode(), QStringLiteral("releasing"));
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(bandSets(rig.outs(from)), 10, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    const QStringList outs = rig.outs(from);
    QCOMPARE(outs.value(0), QStringLiteral("set 16 f0"));
    QCOMPARE(outs.value(1), QStringLiteral("get 16"));
    QVERIFY(outs.contains(QStringLiteral("set 16 a2")));
    QCOMPARE(bandSets(outs), 10);
}

void DacEqTest::dacHeldKeptAfterFailedBypass()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setOpenError(-EACCES);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Open), 2, waitMs);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 2);
    QCOMPARE(rig.dac->reason(), QStringLiteral("permission"));
    QCOMPARE(rig.dac->mode(), QStringLiteral("failed"));
    QVERIFY(rig.dac->dacHeld());
    QVERIFY(rig.eq->dacHeld());
}

void DacEqTest::dacHeldClearedOn240()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setActiveOutput(usb);
    QVERIFY(rig.takeOn());
    QVERIFY(rig.applied.last() == EqProfile::off());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.dac->setLevel(QStringLiteral("off"));
    rig.dac->onEffectiveChanged(false);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QVERIFY(rig.dac->dacHeld());
    QVERIFY(rig.applied.last() == EqProfile::off());
    held->release();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QVERIFY(rig.applied.last() == testProfile());
}

void DacEqTest::dacHeldClearedCardGone()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.eq->stepGain(usb, 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    const std::size_t from = rig.mark();
    const std::vector<uint8_t> band = rig.fake->band(162, 0);
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, false);
    QVERIFY(!rig.dac->dacHeld());
    QVERIFY(!rig.eq->dacHeld());
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QCOMPARE(rig.dac->mode(), QStringLiteral("none"));
    held->release();
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("get 16")});
    QCOMPARE(rig.fake->band(162, 0), band);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::generationPresentKeeps()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.eq->stepGain(usb, 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, true);
    QVERIFY(rig.dac->dacHeld());
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->band(162, 0), (std::vector<uint8_t>{0x00, 0x00, 0x1f, 0x03, 0xe8, 0x00, 0x8d, 0x00}));
    const std::size_t from = rig.mark();
    rig.eq->stepGain(usb, 0, 1);
    QTRY_COMPARE_WITH_TIMEOUT(bandSets(rig.outs(from)), 10, waitMs);
    const QStringList outs = rig.outs(from);
    QCOMPARE(outs.mid(0, 4), (QStringList{QStringLiteral("get 16"), QStringLiteral("get 0b"), QStringLiteral("get 16"),
                                         QStringLiteral("get 30 a2")}));
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
}

void DacEqTest::failedRetriesOnChange()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Claim), 4, waitMs);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Claim), 4);
    QCOMPARE(rig.dac->reason(), QStringLiteral("claim"));
    rig.fake->setClaimError(0);
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    rig.fake->setClaimError(-EBUSY);
    rig.eq->stepGain(usb, 0, 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    rig.fake->setClaimError(0);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    rig.dac->setLevel(QStringLiteral("exclusive"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.dac->reason(), QString());
}

void DacEqTest::sinkAppearedForces()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
    std::size_t from = rig.mark();
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->preset(), fiioBypass, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    from = rig.mark();
    rig.dac->onSinkAppeared(56, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs(from).size(), 1, waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("get 16")});
    from = rig.mark();
    rig.dac->onSinkAppeared(57, QStringLiteral("alsa_output.platform-sound.HiFi__Speaker__sink"), 0);
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from).size(), 0);
}

void DacEqTest::cardRemovedIdPresent()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QVERIFY(rig.dac->dacHeld());
    std::size_t from = rig.mark();
    rig.dac->onCardRemoved(7);
    QVERIFY(rig.dac->dacHeld());
    held->release();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    QCOMPARE(rig.dac->status(), QStringLiteral("flat"));
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from).size(), 1);
    from = rig.mark();
    rig.dac->onSinkAppeared(58, fiioSink, 8);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs(from).size(), 1, waitMs);
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("get 16")});
}

void DacEqTest::cardRemovedIdGone()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs().size(), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.idGone());
    rig.dac->onCardRemoved(7);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QCOMPARE(rig.dac->mode(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::trackerIgnoresWhileEffective()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs().size(), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    rig.dac->setLevel(QStringLiteral("bitperfect"));
    rig.dac->onEffectiveChanged(true);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QVERIFY(rig.idGone());
    const std::size_t from = rig.mark();
    rig.dac->onCardRemoved(7);
    rig.dac->onSinkAppeared(59, fiioSink, 9);
    QVERIFY(rig.idle());
    QCOMPARE(rig.dac->status(), QStringLiteral("on"));
    QVERIFY(rig.dac->dacHeld());
    QCOMPARE(rig.outs(from).size(), 0);
}

void DacEqTest::rearmOnEffectiveFalse()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs().size(), 2, waitMs);
    rig.dac->setLevel(QStringLiteral("bitperfect"));
    rig.dac->onEffectiveChanged(true);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    rig.dac->onCardRemoved(7);
    QCOMPARE(rig.dac->status(), QStringLiteral("on"));
    rig.dac->setLevel(QStringLiteral("off"));
    rig.dac->onEffectiveChanged(false);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.idGone());
    rig.dac->onCardRemoved(7);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::forceOverPreset0()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
    const std::vector<uint8_t> slotBand = rig.fake->band(162, 3);
    const std::size_t from = rig.mark();
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QCOMPARE(QString::fromStdString(rig.fake->name(162)), QStringLiteral("FH3"));
    QCOMPARE(rig.fake->band(162, 3), slotBand);
}

void DacEqTest::replugFullTake()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    rig.dac->onCardGoneChanged(true);
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, false);
    rig.dac->onEffectiveChanged(false);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    rig.fake->simulateReplug();
    QVERIFY(rig.idle(100));
    const std::size_t from = rig.mark();
    QCOMPARE(rig.countSince(from, FakeUsbIo::Op::Open), 0);
    rig.dac->onCardGoneChanged(false);
    rig.dac->onEffectiveChanged(true);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    const QStringList outs = rig.outs(from);
    QCOMPARE(outs.mid(0, 3), (QStringList{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2")}));
    QCOMPARE(bandSets(outs), 10);
    QVERIFY(outs.contains(QStringLiteral("set 17 0000")));
}

void DacEqTest::resetGatePausedOnUsb()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.player.status = PlayerPort::PlayState::Paused;
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QCOMPARE(rig.dac->reason(), QStringLiteral("overflow"));
    rig.dac->onPlayStateChanged();
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    QVERIFY(rig.dac->dacHeld());
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 1, waitMs);
}

void DacEqTest::resetGateExHeld()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.eq->setCustom(usb, testProfile());
    rig.dac->onExHeldChanged(true);
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    rig.dac->onPlayStateChanged();
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    rig.releasing = true;
    rig.dac->onExHeldChanged(false);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    rig.releasing = false;
    QVERIFY(rig.pcm("RUNNING\n"));
    rig.dac->onPlayStateChanged();
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    QVERIFY(rig.pcm("closed\n"));
    rig.dac->onPlayStateChanged();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 1, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
}

void DacEqTest::resetGateTwice()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> resetHeld = rig.fake->hold(FakeUsbIo::Op::Reset);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(resetHeld->waitEntered(1), waitMs);
    const std::shared_ptr<FakeUsbIo::Hold> openHeld = rig.fake->hold(FakeUsbIo::Op::Open);
    resetHeld->release();
    QTRY_VERIFY_WITH_TIMEOUT(openHeld->waitEntered(1), waitMs);
    QVERIFY(rig.wedge());
    openHeld->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QCOMPARE(rig.dac->reason(), QStringLiteral("overflow"));
    rig.dac->onPlayStateChanged();
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
}

void DacEqTest::resetAfterStopped()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.player.status = PlayerPort::PlayState::Paused;
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    const std::size_t from = rig.mark();
    rig.player.status = PlayerPort::PlayState::Stopped;
    rig.dac->onPlayStateChanged();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    QVERIFY(!rig.fake->wedged());
    const QStringList outs = rig.outs(from);
    QCOMPARE(outs.value(0), QStringLiteral("get 0b"));
    QCOMPARE(bandSets(outs), 10);
    QCOMPARE(rig.fake->preset(), uint8_t(162));
}

void DacEqTest::resetCountClearedOnGone()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    rig.dac->onCardGoneChanged(true);
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, false);
    rig.fake->simulateReplug();
    QVERIFY(rig.wedge());
    rig.dac->onCardGoneChanged(false);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
}

void DacEqTest::resetFailedNotRetried()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.fake->setResetError(-EIO);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 1, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->reason(), QStringLiteral("device"), waitMs);
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    rig.dac->onPlayStateChanged();
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    rig.dac->onExHeldChanged(false);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    QVERIFY(rig.dac->dacHeld());
}

void DacEqTest::orderDriverWaitThenLock()
{
    DacEq::Bounds bounds = testBounds();
    bounds.worker.driverWaitMs = 2000;
    Rig rig(bounds);
    QVERIFY(rig.ready);
    QVERIFY(rig.bindDriver());
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTest::qWait(200);
    FiioLock peer(rig.paths);
    QVERIFY(peer.acquire(0, std::nullopt, [] { return false; }));
    peer.release();
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    QVERIFY(rig.unbindDriver());
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);
}

void DacEqTest::lockPeerRetryThenFail()
{
    Rig rig;
    QVERIFY(rig.ready);
    FiioLock peer(rig.paths);
    QVERIFY(peer.acquire(0, std::nullopt, [] { return false; }));
    rig.seed(QStringLiteral("off"), false);
    QElapsedTimer clock;
    clock.start();
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QVERIFY(clock.elapsed() >= 450);
    QCOMPARE(rig.dac->reason(), QStringLiteral("lock"));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    QVERIFY(rig.dac->dacHeld());
    peer.release();
}

void DacEqTest::claimRetryOnce()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    QTimer::singleShot(50, rig.dac.get(), [&rig] { rig.fake->setClaimError(0); });
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Claim), 2);
    QCOMPARE(rig.dac->reason(), QString());
}

void DacEqTest::startReleaseNothingSync()
{
    {
        Rig rig;
        QVERIFY(rig.ready);
        rig.seed(QStringLiteral("off"), false);
        rig.dac->start();
        QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
        const std::size_t from = rig.mark();
        bool called = false;
        rig.dac->startRelease([&called] { called = true; });
        QVERIFY(called);
        QVERIFY(rig.idle());
        QCOMPARE(rig.countSince(from, FakeUsbIo::Op::Open), 0);
    }
    {
        Rig rig;
        QVERIFY(rig.idGone());
        rig.seed(QStringLiteral("off"), false);
        rig.dac->start();
        bool called = false;
        rig.dac->startRelease([&called] { called = true; });
        QVERIFY(called);
    }
}

void DacEqTest::startReleaseAfterAck()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    int called = 0;
    rig.dac->startRelease([&called] { ++called; });
    QCOMPARE(called, 0);
    QTRY_COMPARE_WITH_TIMEOUT(called, 1, waitMs);
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
    QVERIFY(!rig.dac->dacHeld());
    rig.eq->stepGain(usb, 0, 1);
    QVERIFY(rig.idle(400));
    QCOMPARE(rig.outs(from).size(), 1);
    QCOMPARE(called, 1);
}

void DacEqTest::quitSendsBypassThenJoins()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    QElapsedTimer clock;
    clock.start();
    rig.dac->quitAndJoin(500);
    QVERIFY(clock.elapsed() < 600);
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
    rig.eq->stepGain(usb, 0, 1);
    rig.dac->onEffectiveChanged(false);
    QVERIFY(rig.idle());
    QCOMPARE(rig.outs(from).size(), 1);
}

void DacEqTest::quitJoinBlocked()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.eq->stepGain(usb, 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QElapsedTimer clock;
    clock.start();
    rig.dac->quitAndJoin(300);
    QVERIFY(clock.elapsed() < 450);
    held->release();
}

void DacEqTest::guiNeverBlocks()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.eq->stepGain(usb, 0, 1);
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QElapsedTimer clock;
    clock.start();
    rig.eq->stepGain(usb, 0, 1);
    rig.dac->onProfileChanged(usb);
    rig.dac->setLevel(QStringLiteral("exclusive"));
    rig.dac->onGenerationChanged(QStringLiteral("B"), 3, true);
    rig.dac->onPlayStateChanged();
    rig.dac->onEffectiveChanged(false);
    rig.dac->onEffectiveChanged(true);
    bool called = false;
    rig.dac->startRelease([&called] { called = true; });
    rig.dac->onSwitchingChanged(false);
    QVERIFY(clock.elapsed() < 50);
    QVERIFY(!called);
    held->release();
    QTRY_VERIFY_WITH_TIMEOUT(called, waitMs);
}

void DacEqTest::switchingAllowsReleaseOnly()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::size_t from = rig.mark();
    rig.dac->onSwitchingChanged(true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->preset(), fiioBypass, waitMs);
    rig.eq->stepGain(usb, 0, 1);
    rig.dac->setLevel(QStringLiteral("exclusive"));
    rig.dac->onGenerationChanged(QStringLiteral("B"), 3, true);
    rig.dac->onEffectiveChanged(false);
    rig.dac->onEffectiveChanged(true);
    QVERIFY(rig.idle(400));
    QCOMPARE(rig.outs(from), QStringList{QStringLiteral("set 16 f0")});
    QVERIFY(!rig.dac->dacHeld());
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    rig.dac->onSwitchingChanged(false);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->preset(), uint8_t(162));
    QCOMPARE(rig.fake->band(162, 0), (std::vector<uint8_t>{0x00, 0x00, 0x1f, 0x03, 0xe8, 0x00, 0x8d, 0x00}));
}

void DacEqTest::profileChangeDuringTake()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    rig.eq->stepGain(usb, 0, 1);
    QTest::qWait(250);
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.fake->band(162, 0), (std::vector<uint8_t>{0x00, 0x00, 0x1f, 0x03, 0xe8, 0x00, 0x8d, 0x00}));
    const QStringList outs = rig.outs();
    QCOMPARE(bandSets(outs), 10);
    QCOMPARE(outs.count(QStringLiteral("get 0b")), 1);
}

void DacEqTest::startScanUnplug()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.idGone());
    rig.dac->onCardRemoved(42);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QCOMPARE(rig.dac->mode(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::effectiveRecordUnplug()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.idGone());
    rig.eq->setCustom(usb, testProfile());
    rig.seed(QStringLiteral("bitperfect"), true);
    rig.dac->start();
    rig.dac->onFirstApplied();
    QCOMPARE(rig.dac->mode(), QStringLiteral("none"));
    QVERIFY(rig.idBack());
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    rig.dac->setLevel(QStringLiteral("off"));
    rig.dac->onEffectiveChanged(false);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.idGone());
    rig.dac->onCardRemoved(42);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::rearmPresenceCheck()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    QVERIFY(rig.idGone());
    const std::size_t from = rig.mark();
    rig.dac->setLevel(QStringLiteral("off"));
    rig.dac->onEffectiveChanged(false);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
    QVERIFY(rig.idle());
    QCOMPARE(rig.countSince(from, FakeUsbIo::Op::Open), 0);
}

void DacEqTest::replugAfterFailureRunsX()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Claim), 4, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QVERIFY(rig.idle());
    rig.fake->setClaimError(0);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
    const std::size_t from = rig.mark();
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::failedBypassKeepsStatus()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setOutError(-EIO);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(rig.outs().contains(QStringLiteral("set 16 f0")), 2 * waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    QCOMPARE(rig.outs(), (QStringList{QStringLiteral("get 0b"), QStringLiteral("set 16 f0")}));
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    QCOMPARE(rig.dac->reason(), QStringLiteral("device"));
    QVERIFY(!rig.eq->dacHeld());
    QVERIFY(rig.idle());
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 2);
}

void DacEqTest::effectiveChangeReruns()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Claim), 4, waitMs);
    QVERIFY(rig.idle());
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    rig.fake->setClaimError(0);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
    const std::size_t from = rig.mark();
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    rig.dac->onEffectiveChanged(false);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QCOMPARE(rig.dac->reason(), QString());
}

void DacEqTest::switchEndReruns()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Claim), 4, waitMs);
    QVERIFY(rig.idle());
    rig.fake->setClaimError(0);
    rig.dac->onSwitchingChanged(true);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.dac->dacHeld(), waitMs);
    rig.dac->onSwitchingChanged(false);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->preset(), uint8_t(162));
}

void DacEqTest::overflowGateOpenResetsFirst()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    const std::vector<FakeUsbIo::Call> calls = rig.fake->calls();
    std::size_t reset = 0;
    for (std::size_t i = 0; i < calls.size(); ++i) {
        if (calls[i].op == FakeUsbIo::Op::Reset)
            reset = i;
    }
    QVERIFY(reset > 0);
    for (std::size_t i = 0; i < reset; ++i) {
        if (calls[i].op == FakeUsbIo::Op::Out)
            QVERIFY(frameText(calls[i].data) != QStringLiteral("set 16 f0"));
    }
}

void DacEqTest::resetRefusedRearms()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    rig.player.status = PlayerPort::PlayState::Paused;
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    const std::shared_ptr<FakeUsbIo::Hold> openHeld = rig.fake->hold(FakeUsbIo::Op::Open);
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    QTRY_VERIFY_WITH_TIMEOUT(openHeld->waitEntered(1), waitMs);
    QVERIFY(rig.pcm("RUNNING\n"));
    openHeld->release();
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    QVERIFY(rig.pcm("closed\n"));
    rig.dac->onPlayStateChanged();
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 1, waitMs);
    QVERIFY(!rig.fake->wedged());
}

void DacEqTest::takingThenNotEffective()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    rig.dac->setLevel(QStringLiteral("off"));
    rig.dac->onEffectiveChanged(false);
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(), (QStringList{QStringLiteral("get 0b"), QStringLiteral("get 16")}));
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::takingThenOutputAway()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.rawSet(fiioCmdPreset, {0xA2}));
    rig.eq->setCustom(usb, testProfile());
    const std::size_t from = rig.mark();
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    rig.dac->onActiveOutputChanged(QStringLiteral("speaker"));
    rig.dac->onEffectiveChanged(false);
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::takingThenReleaseForSwitch()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    int called = 0;
    rig.dac->startRelease([&called] { ++called; });
    QCOMPARE(called, 0);
    QCOMPARE(rig.dac->mode(), QStringLiteral("releasing"));
    held->release();
    QTRY_COMPARE_WITH_TIMEOUT(called, 1, waitMs);
    QCOMPARE(rig.outs(), (QStringList{QStringLiteral("get 0b"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::takingThenQuit()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.eq->setCustom(usb, testProfile());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    rig.holdLevel();
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    std::thread releaser([held] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        held->release();
    });
    QElapsedTimer clock;
    clock.start();
    rig.dac->quitAndJoin(500);
    releaser.join();
    QVERIFY(clock.elapsed() < 600);
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QCOMPARE(rig.outs(), (QStringList{QStringLiteral("get 0b"), QStringLiteral("set 16 f0")}));
}

void DacEqTest::dacReturnsNotEffective()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.outs().size(), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QVERIFY(rig.idGone());
    rig.dac->onCardRemoved(7);
    QCOMPARE(rig.dac->status(), QStringLiteral("none"));
    rig.fake->simulateReplug();
    QVERIFY(rig.idBack());
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x00}));
    const std::size_t from = rig.mark();
    DacCard card = rig.sink.sinkCards.value(fiioSink);
    card.cardIndex = 8;
    rig.sink.sinkCards.insert(fiioSink, card);
    rig.dac->onSinkAppeared(60, fiioSink, 8);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::sinkAppearedNewRecord()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.idGone());
    rig.seed(QStringLiteral("off"), false);
    rig.dac->start();
    QCOMPARE(rig.dac->mode(), QStringLiteral("none"));
    QVERIFY(!rig.dac->dacHeld());
    QVERIFY(rig.idBack());
    QVERIFY(rig.rawSet(fiioCmdPreset, {0x03}));
    const std::size_t from = rig.mark();
    QSignalSpy held(rig.dac.get(), &DacEq::dacHeldChanged);
    rig.dac->onSinkAppeared(55, fiioSink, 7);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(held.count(), 2);
    QCOMPARE(rig.outs(from), (QStringList{QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QVERIFY(!rig.dac->dacHeld());
}

void DacEqTest::bypassUnlessMirror240()
{
    Rig rig;
    QVERIFY(rig.ready);
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("flat"), waitMs);
    QCOMPARE(rig.outs(), QStringList{QStringLiteral("set 16 f0")});
    rig.dac->setLevel(QStringLiteral("exclusive"));
    rig.dac->onGenerationChanged(QStringLiteral("B"), 2, true);
    rig.dac->onProfileChanged(usb);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QVERIFY(rig.idle(400));
    QCOMPARE(rig.outs(), QStringList{QStringLiteral("set 16 f0")});
    QCOMPARE(rig.dac->status(), QStringLiteral("flat"));
}

void DacEqTest::cardGoneDuringReleaseQueued()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.takeOn());
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake->hold(FakeUsbIo::Op::Out);
    int called = 0;
    rig.dac->startRelease([&called] { ++called; });
    QTRY_VERIFY_WITH_TIMEOUT(held->waitEntered(1), waitMs);
    QCOMPARE(called, 0);
    rig.dac->onCardGoneChanged(true);
    QCOMPARE(called, 0);
    QTRY_COMPARE_WITH_TIMEOUT(called, 1, waitMs);
    QVERIFY(!rig.dac->dacHeld());
    held->release();
    QVERIFY(rig.idle());
    QCOMPARE(called, 1);
}

void DacEqTest::effectiveNoCardLogged()
{
    LogCapture capture;
    const QString line = QStringLiteral("microdap dac eq: effective, no card");
    Rig rig;
    QVERIFY(rig.ready);
    rig.seed(QStringLiteral("bitperfect"), true);
    rig.dac->start();
    rig.dac->onFirstApplied();
    QTest::qWait(150);
    QCOMPARE(capture.count(line), 0);
    QTRY_COMPARE_WITH_TIMEOUT(capture.count(line), 1, waitMs);
    QVERIFY(rig.idle(700));
    QCOMPARE(capture.count(line), 1);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);

    rig.dac->onEffectiveChanged(false);
    rig.dac->onEffectiveChanged(true);
    rig.dac->onCardChanged(QStringLiteral("B"), 1);
    QVERIFY(rig.idle(600));
    QCOMPARE(capture.count(line), 1);
}

void DacEqTest::refusedRearmsCapped()
{
    LogCapture capture;
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    QVERIFY(rig.pcm("RUNNING\n"));
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QCOMPARE(rig.dac->reason(), QStringLiteral("overflow"));
    const QString rearmed = QStringLiteral("microdap dac eq: reset refused, rearmed");
    for (int i = 0; i < 4; ++i) {
        const std::shared_ptr<FakeUsbIo::Hold> openHeld = rig.fake->hold(FakeUsbIo::Op::Open);
        QVERIFY(rig.pcm("closed\n"));
        QTRY_VERIFY_WITH_TIMEOUT(openHeld->waitEntered(1), waitMs);
        QVERIFY(rig.pcm("RUNNING\n"));
        openHeld->release();
        QTRY_COMPARE_WITH_TIMEOUT(capture.count(rearmed) + (rig.dac->reason() == QLatin1StringView("device") ? 1 : 0),
                                  i + 1, waitMs);
    }
    QCOMPARE(capture.count(rearmed), 3);
    QCOMPARE(rig.dac->reason(), QStringLiteral("device"));
    QCOMPARE(rig.dac->status(), QStringLiteral("failed"));
    QVERIFY(rig.pcm("closed\n"));
    const int opens = rig.fake->count(FakeUsbIo::Op::Open);
    rig.dac->onPlayStateChanged();
    QVERIFY(rig.idle(600));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), opens);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
}

void DacEqTest::gateRetriesWhilePending()
{
    Rig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.wedge());
    QVERIFY(rig.pcm("RUNNING\n"));
    rig.eq->setCustom(usb, testProfile());
    rig.holdLevel();
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("failed"), waitMs);
    QVERIFY(rig.idle(500));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    QVERIFY(rig.pcm("closed\n"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.fake->count(FakeUsbIo::Op::Reset), 1, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.dac->status(), QStringLiteral("on"), waitMs);
    const int opens = rig.fake->count(FakeUsbIo::Op::Open);
    QVERIFY(rig.idle(600));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), opens);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
}

QTEST_GUILESS_MAIN(DacEqTest)
#include "tst_daceq.moc"
