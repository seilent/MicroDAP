#include "AlsaVolume.h"
#include "FakeBluezModel.h"
#include "OutputFakes.h"
#include "OutputMode.h"
#include "WirePlumberRestart.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace microdap;

namespace {

constexpr int waitMs = 3000;
const QString fiioSink = QStringLiteral("alsa_output.usb-FIIO_SNOWSKY_TINY_B_000000000000-01.analog-stereo");
const QString replugSink = QStringLiteral("alsa_output.usb-FIIO_SNOWSKY_TINY_B_000000000000-01.3.analog-stereo");
const QString dongleSink = QStringLiteral("alsa_output.usb-Apple__Inc._USB-C_to_3.5mm_Headphone_Jack_Adapter-00.analog-stereo");

const char *fiioStream = R"(FIIO SNOWSKY TINY B at usb-xhci-hcd.3.auto-1, high speed : USB Audio

Playback:
  Status: Stop
  Interface 2
    Altset 1
    Format: S16_LE
    Channels: 2
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
  Interface 2
    Altset 2
    Format: S24_3LE
    Channels: 2
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
  Interface 2
    Altset 3
    Format: S32_LE
    Channels: 2
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
)";

const char *dongleStream = R"(Apple, Inc. USB-C to 3.5mm Headphone Jack Adapter at usb-xhci-hcd.1.auto-1, high speed : USB Audio

Playback:
  Status: Stop
  Interface 1
    Altset 1
    Format: S24_3LE
    Channels: 2
    Rates: 48000
  Interface 1
    Altset 2
    Format: S16_LE
    Channels: 2
    Rates: 48000
)";

bool writeFile(const QString &path, const QByteArray &text, bool executable = false)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text);
    file.close();
    if (!executable)
        return true;
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

int indexOf(const QStringList &log, const QString &prefix, int from = 0)
{
    for (int i = from; i < log.size(); ++i) {
        if (log.at(i).startsWith(prefix))
            return i;
    }
    return -1;
}

int countOf(const QStringList &log, const QString &prefix)
{
    int n = 0;
    for (const QString &line : log) {
        if (line.startsWith(prefix))
            ++n;
    }
    return n;
}

OutputMode::Bounds shortBounds()
{
    OutputMode::Bounds b;
    b.pcmFreeMs = 150;
    b.pcmPollMs = 5;
    b.acquireMs = 400;
    b.restartWaitMs = 300;
    b.parkMs = 120;
    b.netFirstMs = 120;
    b.netSecondMs = 150;
    b.switchMs = 100;
    b.pcmWatchMs = 250;
    b.retryMs = 400;
    b.resolveMs = 150;
    b.readyMs = 150;
    b.quitMs = 50;
    b.sleepReleaseMs = 120;
    b.dacReleaseMs = 250;
    b.pacingMs = 10;
    b.storeDelayMs = 30;
    return b;
}

using Chain = PlayerPort::OutputChain;
using Play = PlayerPort::PlayState;
using State = OutputMode::State;
using Reason = OutputMode::Reason;

class Rig
{
public:
    explicit Rig(const OutputMode::Bounds &bounds = shortBounds(), bool withDacEq = false)
        : mixer(new FakeMixer(&log))
        , alsa(std::unique_ptr<MixerBackend>(mixer), bounds.pacingMs)
    {
        dac.card.cardIndex = 7;
        dac.card.alsaCard = 1;
        dac.card.reserveName = QStringLiteral("Audio1");
        dac.volumeDb = -20.0;
        dac.sinkCards.insert(fiioSink, dac.card);
        dac.sinkCards.insert(replugSink, dac.card);
        restart.setBluez(&bluez);
        OutputMode::Ports ports;
        ports.player = &player;
        ports.reservation = &reservation;
        ports.output = &output;
        ports.dac = &dac;
        ports.volume = &volume;
        ports.mixer = &alsa;
        ports.wireplumber = &restart;
        ports.bluez = &bluez;
        if (withDacEq)
            ports.dacEq = &dacEq;
        mode = std::make_unique<OutputMode>(bounds, ports);
        player.policy = mode.get();
        player.onStatus = [this] { mode->onStatusChanged(); };
        reservation.onAcquire = [this](quint64 seq) {
            if (autoAcquire)
                QTimer::singleShot(0, &context, [this, seq] { mode->onAcquired(seq); });
        };
        reservation.onRelease = [this] {
            if (autoRelease)
                QTimer::singleShot(0, &context, [this] { mode->onReleased(); });
        };
        output.onRefresh = [this](quint64 read) { mode->onSinksRead(sinkListed ? sink : QString(), read); };
        QObject::connect(&restart, &WirePlumberRestart::restartFinished, mode.get(), &OutputMode::onRestartFinished);
        QObject::connect(&alsa, &AlsaVolume::disconnected, mode.get(), &OutputMode::onMixerDisconnected);
        QObject::connect(&bluez, &BluezModel::connectedChanged, mode.get(), &OutputMode::onBluezConnectedChanged);
        QObject::connect(&bluez, &BluezModel::readyChanged, mode.get(), &OutputMode::onBluezReadyChanged);
        QObject::connect(&bluez, &BluezModel::transportStateChanged, mode.get(),
                         &OutputMode::onTransportStateChanged);
    }

    bool enter(const QString &level)
    {
        mode->setLevel(level);
        mode->onActiveOutputChanged(QStringLiteral("usb"), sink);
        return QTest::qWaitFor([this] { return volume.route == DacVolumePort::Route::Alsa; }, waitMs);
    }

    bool hold(qint64 id = 1)
    {
        player.play(id);
        return QTest::qWaitFor([this] { return mode->state() == State::ExHeld && player.chain == Chain::Ex; },
                               waitMs);
    }

    bool settled()
    {
        return QTest::qWaitFor([this] { return !mode->releasing(); }, waitMs);
    }

    QStringList log;
    FakeMixer *mixer;
    AlsaVolume alsa;
    FakePlayer player{&log};
    FakeReservation reservation{&log};
    FakeHeldOutput output{&log};
    FakeDacSink dac{&log};
    FakeDacVolume volume{&log};
    FakeDacEqPort dacEq{&log};
    FakeBluezModel bluez;
    WirePlumberRestart restart;
    std::unique_ptr<OutputMode> mode;
    QObject context;
    QString sink = fiioSink;
    bool sinkListed = true;
    bool autoAcquire = true;
    bool autoRelease = true;
};

}

class OutputModeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();

    void pwLevelBitperfectEntersBp();
    void pwLevelExclusivePlayingAcquires();
    void pwLevelExclusivePausedParks();
    void bpLevelOffRebuildsPw();
    void bpOutputAwayRebuildsPw();
    void exIdlePlayingBuildAcquires();
    void exIdlePlayingBuildBlockedBuildsBp();
    void exIdlePausedBuildParks();
    void exIdlePausedBuildBlockedBuildsBp();
    void exIdleParkedLevelChangeBuildsPaused();
    void exIdleParkedUnplugBuildsPwPaused();
    void exIdleConnectedSetsPhone();
    void exIdleDisconnectedNextPlayAcquires();
    void releasePlayingBuildWaitsLatestWins();
    void releasePauseCancelsWaitingBuild();
    void releaseConnectedSetsPhone();
    void releaseSwitchJoinsBounded();
    void releaseSleepCancelsNet();
    void releaseUnplugEndsReturn();
    void releaseOutputAwayEndsReturn();
    void releaseLevelChangeRunsToEnd();
    void releaseReturningEndsAtFreshRead();
    void releaseRenamedSinkUsedByNextAcquire();
    void acquiringNextAndSeekEditPending();
    void acquiringPauseBeforeA3Drops();
    void acquiringSupersededAnswerDropped();
    void acquiringPauseAfterA3Releases();
    void acquiringConnectedAbortsToBp();
    void acquiringSleepBeforeA3Drops();
    void acquiringSleepAfterA3KeepsName();
    void acquiringA4DoneHolds();
    void acquiringRefusedUnavailable();
    void acquiringNoVolumeRefused();
    void acquiringSoftwarePartBlocks();
    void acquiringTimeoutRetriesAfterBound();
    void acquiringCardGoneUnplugPath();
    void acquireDoneStopReleases();
    void heldBuildKeepsName();
    void heldPauseParkTimer();
    void heldPausedBuildKeepsTimer();
    void heldParkTimerReleasesIdle();
    void heldStopReleasesAtOnce();
    void heldEndOfStreamReleasesAtOnce();
    void heldConnectedReleasesContinuesBp();
    void heldSwitchReleasesThenReady();
    void heldSleepKeepsName();
    void heldWakeSameGenerationReleases();
    void heldWakeNewerGenerationReturnedCard();
    void heldWakeCardGoneNoReturn();
    void heldReleaseRequestedGrants();
    void grantSleepNameLostNoNewRelease();
    void heldNameLostUnrequested();
    void heldConnectionDropped();
    void heldCardGoneUnplugToPw();
    void heldReplugInsidePollGap();
    void heldLevelChangeRebuilds();
    void heldReadyFalseReleases();
    void heldQuitReleasesThroughJoin();
    void netSinkListedNoWait();
    void netSinkMissingRestartsOnceThenUnavailable();
    void netCardGoneWhenTimerFires();
    void netTransportDefers();
    void netConnectedWithoutTransportRestarts();
    void netAdoptsRunningRestart();
    void netSecondFiringSticky();
    void stickyUnavailableClearsOnDisconnect();
    void restartingWaits();
    void restartingBoundBuildsBp();
    void switchDuringRestartWaitParks();
    void readyFalseNoAcquire();
    void dapStartAdoptsWritesNothing();
    void entryUnmutesOnce();
    void replugWritesOwnedStep();
    void rowChangeAdoptsHardwareStep();
    void otherDacAfterUnplugIsEffective();
    void sinkReturnWhilePlayingStoresAtPause();
    void outputErrorOpenWriteUnavailable();
    void outputErrorCardGoneUnplug();
    void outputErrorWriteReleasesAndReacquires();
    void restoreWaitsForResolveAndReady();
    void restoreBoundsAlone();
    void restoreNeverWithLevelOff();
    void restoreStaysPausedWithTransportActive();
    void restoreDroppedByUserPlay();
    void switchWithoutLevelIsSynchronous();
    void sleepDuringReleaseWaits();
    void sleepDuringReleaseBound();
    void statusSuffixOrder();
    void ratesAndFixedRate();
    void exclusiveSinkFormat();
    void bitPerfectSinkFollowsUsb();
    void cardChangedAtAssignments();
    void generationAfterGoneDecision();
    void cardGoneOnlyOnChange();
    void exHeldChangedOnTransitions();
    void switchReadyDacNow();
    void switchReadyDacDelayed();
    void switchReadyDacNever();
    void switchReadyLateCallback();
    void switchRetryJoinsWithDac();
    void switchReadyNoDacEq();
    void switchBoundIncludesDac();
    void quitOrder();

private:
    void setCard(int index, const QByteArray &id, const char *stream = fiioStream);
    void removeCard(int index);
    void setPcm(int index, const QByteArray &status);
    void setRestartDelay(const QString &seconds);
    int restarts() const;
    bool enterSwitchPath(Rig &rig, int path);
    void finishSwitchPath(Rig &rig, int path);

    QTemporaryDir m_dir;
    QString m_asound;
    QString m_stub;
    QString m_stubLog;
    QString m_delay;
};

void OutputModeTest::setCard(int index, const QByteArray &id, const char *stream)
{
    const QString card = m_asound + QStringLiteral("/card%1").arg(index);
    QVERIFY(writeFile(card + QStringLiteral("/id"), id + "\n"));
    QVERIFY(writeFile(card + QStringLiteral("/stream0"), QByteArray(stream)));
    QVERIFY(writeFile(card + QStringLiteral("/pcm0p/sub0/status"), "closed\n"));
}

void OutputModeTest::removeCard(int index)
{
    QDir(m_asound + QStringLiteral("/card%1").arg(index)).removeRecursively();
}

void OutputModeTest::setPcm(int index, const QByteArray &status)
{
    QVERIFY(writeFile(m_asound + QStringLiteral("/card%1/pcm0p/sub0/status").arg(index), status + "\n"));
}

void OutputModeTest::setRestartDelay(const QString &seconds)
{
    QVERIFY(writeFile(m_delay, seconds.toUtf8() + "\n"));
}

int OutputModeTest::restarts() const
{
    return int(readFile(m_stubLog).count(QStringLiteral("restart wireplumber")));
}

void OutputModeTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_asound = m_dir.path() + QStringLiteral("/asound");
    m_stub = m_dir.path() + QStringLiteral("/bin/systemctl");
    m_stubLog = m_dir.path() + QStringLiteral("/systemctl.log");
    m_delay = m_dir.path() + QStringLiteral("/delay");
    QVERIFY(writeFile(m_stub,
                      QStringLiteral("#!/bin/sh\nprintf '%s\\n' \"$*\" >> \"%1\"\nsleep \"$(cat \"%2\")\"\nexit 0\n")
                          .arg(m_stubLog, m_delay)
                          .toUtf8(),
                      true));
    qputenv("MICRODAP_ASOUND_DIR", m_asound.toUtf8());
    qputenv("MICRODAP_SYSTEMCTL_BIN", m_stub.toUtf8());
    qputenv("MICRODAP_WP_CONF_DIR", (m_dir.path() + QStringLiteral("/wireplumber.conf.d")).toUtf8());
}

void OutputModeTest::init()
{
    QDir(m_asound).removeRecursively();
    QFile::remove(m_stubLog);
    setRestartDelay(QStringLiteral("0"));
    setCard(1, "B");
}

void OutputModeTest::pwLevelBitperfectEntersBp()
{
    Rig rig;
    rig.mixer->value = 70;
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.player.play(1);
    rig.player.seek(4000);
    QCOMPARE(rig.player.chain, Chain::Pw);
    QCOMPARE(rig.mode->state(), State::Pw);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QCOMPARE(rig.mode->state(), State::Bp);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp playing 1 4000")));
    QTRY_COMPARE_WITH_TIMEOUT(rig.volume.route, DacVolumePort::Route::Alsa, waitMs);
    QCOMPARE(rig.volume.ownedStep(QStringLiteral("B")), 70);
    QVERIFY(rig.mixer->writes.isEmpty());
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("unmute ")), 1);
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::pwLevelExclusivePlayingAcquires()
{
    Rig rig;
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.player.play(1);
    rig.player.seek(2500);
    rig.mode->setLevel(QStringLiteral("exclusive"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QVERIFY(rig.reservation.calls.contains(QStringLiteral("acquire Audio1 B")));
    QVERIFY(rig.player.calls.contains(QStringLiteral("built ex playing 1 2500")));
}

void OutputModeTest::pwLevelExclusivePausedParks()
{
    Rig rig;
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.player.play(1);
    rig.player.seek(1500);
    rig.player.pause();
    rig.mode->setLevel(QStringLiteral("exclusive"));
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.player.position, qint64(1500));
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QTest::qWait(50);
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::bpLevelOffRebuildsPw()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.play(1);
    rig.player.seek(3000);
    QCOMPARE(rig.player.chain, Chain::Bp);
    rig.mode->setLevel(QStringLiteral("off"));
    QCOMPARE(rig.mode->state(), State::Pw);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built pw playing 1 3000")));
    QCOMPARE(rig.volume.route, DacVolumePort::Route::Pulse);
    QVERIFY(!rig.alsa.isOpen());
}

void OutputModeTest::bpOutputAwayRebuildsPw()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.play(1);
    rig.player.seek(800);
    rig.player.pause();
    rig.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    QCOMPARE(rig.mode->state(), State::Pw);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built pw paused 1 800")));
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("usb only"));
}

void OutputModeTest::exIdlePlayingBuildAcquires()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QVERIFY(rig.hold());
    const int acquire = indexOf(rig.log, QStringLiteral("reserve acquire Audio1 B"));
    const int held = indexOf(rig.log, QStringLiteral("output held held"));
    const int watch = indexOf(rig.log, QStringLiteral("output watch 1 B"));
    const int built = indexOf(rig.log, QStringLiteral("player built ex playing 1 0"));
    QVERIFY(acquire >= 0);
    QVERIFY(acquire < held);
    QVERIFY(held < watch);
    QVERIFY(watch < built);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive"));
    QVERIFY(!rig.volume.storeAllowed);
}

void OutputModeTest::exIdlePlayingBuildBlockedBuildsBp()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.bluez.setConnected(true);
    rig.player.play(1);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.mode->reason(), Reason::Phone);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (phone)"));
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::exIdlePausedBuildParks()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.playPaused(1, 900);
    QTRY_VERIFY_WITH_TIMEOUT(rig.player.isParked, waitMs);
    QCOMPARE(rig.player.position, qint64(900));
    QVERIFY(rig.reservation.calls.isEmpty());
    QCOMPARE(rig.mode->state(), State::ExIdle);
}

void OutputModeTest::exIdlePausedBuildBlockedBuildsBp()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.bluez.setConnected(true);
    rig.player.playPaused(1, 900);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp paused 1 900")));
    QTest::qWait(30);
    QVERIFY(!rig.player.isParked);
}

void OutputModeTest::exIdleParkedLevelChangeBuildsPaused()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.playPaused(1, 1200);
    QTRY_VERIFY_WITH_TIMEOUT(rig.player.isParked, waitMs);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp paused 1 1200")));
    QCOMPARE(rig.mode->state(), State::Bp);
}

void OutputModeTest::exIdleParkedUnplugBuildsPwPaused()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.playPaused(1, 2200);
    QTRY_VERIFY_WITH_TIMEOUT(rig.player.isParked, waitMs);
    removeCard(1);
    rig.mode->onCardRemoved(7);
    QCOMPARE(rig.mode->state(), State::Pw);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built pw paused 1 2200")));
}

void OutputModeTest::exIdleConnectedSetsPhone()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.playPaused(1);
    QTRY_VERIFY_WITH_TIMEOUT(rig.player.isParked, waitMs);
    const QStringList before = rig.log;
    rig.bluez.setConnected(true);
    QCOMPARE(rig.mode->reason(), Reason::Phone);
    QCOMPARE(rig.log, before);
}

void OutputModeTest::exIdleDisconnectedNextPlayAcquires()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.bluez.setConnected(true);
    rig.player.play(1);
    QCOMPARE(rig.player.chain, Chain::Bp);
    const int before = int(rig.log.size());
    rig.bluez.setConnected(false);
    QCOMPARE(int(rig.log.size()), before);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (phone)"));
    rig.player.next();
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QCOMPARE(rig.player.chain, Chain::Ex);
}

void OutputModeTest::releasePlayingBuildWaitsLatestWins()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    const int calls = rig.player.buildPendingCalls;
    for (int i = 0; i < 5; ++i)
        rig.player.next();
    QCOMPARE(rig.player.status, Play::Playing);
    QCOMPARE(rig.player.entry, qint64(6));
    QCOMPARE(rig.player.buildPendingCalls, calls);
    rig.sinkListed = true;
    rig.mode->onSinkAppeared(40, fiioSink, 1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QCOMPARE(rig.player.buildPendingCalls, calls + 1);
    QVERIFY(rig.player.calls.last().startsWith(QStringLiteral("built ex playing 6")));
}

void OutputModeTest::releasePauseCancelsWaitingBuild()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.player.next();
    rig.player.pause();
    QVERIFY(rig.player.isParked);
    const int calls = rig.player.buildPendingCalls;
    rig.sinkListed = true;
    rig.mode->onSinkAppeared(40, fiioSink, 1);
    QVERIFY(rig.settled());
    QTest::qWait(30);
    QCOMPARE(rig.player.buildPendingCalls, calls);
    QVERIFY(rig.player.isParked);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1);
}

void OutputModeTest::releaseConnectedSetsPhone()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.player.resume();
    const int before = int(rig.log.size());
    rig.bluez.setConnected(true);
    QCOMPARE(rig.mode->reason(), Reason::Phone);
    QCOMPARE(int(rig.log.size()), before);
    rig.sinkListed = true;
    rig.mode->onSinkAppeared(40, fiioSink, 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (phone)"));
}

void OutputModeTest::releaseSwitchJoinsBounded()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    QElapsedTimer clock;
    clock.start();
    rig.mode->releaseForSwitch();
    QCOMPARE(ready.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
    QVERIFY(clock.elapsed() < 1000);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
    QVERIFY(rig.player.isParked);
}

void OutputModeTest::releaseSleepCancelsNet()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.mode->prepareForSleep(true);
    QTest::qWait(400);
    QCOMPARE(restarts(), 0);
    rig.sinkListed = true;
    const int refreshes = rig.output.refreshes;
    rig.mode->prepareForSleep(false);
    QVERIFY(rig.output.refreshes > refreshes);
    QVERIFY(rig.settled());
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
}

void OutputModeTest::releaseUnplugEndsReturn()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    removeCard(1);
    rig.mode->onCardGone(1);
    QVERIFY(!rig.mode->releasing());
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QCOMPARE(rig.mode->state(), State::Pw);
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::releaseOutputAwayEndsReturn()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.mode->onActiveOutputChanged(QStringLiteral("headphones"), QString());
    QVERIFY(!rig.mode->releasing());
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("inputs")), 0);
    QCOMPARE(rig.mode->state(), State::Pw);
}

void OutputModeTest::releaseLevelChangeRunsToEnd()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QVERIFY(rig.mode->releasing());
    rig.sinkListed = true;
    rig.mode->onSinkAppeared(40, fiioSink, 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->state(), State::Bp);
    QVERIFY(rig.player.calls.last().startsWith(QStringLiteral("built bp paused")));
}

void OutputModeTest::releaseReturningEndsAtFreshRead()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    rig.mode->onSinkAppeared(41, replugSink, 1);
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), replugSink);
    QTest::qWait(30);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::Returning);
    QVERIFY(rig.mode->releasing());
    rig.mode->onSinksRead(replugSink, rig.output.started - 1);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::Returning);
    rig.mode->onSinksRead(replugSink, rig.output.started);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QVERIFY(!rig.mode->releasing());
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::releaseRenamedSinkUsedByNextAcquire()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sink = replugSink;
    rig.dac.sinkCards.remove(fiioSink);
    rig.player.stop();
    QVERIFY(rig.settled());
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), replugSink);
    QVERIFY(rig.hold(2));
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire Audio1 B")), 2);
}

void OutputModeTest::acquiringNextAndSeekEditPending()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    QCOMPARE(rig.mode->state(), State::ExAcquiring);
    rig.player.next();
    rig.player.next();
    rig.player.seek(700);
    QCOMPARE(rig.player.status, Play::Playing);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1);
    rig.mode->onAcquired(rig.reservation.lastSeq);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built ex playing 3 700")));
}

void OutputModeTest::acquiringPauseBeforeA3Drops()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    rig.player.pause();
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    rig.mode->onAcquired(rig.reservation.lastSeq);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QCOMPARE(rig.mode->state(), State::ExIdle);
}

void OutputModeTest::acquiringSupersededAnswerDropped()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    const quint64 first = rig.reservation.lastSeq;
    rig.player.pause();
    rig.player.resume();
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 2, waitMs);
    const quint64 second = rig.reservation.lastSeq;
    QVERIFY(second != first);
    rig.mode->onAcquired(second);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QCOMPARE(rig.player.chain, Chain::Ex);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    rig.player.stop();
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
}

void OutputModeTest::acquiringPauseAfterA3Releases()
{
    Rig rig;
    setPcm(1, "RUNNING");
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Held, waitMs);
    rig.player.pause();
    QVERIFY(rig.player.isParked);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.mode->state(), State::ExIdle);
}

void OutputModeTest::acquiringConnectedAbortsToBp()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    rig.bluez.setConnected(true);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.player.status, Play::Playing);
    QCOMPARE(rig.mode->reason(), Reason::Phone);
    rig.mode->onAcquired(rig.reservation.lastSeq);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
}

void OutputModeTest::acquiringSleepBeforeA3Drops()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    rig.mode->prepareForSleep(true);
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
}

void OutputModeTest::acquiringSleepAfterA3KeepsName()
{
    Rig rig;
    setPcm(1, "RUNNING");
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Held, waitMs);
    rig.mode->prepareForSleep(true);
    QCOMPARE(rig.mode->state(), State::ExHeld);
    QVERIFY(rig.player.isParked);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QVERIFY(!rig.alsa.isOpen());
    QTest::qWait(200);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
}

void OutputModeTest::acquiringA4DoneHolds()
{
    Rig rig;
    setPcm(1, "RUNNING");
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.volume.setOwnedStep(QStringLiteral("B"), 50);
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Held, waitMs);
    QTest::qWait(40);
    QCOMPARE(rig.mode->state(), State::ExAcquiring);
    setPcm(1, "closed");
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(rig.mixer->writes.contains(50), waitMs);
}

void OutputModeTest::acquiringRefusedUnavailable()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    rig.mode->onRefused(rig.reservation.lastSeq);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (unavailable)"));
    rig.player.next();
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    rig.mode->setLevel(QStringLiteral("exclusive"));
    rig.player.next();
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 2, waitMs);
}

void OutputModeTest::acquiringNoVolumeRefused()
{
    Rig rig;
    rig.mixer->mixerRange = MixerRange{0, 60, -30.0, 0.0};
    rig.mixer->value = 40;
    rig.dac.volumeDb = -10.0;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.player.chain, Chain::Bp, waitMs);
    QCOMPARE(rig.mode->reason(), Reason::NoVolume);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (no volume)"));
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::acquiringSoftwarePartBlocks()
{
    Rig rig;
    rig.dac.volumeDb = -20.6;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.player.chain, Chain::Bp, waitMs);
    QCOMPARE(rig.mode->reason(), Reason::Volume);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (volume)"));
    QVERIFY(rig.reservation.calls.isEmpty());
    rig.dac.volumeDb = -20.0;
    rig.mode->onVolumePressed();
    rig.player.next();
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
}

void OutputModeTest::acquiringTimeoutRetriesAfterBound()
{
    OutputMode::Bounds bounds = shortBounds();
    bounds.retryMs = 1500;
    Rig rig(bounds);
    setPcm(1, "RUNNING");
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    rig.player.seek(600);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(rig.settled());
    QCOMPARE(rig.player.chain, Chain::Bp);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp playing 1 600")));
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    setPcm(1, "closed");
    rig.player.next();
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1);
    QTest::qWait(1600);
    rig.player.next();
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("acquire")), 2);
}

void OutputModeTest::acquiringCardGoneUnplugPath()
{
    Rig rig;
    rig.autoAcquire = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, waitMs);
    removeCard(1);
    rig.mode->onFailed(rig.reservation.lastSeq, QStringLiteral("timeout"));
    QCOMPARE(rig.mode->state(), State::Pw);
    QCOMPARE(rig.player.chain, Chain::Pw);
    QCOMPARE(rig.player.status, Play::Playing);
}

void OutputModeTest::acquireDoneStopReleases()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.emptyQueue = true;
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(rig.settled());
    QCOMPARE(rig.player.status, Play::Stopped);
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
}

void OutputModeTest::heldBuildKeepsName()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.next();
    QCOMPARE(rig.player.chain, Chain::Ex);
    QCOMPARE(rig.player.entry, qint64(2));
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QCOMPARE(rig.mode->state(), State::ExHeld);
}

void OutputModeTest::heldPauseParkTimer()
{
    OutputMode::Bounds bounds = shortBounds();
    bounds.parkMs = 600;
    Rig rig(bounds);
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.pause();
    QTest::qWait(100);
    rig.player.resume();
    QVERIFY(rig.player.calls.contains(QStringLiteral("resume")));
    QTest::qWait(800);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QElapsedTimer clock;
    clock.start();
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(clock.elapsed() >= 540);
}

void OutputModeTest::heldPausedBuildKeepsTimer()
{
    OutputMode::Bounds bounds = shortBounds();
    bounds.parkMs = 900;
    Rig rig(bounds);
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    QElapsedTimer clock;
    clock.start();
    rig.player.pause();
    QTest::qWait(600);
    rig.player.playPaused(2);
    QCOMPARE(rig.player.chain, Chain::Ex);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built ex paused 2 0")));
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(clock.elapsed() >= 810);
    QVERIFY(clock.elapsed() < 1350);
}

void OutputModeTest::heldParkTimerReleasesIdle()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.seek(5000);
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(rig.settled());
    const int park = indexOf(rig.log, QStringLiteral("player park 5000"));
    const int release = indexOf(rig.log, QStringLiteral("reserve release"));
    const int returning = indexOf(rig.log, QStringLiteral("output held returning"));
    const int def = indexOf(rig.log, QStringLiteral("inputs"));
    const int none = indexOf(rig.log, QStringLiteral("output held none"), returning);
    QVERIFY(park >= 0);
    QVERIFY(park < release);
    QVERIFY(release < returning);
    QVERIFY(returning < def);
    QVERIFY(def < none);
    QVERIFY(indexOf(rig.log, QStringLiteral("volume store sync B")) > def);
    QVERIFY(indexOf(rig.log, QStringLiteral("unmute ") + fiioSink, def) > def);
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QCOMPARE(rig.mode->reason(), Reason::Idle);
    QVERIFY(rig.player.isParked);
    QVERIFY(rig.volume.storeAllowed);
}

void OutputModeTest::heldStopReleasesAtOnce()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->reason(), Reason::Idle);
    QCOMPARE(rig.player.status, Play::Stopped);
}

void OutputModeTest::heldEndOfStreamReleasesAtOnce()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->onEndOfStream();
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->state(), State::ExIdle);
}

void OutputModeTest::heldConnectedReleasesContinuesBp()
{
    Rig rig;
    rig.dac.inputs = {12};
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.seek(2000);
    rig.bluez.setConnected(true);
    QCOMPARE(rig.player.status, Play::Playing);
    QVERIFY(rig.settled());
    const int release = indexOf(rig.log, QStringLiteral("reserve release"));
    const int def = indexOf(rig.log, QStringLiteral("inputs"));
    const int move = indexOf(rig.log, QStringLiteral("move 12 ") + fiioSink);
    const int built = indexOf(rig.log, QStringLiteral("player built bp playing 1 2000"));
    QVERIFY(indexOf(rig.log, QStringLiteral("player hold 1 playing 2000")) < release);
    QVERIFY(release < def);
    QVERIFY(def < move);
    QVERIFY(move < built);
    QCOMPARE(rig.mode->reason(), Reason::Phone);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("exclusive (phone)"));
}

void OutputModeTest::heldSwitchReleasesThenReady()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    rig.mode->releaseForSwitch();
    QCOMPARE(ready.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
    QVERIFY(rig.player.isParked);
    QVERIFY(indexOf(rig.log, QStringLiteral("player park")) < indexOf(rig.log, QStringLiteral("reserve release")));
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
    QCOMPARE(rig.mode->reason(), Reason::Switch);
    QCOMPARE(countOf(rig.log, QStringLiteral("player built")), 1);
}

void OutputModeTest::heldSleepKeepsName()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.pause();
    rig.mode->prepareForSleep(true);
    QCOMPARE(rig.mode->state(), State::ExHeld);
    QVERIFY(rig.player.isParked);
    QVERIFY(!rig.alsa.isOpen());
    QTest::qWait(200);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
}

void OutputModeTest::heldWakeSameGenerationReleases()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    const quint64 generation = rig.mode->generation();
    rig.mode->prepareForSleep(true);
    rig.mode->prepareForSleep(false);
    QCOMPARE(rig.mode->generation(), generation);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("mark gone")), 0);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
    QCOMPARE(rig.mode->reason(), Reason::Idle);
    QVERIFY(rig.player.isParked);
    QVERIFY(rig.alsa.isOpen());
}

void OutputModeTest::heldWakeNewerGenerationReturnedCard()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->prepareForSleep(true);
    const quint64 generation = rig.mode->generation();
    rig.mode->onCardGone(1);
    QVERIFY(rig.mode->generation() > generation);
    QVERIFY(rig.reservation.calls.contains(QStringLiteral("mark gone")));
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    rig.sink = replugSink;
    rig.volume.setOwnedStep(QStringLiteral("B"), 74);
    rig.mode->prepareForSleep(false);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(indexOf(rig.log, QStringLiteral("reserve mark gone")) < indexOf(rig.log, QStringLiteral("reserve release")));
    QVERIFY(rig.settled());
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), replugSink);
    QVERIFY(rig.mode->bitPerfectSink() != fiioSink);
    QVERIFY(rig.mixer->writes.contains(74));
    QVERIFY(rig.player.isParked);
}

void OutputModeTest::heldWakeCardGoneNoReturn()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->prepareForSleep(true);
    removeCard(1);
    rig.mode->prepareForSleep(false);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("inputs")), 0);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QCOMPARE(rig.mode->state(), State::Pw);
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::heldReleaseRequestedGrants()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->onReleaseRequested(20);
    QVERIFY(rig.player.isParked);
    QVERIFY(indexOf(rig.log, QStringLiteral("player park")) < indexOf(rig.log, QStringLiteral("reserve grant")));
    rig.mode->onNameLost(true);
    QVERIFY(!rig.mode->releasing());
    QCOMPARE(rig.mode->reason(), Reason::Released);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("inputs")), 0);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
}

void OutputModeTest::grantSleepNameLostNoNewRelease()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->onReleaseRequested(20);
    QVERIFY(rig.reservation.calls.contains(QStringLiteral("grant")));
    rig.mode->prepareForSleep(true);
    rig.mode->onNameLost(true);
    const int refreshes = rig.output.refreshes;
    rig.mode->prepareForSleep(false);
    QVERIFY(!rig.mode->releasing());
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QCOMPARE(rig.mode->reason(), Reason::Released);
    QTest::qWait(400);
    QCOMPARE(rig.output.refreshes, refreshes);
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("inputs")), 0);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::heldNameLostUnrequested()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->onNameLost(false);
    QVERIFY(rig.player.isParked);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QVERIFY(rig.settled());
    QVERIFY(indexOf(rig.log, QStringLiteral("player park")) < indexOf(rig.log, QStringLiteral("output held returning")));
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
    QCOMPARE(rig.mode->reason(), Reason::Lost);
}

void OutputModeTest::heldConnectionDropped()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->onConnectionLost();
    QVERIFY(rig.player.isParked);
    QTRY_COMPARE_WITH_TIMEOUT(restarts(), 1, waitMs);
    QCOMPARE(rig.mode->reason(), Reason::Lost);
}

void OutputModeTest::heldCardGoneUnplugToPw()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.seek(3300);
    removeCard(1);
    rig.mode->onCardGone(1);
    rig.mode->onMixerDisconnected();
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("mark gone")), 1);
    QVERIFY(indexOf(rig.log, QStringLiteral("reserve mark gone")) < indexOf(rig.log, QStringLiteral("player hold 1 playing 3300")));
    QVERIFY(indexOf(rig.log, QStringLiteral("player hold 1 playing 3300")) < indexOf(rig.log, QStringLiteral("reserve release")));
    QVERIFY(rig.settled());
    QVERIFY(rig.player.calls.contains(QStringLiteral("built pw playing 1 3300")));
    QCOMPARE(rig.mode->state(), State::Pw);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::heldReplugInsidePollGap()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.pause();
    rig.mixer->fire(POLLERR);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("mark gone")), 1, waitMs);
    const int release = indexOf(rig.log, QStringLiteral("reserve release"));
    QVERIFY(indexOf(rig.log, QStringLiteral("reserve mark gone")) < release);
    const int r2 = indexOf(rig.log, QStringLiteral("player hold 1 paused"));
    QVERIFY(r2 >= 0);
    QVERIFY(r2 < release);
    QVERIFY(rig.settled());
    QVERIFY(rig.player.isParked);
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
    QVERIFY(rig.alsa.isOpen());
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
    QCOMPARE(rig.mode->state(), State::ExIdle);
}

void OutputModeTest::heldLevelChangeRebuilds()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.seek(4400);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->state(), State::Bp);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp playing 1 4400")));
    QVERIFY(indexOf(rig.log, QStringLiteral("inputs")) < indexOf(rig.log, QStringLiteral("player built bp")));
}

void OutputModeTest::heldReadyFalseReleases()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.bluez.setReady(false);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.player.status, Play::Playing);
    QVERIFY(indexOf(rig.log, QStringLiteral("inputs")) < indexOf(rig.log, QStringLiteral("player built bp")));
}

void OutputModeTest::heldQuitReleasesThroughJoin()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    const int refreshes = rig.output.refreshes;
    rig.mode->aboutToQuit();
    const int park = indexOf(rig.log, QStringLiteral("player park"));
    const int quit = indexOf(rig.log, QStringLiteral("reserve quit 50"));
    QVERIFY(park >= 0);
    QVERIFY(park < quit);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 0);
    QTest::qWait(50);
    QCOMPARE(rig.output.refreshes, refreshes);
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("inputs")), 0);
}

void OutputModeTest::netSinkListedNoWait()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), 100);
    QTest::qWait(300);
    QCOMPARE(restarts(), 0);
}

void OutputModeTest::netSinkMissingRestartsOnceThenUnavailable()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    QSignalSpy notice(rig.mode.get(), &OutputMode::notice);
    rig.player.next();
    rig.player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(restarts(), 1, waitMs);
    QVERIFY(rig.mode->releasing());
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
    QCOMPARE(restarts(), 1);
    QCOMPARE(notice.count(), 1);
    QCOMPARE(notice.first().first().toString(), QStringLiteral("usb dac unavailable"));
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    QCOMPARE(rig.output.held, HeldOutputPort::HeldState::None);
    QCOMPARE(rig.output.watched, 1);
    rig.player.play(3);
    QCOMPARE(rig.player.chain, Chain::Bp);
}

void OutputModeTest::netCardGoneWhenTimerFires()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    removeCard(1);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
    QTest::qWait(200);
    QCOMPARE(restarts(), 0);
    QCOMPARE(rig.mode->state(), State::Pw);
}

void OutputModeTest::netTransportDefers()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.bluez.setConnected(true);
    rig.bluez.setTransportState(QStringLiteral("pending"));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
    QCOMPARE(restarts(), 0);
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    rig.bluez.setTransportState(QStringLiteral("active"));
    QTest::qWait(50);
    QCOMPARE(restarts(), 0);
    rig.bluez.setTransportState(QStringLiteral("idle"));
    QTest::qWait(50);
    QCOMPARE(restarts(), 0);
    rig.bluez.setTransportState(QStringLiteral("none"));
    QTRY_COMPARE_WITH_TIMEOUT(restarts(), 1, waitMs);
}

void OutputModeTest::netConnectedWithoutTransportRestarts()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.bluez.setConnected(true);
    QTRY_COMPARE_WITH_TIMEOUT(restarts(), 1, waitMs);
}

void OutputModeTest::netAdoptsRunningRestart()
{
    Rig rig;
    rig.sinkListed = false;
    setRestartDelay(QStringLiteral("0.4"));
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    QVERIFY(rig.restart.restartForNet(QStringLiteral("test")));
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
    QCOMPARE(restarts(), 1);
}

void OutputModeTest::netSecondFiringSticky()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.stop();
    QTRY_COMPARE_WITH_TIMEOUT(restarts(), 1, waitMs);
    rig.mode->onSinkAppeared(41, replugSink, 1);
    QVERIFY(rig.settled());
    rig.sinkListed = true;
    QCOMPARE(rig.mode->reason(), Reason::Idle);
    QVERIFY(rig.hold(5));
    rig.sinkListed = false;
    rig.player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(rig.mode->reason() == Reason::Unavailable, waitMs);
    QVERIFY(rig.settled());
    QCOMPARE(restarts(), 1);
}

void OutputModeTest::stickyUnavailableClearsOnDisconnect()
{
    Rig rig;
    rig.sinkListed = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QTRY_VERIFY_WITH_TIMEOUT(rig.mode->reason() == Reason::Unavailable && !rig.mode->releasing(), waitMs);
    rig.mode->onCardGone(2);
    rig.player.play(2);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    QVERIFY(rig.alsa.isOpen());
    const quint64 generation = rig.mode->generation();
    rig.mixer->fire(POLLERR);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->generation(), generation + 1, waitMs);
    QCOMPARE(rig.mode->cardId(), QStringLiteral("B"));
    QCOMPARE(rig.mode->reason(), Reason::None);
    rig.sinkListed = true;
    rig.player.next();
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
}

void OutputModeTest::restartingWaits()
{
    Rig rig;
    setRestartDelay(QStringLiteral("0.15"));
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.restart.restartForNet(QStringLiteral("test")));
    QVERIFY(rig.restart.restarting());
    rig.player.play(1);
    QCOMPARE(rig.player.status, Play::Playing);
    QTest::qWait(30);
    QVERIFY(rig.reservation.calls.isEmpty());
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
}

void OutputModeTest::restartingBoundBuildsBp()
{
    Rig rig;
    setRestartDelay(QStringLiteral("1"));
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.restart.restartForNet(QStringLiteral("test")));
    QElapsedTimer clock;
    clock.start();
    rig.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(rig.player.chain, Chain::Bp, waitMs);
    QVERIFY(clock.elapsed() >= 270);
    QVERIFY(rig.reservation.calls.isEmpty());
    QTRY_VERIFY_WITH_TIMEOUT(!rig.restart.restarting(), waitMs);
}

void OutputModeTest::switchDuringRestartWaitParks()
{
    Rig rig;
    setRestartDelay(QStringLiteral("0.3"));
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.restart.restartForNet(QStringLiteral("test")));
    rig.player.play(1);
    rig.player.seek(800);
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    rig.mode->releaseForSwitch();
    QCOMPARE(ready.count(), 1);
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.player.position, qint64(800));
    QSignalSpy finished(&rig.restart, &WirePlumberRestart::restartFinished);
    QVERIFY(finished.wait(waitMs));
    QTest::qWait(50);
    QVERIFY(rig.reservation.calls.isEmpty());
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("built")), 0);
    QVERIFY(rig.player.isParked);
}

void OutputModeTest::readyFalseNoAcquire()
{
    Rig rig;
    rig.bluez.setReady(false);
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    rig.player.play(1);
    QCOMPARE(rig.player.chain, Chain::Bp);
    QTest::qWait(30);
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::dapStartAdoptsWritesNothing()
{
    Rig rig;
    rig.mixer->value = 66;
    rig.mode->setLevel(QStringLiteral("exclusive"));
    QVERIFY(rig.mode->levelSet());
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    QTRY_COMPARE_WITH_TIMEOUT(rig.volume.ownedStep(QStringLiteral("B")), 66, waitMs);
    QTest::qWait(50);
    QVERIFY(rig.mixer->writes.isEmpty());
    QCOMPARE(rig.alsa.step(), 66);
    QVERIFY(rig.reservation.calls.isEmpty());
}

void OutputModeTest::entryUnmutesOnce()
{
    Rig rig;
    rig.dac.muted = true;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.dac.calls, QStringLiteral("unmute ") + fiioSink), 1, waitMs);
    QVERIFY(!rig.dac.muted);
    rig.player.play(1);
    QTest::qWait(30);
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("unmute ")), 1);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on"));
    QCOMPARE(countOf(rig.dac.calls, QStringLiteral("sink volume ")), 0);
}

void OutputModeTest::replugWritesOwnedStep()
{
    Rig rig;
    rig.mixer->value = 60;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.volume.setOwnedStep(QStringLiteral("B"), 72);
    rig.alsa.setOwnedStep(72);
    rig.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    QVERIFY(!rig.alsa.isOpen());
    rig.mixer->value = 40;
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), replugSink);
    QTRY_VERIFY_WITH_TIMEOUT(rig.mixer->writes.contains(72), waitMs);
    rig.mixer->foreign(45);
    QTRY_COMPARE_WITH_TIMEOUT(int(rig.mixer->writes.count(72)), 2, waitMs);
}

void OutputModeTest::rowChangeAdoptsHardwareStep()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    QCOMPARE(rig.volume.ownedStep(QStringLiteral("B")), 80);
    rig.mixer->foreign(60);
    QTRY_COMPARE_WITH_TIMEOUT(rig.alsa.hwStep(), 60, waitMs);
    rig.mode->setLevel(QStringLiteral("exclusive"));
    QCOMPARE(rig.volume.ownedStep(QStringLiteral("B")), 60);
    QCOMPARE(rig.alsa.step(), 60);
    QVERIFY(rig.mixer->writes.isEmpty());
}

void OutputModeTest::otherDacAfterUnplugIsEffective()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    removeCard(1);
    rig.mode->onCardGone(1);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->state(), State::Pw);
    QVERIFY(!rig.mode->effective());
    setCard(2, "A", dongleStream);
    DacCard other;
    other.cardIndex = 9;
    other.alsaCard = 2;
    other.reserveName = QStringLiteral("Audio2");
    rig.dac.sinkCards.insert(dongleSink, other);
    rig.sink = dongleSink;
    rig.mode->onSinkAppeared(50, dongleSink, 2);
    QTRY_VERIFY_WITH_TIMEOUT(rig.mode->effective(), waitMs);
    QCOMPARE(rig.mode->cardId(), QStringLiteral("A"));
    QTRY_VERIFY_WITH_TIMEOUT(rig.reservation.calls.contains(QStringLiteral("acquire Audio2 A")), waitMs);

    removeCard(2);
    setCard(1, "B");
    Rig second;
    QVERIFY(second.enter(QStringLiteral("bitperfect")));
    removeCard(1);
    second.mode->onCardGone(1);
    QVERIFY(!second.mode->effective());
    second.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    setCard(2, "A", dongleStream);
    second.dac.sinkCards.insert(dongleSink, other);
    second.mode->onActiveOutputChanged(QStringLiteral("usb"), dongleSink);
    QTRY_VERIFY_WITH_TIMEOUT(second.mode->effective() && second.mode->cardId() == QStringLiteral("A"), waitMs);
    QCOMPARE(second.mode->state(), State::Bp);
}

void OutputModeTest::sinkReturnWhilePlayingStoresAtPause()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.play(1);
    rig.mode->onSinkAppeared(41, replugSink, 1);
    QTRY_VERIFY_WITH_TIMEOUT(rig.mixer->writes.contains(80), waitMs);
    QTest::qWait(50);
    QCOMPARE(countOf(rig.volume.calls, QStringLiteral("store sync")), 0);
    rig.player.pause();
    QCOMPARE(countOf(rig.volume.calls, QStringLiteral("store sync B")), 1);
    QVERIFY(rig.volume.storeAllowed);
}

void OutputModeTest::outputErrorOpenWriteUnavailable()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.seek(1000);
    rig.mode->outputError(GST_RESOURCE_ERROR_OPEN_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->reason(), Reason::Unavailable);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built bp playing 1 1000")));
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("mark gone")), 0);
}

void OutputModeTest::outputErrorCardGoneUnplug()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    removeCard(1);
    rig.mode->outputError(GST_RESOURCE_ERROR_OPEN_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("mark gone")), 1, waitMs);
    QVERIFY(rig.settled());
    QCOMPARE(rig.mode->state(), State::Pw);
    QCOMPARE(rig.player.chain, Chain::Pw);
}

void OutputModeTest::outputErrorWriteReleasesAndReacquires()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->outputError(GST_RESOURCE_ERROR_WRITE);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("release")), 1, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QVERIFY(indexOf(rig.log, QStringLiteral("inputs")) < indexOf(rig.log, QStringLiteral("reserve acquire"),
                                                                    indexOf(rig.log, QStringLiteral("reserve release"))));
}

void OutputModeTest::restoreWaitsForResolveAndReady()
{
    Rig rig;
    rig.bluez.setReady(false);
    rig.mode->setLevel(QStringLiteral("exclusive"));
    rig.player.playPaused(1, 3000);
    rig.player.park(3000);
    rig.mode->restorePlaying(true);
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    QTRY_COMPARE_WITH_TIMEOUT(rig.volume.route, DacVolumePort::Route::Alsa, waitMs);
    QTest::qWait(30);
    QCOMPARE(rig.player.status, Play::Paused);
    rig.mode->onApplied(QStringLiteral("auto"), false);
    QTest::qWait(30);
    QCOMPARE(rig.player.status, Play::Paused);
    rig.bluez.setReady(true);
    QCOMPARE(rig.player.status, Play::Playing);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->state(), State::ExHeld, waitMs);
    QVERIFY(rig.player.calls.contains(QStringLiteral("built ex playing 1 3000")));
}

void OutputModeTest::restoreBoundsAlone()
{
    OutputMode::Bounds bounds = shortBounds();
    bounds.resolveMs = 1000;
    bounds.readyMs = 300;
    Rig rig(bounds);
    rig.bluez.setReady(false);
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.playPaused(1, 500);
    QElapsedTimer clock;
    clock.start();
    rig.mode->restorePlaying(true);
    QTRY_COMPARE_WITH_TIMEOUT(rig.player.status, Play::Playing, waitMs);
    QVERIFY(clock.elapsed() >= 1170);

    Rig second(bounds);
    second.bluez.setReady(false);
    QVERIFY(second.enter(QStringLiteral("bitperfect")));
    second.player.playPaused(1, 500);
    clock.restart();
    second.mode->restorePlaying(true);
    second.mode->onApplied(QStringLiteral("auto"), true);
    QTRY_COMPARE_WITH_TIMEOUT(second.player.status, Play::Playing, waitMs);
    QVERIFY(clock.elapsed() >= 270);
    QVERIFY(clock.elapsed() < 1000);
}

void OutputModeTest::restoreStaysPausedWithTransportActive()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.bluez.setTransportState(QStringLiteral("active"));
    QVERIFY(rig.bluez.transportActive());
    rig.player.playPaused(1, 500);
    rig.mode->restorePlaying(true);
    rig.mode->onApplied(QStringLiteral("auto"), true);
    QTest::qWait(400);
    QCOMPARE(rig.player.status, Play::Paused);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("built bp playing")), 0);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("resume")), 0);
}

void OutputModeTest::restoreDroppedByUserPlay()
{
    Rig rig;
    rig.bluez.setReady(false);
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.playPaused(1, 500);
    rig.mode->restorePlaying(true);
    rig.mode->onApplied(QStringLiteral("auto"), true);
    rig.player.resume();
    QCOMPARE(rig.player.status, Play::Playing);
    rig.player.pause();
    QCOMPARE(rig.player.status, Play::Paused);
    rig.bluez.setReady(true);
    QTest::qWait(400);
    QCOMPARE(rig.player.status, Play::Paused);
}

void OutputModeTest::restoreNeverWithLevelOff()
{
    Rig rig;
    QVERIFY(!rig.mode->levelSet());
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.player.playPaused(1, 500);
    rig.mode->restorePlaying(true);
    rig.mode->onApplied(QStringLiteral("auto"), true);
    QTest::qWait(400);
    QCOMPARE(rig.player.status, Play::Paused);
}

void OutputModeTest::switchWithoutLevelIsSynchronous()
{
    Rig rig;
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    rig.mode->releaseForSwitch();
    QCOMPARE(ready.count(), 1);
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    rig.player.play(1);
    rig.mode->releaseForSwitch();
    QCOMPARE(ready.count(), 2);
}

void OutputModeTest::sleepDuringReleaseWaits()
{
    Rig rig;
    rig.autoRelease = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
    rig.mode->prepareForSleep(true);
    QCOMPARE(rig.reservation.calls.last(), QStringLiteral("wait released 120"));
    rig.mode->onReleased();
    rig.mode->prepareForSleep(false);
    QVERIFY(rig.settled());
    QVERIFY(rig.dac.calls.contains(QStringLiteral("inputs")));
    QCOMPARE(rig.mode->bitPerfectSink(), fiioSink);
}

void OutputModeTest::sleepDuringReleaseBound()
{
    Rig rig;
    rig.autoRelease = false;
    rig.reservation.releaseCompletes = false;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.player.stop();
    QElapsedTimer clock;
    clock.start();
    rig.mode->prepareForSleep(true);
    QVERIFY(clock.elapsed() >= 110);
    QVERIFY(clock.elapsed() < 1000);
    QCOMPARE(rig.reservation.calls.last(), QStringLiteral("wait released 120"));
}

void OutputModeTest::statusSuffixOrder()
{
    Rig rig;
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("off"));
    rig.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("usb only"));
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on"));
    rig.player.source = {{QStringLiteral("format"), QStringLiteral("S16LE")}, {QStringLiteral("rate"), 44100},
                         {QStringLiteral("channels"), 2}};
    rig.player.sink = {{QStringLiteral("format"), QStringLiteral("F32LE")}, {QStringLiteral("rate"), 44100},
                       {QStringLiteral("channels"), 2}};
    rig.player.play(1);
    rig.mode->onHwRateChanged(44100);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on"));
    rig.bluez.setTransportState(QStringLiteral("active"));
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on (mixed)"));
    rig.player.source.insert(QStringLiteral("channels"), 6);
    rig.mode->onFormatChanged();
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on (converted)"));
    rig.mode->onHwRateChanged(48000);
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on (resampled)"));
    rig.dac.volumeDb = -25.0;
    rig.mode->onSinkChanged(3);
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->levelStatus(), QStringLiteral("on (volume)"), waitMs);
    rig.player.chain = Chain::Pw;
    rig.mode->onFormatChanged();
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on (pending)"));
    rig.player.pause();
    QCOMPARE(rig.mode->levelStatus(), QStringLiteral("on"));
    rig.player.source.insert(QStringLiteral("format"), QStringLiteral("F64LE"));
    rig.player.source.insert(QStringLiteral("channels"), 2);
    rig.player.chain = Chain::Bp;
    rig.dac.volumeDb = -20.0;
    rig.mode->onVolumePressed();
    rig.mode->onHwRateChanged(44100);
    rig.bluez.setTransportState(QStringLiteral("none"));
    rig.player.resume();
    QTRY_COMPARE_WITH_TIMEOUT(rig.mode->levelStatus(), QStringLiteral("on (converted)"), waitMs);
}

void OutputModeTest::ratesAndFixedRate()
{
    Rig rig;
    rig.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    QCOMPARE(rig.mode->rates().size(), 6);
    rig.mode->setSampleRate(QStringLiteral("352800"));
    QCOMPARE(rig.mode->fixedRate(), 0);
    rig.mode->setSampleRate(QStringLiteral("96000"));
    QCOMPARE(rig.mode->fixedRate(), 96000);
    rig.player.play(1);
    rig.mode->setSampleRate(QStringLiteral("192000"));
    QVERIFY(rig.player.calls.contains(QStringLiteral("rebuild 0 playing")));
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    QCOMPARE(rig.mode->rates().size(), 8);
    QVERIFY(rig.mode->rateReadOnly());
    QCOMPARE(rig.mode->fixedRate(), 0);
    rig.mode->setLevel(QStringLiteral("off"));
    QVERIFY(!rig.mode->rateReadOnly());
    rig.mode->setSampleRate(QStringLiteral("384000"));
    QCOMPARE(rig.mode->fixedRate(), 384000);
}

void OutputModeTest::exclusiveSinkFormat()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QCOMPARE(rig.mode->exclusiveCard(), QStringLiteral("B"));
    QCOMPARE(rig.mode->exclusiveSink(),
             QStringLiteral("audioconvert dithering=none noise-shaping=none ! capsfilter caps=audio/x-raw,format=S32LE "
                            "! alsasink device=hw:CARD=B,DEV=0"));
    setCard(1, "A", dongleStream);
    Rig dongle;
    QVERIFY(dongle.enter(QStringLiteral("exclusive")));
    QVERIFY(dongle.mode->exclusiveSink().contains(QStringLiteral("format=S24LE ! alsasink device=hw:CARD=A,DEV=0")));
    QCOMPARE(dongle.mode->rates(), QVariantList{48000});
}

void OutputModeTest::bitPerfectSinkFollowsUsb()
{
    Rig rig;
    QCOMPARE(rig.mode->bitPerfectSink(), QString());
    QCOMPARE(rig.mode->bitPerfectChain(), Chain::Pw);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), QString());
    QTest::qWait(100);
    QCOMPARE(rig.mode->bitPerfectSink(), QString());
    QVERIFY(rig.mode->chainFor(GST_STATE_PLAYING) != Chain::Bp);
    rig.player.play(1);
    QVERIFY(rig.player.chain != Chain::Bp);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("built bp")), 0);
    rig.player.stop();

    Rig known;
    known.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    QCOMPARE(known.mode->bitPerfectSink(), fiioSink);
    QCOMPARE(known.mode->bitPerfectChain(), Chain::Bp);
    QVERIFY(known.enter(QStringLiteral("bitperfect")));
    QCOMPARE(known.mode->chainFor(GST_STATE_PLAYING), Chain::Bp);
    known.player.play(1);
    QCOMPARE(known.player.chain, Chain::Bp);
}

bool OutputModeTest::enterSwitchPath(Rig &rig, int path)
{
    if (path == 0) {
        setRestartDelay(QStringLiteral("0.3"));
        if (!rig.enter(QStringLiteral("exclusive")) || !rig.restart.restartForNet(QStringLiteral("test")))
            return false;
        rig.player.play(1);
        return rig.player.pending.has_value();
    }
    if (path == 1) {
        if (!rig.enter(QStringLiteral("bitperfect")))
            return false;
        rig.player.play(1);
        return rig.player.chain == Chain::Bp;
    }
    return rig.enter(QStringLiteral("exclusive")) && rig.hold();
}

void OutputModeTest::finishSwitchPath(Rig &rig, int path)
{
    if (path == 0)
        QTRY_VERIFY_WITH_TIMEOUT(!rig.restart.restarting(), waitMs);
    setRestartDelay(QStringLiteral("0"));
}

void OutputModeTest::cardChangedAtAssignments()
{
    Rig rig;
    QStringList events;
    QObject::connect(rig.mode.get(), &OutputMode::cardChanged, rig.mode.get(), [&events](const QString &id, int alsa) {
        events.append(QStringLiteral("card %1 %2").arg(id).arg(alsa));
    });
    QObject::connect(rig.mode.get(), &OutputMode::cardGoneChanged, rig.mode.get(), [&events](bool gone) {
        events.append(QStringLiteral("gone %1").arg(gone ? 1 : 0));
    });
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QCOMPARE(events, QStringList{QStringLiteral("card B 1")});
    QCOMPARE(rig.mode->alsaCard(), 1);
    QVERIFY(rig.hold());
    QCOMPARE(events, (QStringList{QStringLiteral("card B 1"), QStringLiteral("card B 1")}));

    removeCard(1);
    rig.mode->onCardGone(1);
    QVERIFY(rig.settled());
    QCOMPARE(events.last(), QStringLiteral("gone 1"));
    events.clear();
    setCard(2, "A", dongleStream);
    DacCard other;
    other.cardIndex = 9;
    other.alsaCard = 2;
    other.reserveName = QStringLiteral("Audio2");
    rig.dac.sinkCards.insert(dongleSink, other);
    rig.sink = dongleSink;
    rig.mode->onSinkAppeared(50, dongleSink, 2);
    QTRY_VERIFY_WITH_TIMEOUT(events.size() >= 2, waitMs);
    QCOMPARE(events.at(0), QStringLiteral("gone 0"));
    QCOMPARE(events.at(1), QStringLiteral("card A 2"));
    QCOMPARE(rig.mode->alsaCard(), 2);
}

void OutputModeTest::generationAfterGoneDecision()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("bitperfect")));
    QStringList events;
    OutputMode *mode = rig.mode.get();
    QObject::connect(mode, &OutputMode::cardGoneChanged, mode, [&events](bool gone) {
        events.append(QStringLiteral("gone %1").arg(gone ? 1 : 0));
    });
    QObject::connect(mode, &OutputMode::generationChanged, mode,
                     [&events, mode](const QString &id, quint64 generation, bool present) {
                         QCOMPARE(generation, mode->generation());
                         QCOMPARE(mode->effective(), present);
                         events.append(QStringLiteral("generation %1 %2 %3").arg(id).arg(generation).arg(present ? 1 : 0));
                     });
    rig.mode->onMixerDisconnected();
    QCOMPARE(events, QStringList{QStringLiteral("generation B 1 1")});
    QVERIFY(rig.mode->effective());

    removeCard(1);
    rig.mode->onCardGone(1);
    QCOMPARE(events, (QStringList{QStringLiteral("generation B 1 1"), QStringLiteral("gone 1"),
                                  QStringLiteral("generation B 2 0")}));
    QVERIFY(!rig.mode->effective());
}

void OutputModeTest::cardGoneOnlyOnChange()
{
    Rig rig;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.sinkListed = false;
    rig.player.pause();
    QTRY_COMPARE_WITH_TIMEOUT(rig.output.held, HeldOutputPort::HeldState::Returning, waitMs);
    QSignalSpy gone(rig.mode.get(), &OutputMode::cardGoneChanged);
    removeCard(1);
    rig.mode->onCardGone(1);
    QVERIFY(!rig.mode->releasing());
    QCOMPARE(gone.count(), 1);
    QCOMPARE(gone.at(0).at(0).toBool(), true);
    rig.mode->onCardGone(1);
    QCOMPARE(gone.count(), 1);

    setCard(1, "B");
    rig.sinkListed = true;
    rig.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    QCOMPARE(gone.count(), 2);
    QCOMPARE(gone.at(1).at(0).toBool(), false);
    QVERIFY(rig.settled());
    rig.mode->onSinkAppeared(60, fiioSink, 1);
    QTest::qWait(50);
    QCOMPARE(gone.count(), 2);

    removeCard(1);
    rig.mode->onCardGone(1);
    QCOMPARE(gone.count(), 3);
    QCOMPARE(gone.at(2).at(0).toBool(), true);
    setCard(1, "B");
    rig.mode->onSinkAppeared(61, replugSink, 1);
    QTRY_COMPARE_WITH_TIMEOUT(gone.count(), 4, waitMs);
    QCOMPARE(gone.at(3).at(0).toBool(), false);
    QTest::qWait(50);
    QCOMPARE(gone.count(), 4);
}

void OutputModeTest::exHeldChangedOnTransitions()
{
    Rig rig;
    QList<bool> events;
    OutputMode *mode = rig.mode.get();
    QObject::connect(mode, &OutputMode::exHeldChanged, mode, [&events, mode](bool held) {
        QCOMPARE(mode->exHeld(), held);
        events.append(held);
    });
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QTRY_COMPARE_WITH_TIMEOUT(rig.volume.route, DacVolumePort::Route::Alsa, waitMs);
    QCOMPARE(rig.mode->state(), State::Bp);
    rig.mode->setLevel(QStringLiteral("exclusive"));
    QCOMPARE(rig.mode->state(), State::ExIdle);
    QVERIFY(events.isEmpty());

    QVERIFY(rig.hold());
    QCOMPARE(events, QList<bool>{true});
    rig.player.stop();
    QCOMPARE(events, (QList<bool>{true, false}));
    QVERIFY(rig.settled());

    QVERIFY(rig.hold());
    QCOMPARE(events.size(), 3);
    rig.mode->onReleaseRequested(20);
    QCOMPARE(events, (QList<bool>{true, false, true, false}));
    rig.mode->onNameLost(true);
    QVERIFY(!rig.mode->releasing());

    Rig sleeper;
    QList<bool> slept;
    OutputMode *other = sleeper.mode.get();
    QObject::connect(other, &OutputMode::exHeldChanged, other, [&slept](bool held) { slept.append(held); });
    setPcm(1, "RUNNING");
    QVERIFY(sleeper.enter(QStringLiteral("exclusive")));
    sleeper.player.play(1);
    QTRY_COMPARE_WITH_TIMEOUT(sleeper.output.held, HeldOutputPort::HeldState::Held, waitMs);
    QCOMPARE(sleeper.mode->state(), State::ExAcquiring);
    QVERIFY(slept.isEmpty());
    sleeper.mode->prepareForSleep(true);
    QCOMPARE(slept, QList<bool>{true});
    sleeper.mode->prepareForSleep(false);
    QCOMPARE(slept, (QList<bool>{true, false}));
    QVERIFY(sleeper.settled());
    QCOMPARE(slept.size(), 2);
    QCOMPARE(events.size(), 4);

    Rig third;
    QList<bool> left;
    OutputMode *mode3 = third.mode.get();
    QObject::connect(mode3, &OutputMode::exHeldChanged, mode3, [&left](bool held) { left.append(held); });
    setPcm(1, "closed");
    QVERIFY(third.enter(QStringLiteral("exclusive")));
    QVERIFY(third.hold());
    third.player.pause();
    third.mode->onMixerDisconnected();
    QCOMPARE(left, (QList<bool>{true, false}));
    QVERIFY(third.settled());
    QVERIFY(third.hold());
    QCOMPARE(left, (QList<bool>{true, false, true}));
    third.mode->onActiveOutputChanged(QStringLiteral("speaker"), QString());
    QCOMPARE(left, (QList<bool>{true, false, true, false}));
    QVERIFY(third.settled());
    QCOMPARE(left.size(), 4);
}

void OutputModeTest::switchReadyDacNow()
{
    for (int path = 0; path < 3; ++path) {
        Rig rig(shortBounds(), true);
        QVERIFY(enterSwitchPath(rig, path));
        QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
        rig.mode->releaseForSwitch();
        QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("release")});
        if (path == 2)
            QCOMPARE(ready.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
        QTest::qWait(shortBounds().dacReleaseMs + 50);
        QCOMPARE(ready.count(), 1);
        finishSwitchPath(rig, path);
    }
}

void OutputModeTest::switchReadyDacDelayed()
{
    for (int path = 0; path < 3; ++path) {
        Rig rig(shortBounds(), true);
        rig.dacEq.answer = FakeDacEqPort::Answer::Delayed;
        rig.dacEq.delayMs = 100;
        QVERIFY(enterSwitchPath(rig, path));
        QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
        QElapsedTimer clock;
        qint64 at = -1;
        QObject::connect(rig.mode.get(), &OutputMode::switchReady, rig.mode.get(), [&clock, &at] { at = clock.elapsed(); });
        clock.start();
        rig.mode->releaseForSwitch();
        QCOMPARE(ready.count(), 0);
        if (path == 2) {
            QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
            QCOMPARE(ready.count(), 0);
        }
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
        QVERIFY(at >= 100);
        QVERIFY(at < shortBounds().dacReleaseMs);
        QTest::qWait(shortBounds().dacReleaseMs);
        QCOMPARE(ready.count(), 1);
        finishSwitchPath(rig, path);
    }
}

void OutputModeTest::switchReadyDacNever()
{
    for (int path = 0; path < 3; ++path) {
        Rig rig(shortBounds(), true);
        rig.dacEq.answer = FakeDacEqPort::Answer::Never;
        QVERIFY(enterSwitchPath(rig, path));
        QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
        QElapsedTimer clock;
        clock.start();
        QTest::ignoreMessage(QtInfoMsg, "microdap output: switch bound reached, dac eq not released");
        rig.mode->releaseForSwitch();
        QCOMPARE(ready.count(), 0);
        if (path == 2) {
            QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
            QCOMPARE(ready.count(), 0);
        }
        QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
        QVERIFY(clock.elapsed() >= shortBounds().dacReleaseMs);
        QCOMPARE(rig.dacEq.pending.size(), 1);
        rig.dacEq.pending.first()();
        QTest::qWait(50);
        QCOMPARE(ready.count(), 1);
        finishSwitchPath(rig, path);
    }
}

void OutputModeTest::switchReadyLateCallback()
{
    Rig rig(shortBounds(), true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Never;
    QVERIFY(enterSwitchPath(rig, 1));
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    rig.mode->releaseForSwitch();
    QTest::qWait(shortBounds().dacReleaseMs / 2);
    rig.mode->releaseForSwitch();
    QCOMPARE(rig.dacEq.pending.size(), 2);
    rig.dacEq.pending.at(0)();
    QCOMPARE(ready.count(), 0);
    QTest::qWait(shortBounds().dacReleaseMs / 2 + 30);
    QCOMPARE(ready.count(), 0);
    rig.dacEq.pending.at(1)();
    QCOMPARE(ready.count(), 1);
    QTest::qWait(shortBounds().dacReleaseMs + 50);
    QCOMPARE(ready.count(), 1);
}

void OutputModeTest::switchRetryJoinsWithDac()
{
    Rig rig(shortBounds(), true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Never;
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
    rig.mode->releaseForSwitch();
    QVERIFY(rig.mode->releasing());
    rig.mode->releaseForSwitch();
    QVERIFY(rig.mode->switchStoppedStream());
    QCOMPARE(rig.dacEq.pending.size(), 2);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), waitMs);
    QCOMPARE(ready.count(), 0);
    rig.dacEq.pending.at(0)();
    QCOMPARE(ready.count(), 0);
    rig.dacEq.pending.at(1)();
    QCOMPARE(ready.count(), 1);
    rig.dacEq.pending.at(1)();
    QTest::qWait(shortBounds().dacReleaseMs + 50);
    QCOMPARE(ready.count(), 1);
    QCOMPARE(countOf(rig.reservation.calls, QStringLiteral("release")), 1);
}

void OutputModeTest::switchReadyNoDacEq()
{
    for (int path = 0; path < 3; ++path) {
        Rig rig;
        QVERIFY(enterSwitchPath(rig, path));
        QSignalSpy ready(rig.mode.get(), &OutputMode::switchReady);
        rig.mode->releaseForSwitch();
        QCOMPARE(rig.mode->switchStoppedStream(), path == 2);
        if (path == 2) {
            QCOMPARE(ready.count(), 0);
            QTRY_COMPARE_WITH_TIMEOUT(ready.count(), 1, waitMs);
        } else {
            QCOMPARE(ready.count(), 1);
        }
        QTest::qWait(shortBounds().dacReleaseMs + 50);
        QCOMPARE(ready.count(), 1);
        QVERIFY(rig.dacEq.calls.isEmpty());
        finishSwitchPath(rig, path);
    }
}

void OutputModeTest::switchBoundIncludesDac()
{
    QCOMPARE(OutputMode::Bounds{}.dacReleaseMs, 500);
    Rig plain;
    Rig withDac(shortBounds(), true);
    QCOMPARE(plain.mode->switchBoundMs(), shortBounds().reservation.callMs + shortBounds().switchMs);
    QCOMPARE(withDac.mode->switchBoundMs(), plain.mode->switchBoundMs() + shortBounds().dacReleaseMs);
}

void OutputModeTest::quitOrder()
{
    Rig rig(shortBounds(), true);
    QVERIFY(rig.enter(QStringLiteral("exclusive")));
    QVERIFY(rig.hold());
    rig.mode->aboutToQuit();
    const int park = indexOf(rig.log, QStringLiteral("player park"));
    const int reserve = indexOf(rig.log, QStringLiteral("reserve quit 50"));
    const int dac = indexOf(rig.log, QStringLiteral("daceq quit 500"));
    QVERIFY(park >= 0);
    QVERIFY(park < reserve);
    QVERIFY(reserve < dac);
    QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("quit 500")});
}

QTEST_GUILESS_MAIN(OutputModeTest)
#include "tst_outputmode.moc"
