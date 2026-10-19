#include "BluezModel.h"
#include "FakeBluezModel.h"
#include "Player.h"
#include "Queue.h"

#include <QDir>
#include <QDirIterator>
#include <QtTest>

#include <gst/gst.h>

using namespace microdap;

class BluezTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void volumeMapping();
    void propertySignals();
    void commands();
    void presets();
    void transportPausesPlayer();
    void transportActiveAtInstall();
    void roleSwitch();
    void roleSwitchTimeout();
    void windowsWaitForTheRole();
    void wrongRole();

private:
    QueueEntry entry() const;

    QString m_track;
};

QueueEntry BluezTest::entry() const
{
    QueueEntry item;
    item.path = m_track;
    item.title = QStringLiteral("fixture");
    return item;
}

void BluezTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("MICRODAP_AUDIO_SINK"))
        qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));

    QDirIterator it(QStringLiteral(MICRODAP_FIXTURE_MUSIC), {QStringLiteral("*.flac")}, QDir::Files,
                    QDirIterator::Subdirectories);
    if (it.hasNext())
        m_track = it.next();
    QVERIFY2(!m_track.isEmpty(), "no flac fixture found");
}

void BluezTest::volumeMapping()
{
    QCOMPARE(BluezModel::volumeFromTransport(64), 50);
    QCOMPARE(BluezModel::volumeFromTransport(0), 0);
    QCOMPARE(BluezModel::volumeFromTransport(127), 100);
    QCOMPARE(BluezModel::volumeFromTransport(200), 100);
    QCOMPARE(BluezModel::volumeFromTransport(-5), 0);
    QCOMPARE(BluezModel::volumeToTransport(50), 64);
    QCOMPARE(BluezModel::volumeToTransport(100), 127);

    FakeBluezModel fake;
    QSignalSpy spy(&fake, &BluezModel::volumeChanged);
    QSignalSpy phone(&fake, &BluezModel::phoneVolumeChanged);
    fake.setTransportVolume(64);
    QCOMPARE(fake.volume(), 50);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toInt(), 50);
    QCOMPARE(phone.count(), 0);
    fake.setTransportVolume(64);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(phone.count(), 0);
    fake.setVolume(130);
    QCOMPARE(fake.volume(), 100);
    QCOMPARE(phone.count(), 1);
    QCOMPARE(phone.at(0).at(0).toInt(), 100);
    fake.applyPreset(QStringLiteral("off"));
    fake.setTransportVolume(32);
    QCOMPARE(fake.volume(), 25);
    QCOMPARE(spy.count(), 4);
    QCOMPARE(phone.count(), 1);
}

void BluezTest::propertySignals()
{
    FakeBluezModel fake;
    QVERIFY(!fake.connected());
    QVERIFY(!fake.transportActive());
    QCOMPARE(fake.status(), QStringLiteral("stopped"));
    QCOMPARE(fake.battery(), -1);
    QCOMPARE(fake.shuffle(), QStringLiteral("off"));
    QCOMPARE(fake.repeat(), QStringLiteral("off"));

    QSignalSpy connectedSpy(&fake, &BluezModel::connectedChanged);
    QSignalSpy nameSpy(&fake, &BluezModel::deviceNameChanged);
    QSignalSpy activeSpy(&fake, &BluezModel::transportActiveChanged);
    QSignalSpy titleSpy(&fake, &BluezModel::titleChanged);
    QSignalSpy batterySpy(&fake, &BluezModel::batteryChanged);
    QSignalSpy pairingSpy(&fake, &BluezModel::pairingChanged);

    fake.setConnected(true);
    fake.setConnected(true);
    QCOMPARE(connectedSpy.count(), 1);
    fake.setDeviceName(QStringLiteral("Pixel 8"));
    QCOMPARE(nameSpy.count(), 1);
    QCOMPARE(fake.deviceName(), QStringLiteral("Pixel 8"));
    fake.setTransportActive(true);
    fake.setTransportActive(false);
    QCOMPARE(activeSpy.count(), 2);
    fake.setTrack(QStringLiteral("t"), QStringLiteral("a"), QStringLiteral("b"), 4, 11, 228000);
    QCOMPARE(titleSpy.count(), 1);
    QCOMPARE(fake.trackNumber(), 4);
    QCOMPARE(fake.numberOfTracks(), 11);
    QCOMPARE(fake.durationMs(), qint64(228000));
    fake.setBattery(73);
    fake.setBattery(150);
    QCOMPARE(fake.battery(), 100);
    QCOMPARE(batterySpy.count(), 2);
    fake.setPairing(true);
    QCOMPARE(pairingSpy.count(), 1);

    BluezModel *base = &fake;
    base->setShuffle(QStringLiteral("alltracks"));
    base->setRepeat(QStringLiteral("singletrack"));
    QCOMPARE(fake.shuffle(), QStringLiteral("alltracks"));
    QCOMPARE(fake.repeat(), QStringLiteral("singletrack"));
}

void BluezTest::commands()
{
    FakeBluezModel fake;
    BluezModel *base = &fake;
    fake.setTrack({}, {}, {}, 2, 5, 0);

    base->play();
    QCOMPARE(fake.status(), QStringLiteral("playing"));
    base->pause();
    QCOMPARE(fake.status(), QStringLiteral("paused"));
    base->next();
    QCOMPARE(fake.trackNumber(), 3);
    base->previous();
    base->previous();
    QCOMPARE(fake.trackNumber(), 1);
    base->previous();
    QCOMPARE(fake.trackNumber(), 1);
    base->fastForward();
    QCOMPARE(fake.status(), QStringLiteral("forward-seek"));
    base->rewind();
    QCOMPARE(fake.status(), QStringLiteral("reverse-seek"));

    base->startPairing();
    QVERIFY(fake.pairing());
    QCOMPARE(fake.pairingRemaining(), BluezModel::defaultPairingTimeout);
    base->stopPairing();
    QVERIFY(!fake.pairing());
    QCOMPARE(fake.pairingRemaining(), 0);
    base->startPairing(60);
    QCOMPARE(fake.pairingRemaining(), 60);

    QCOMPARE(fake.commands(),
             QStringList({QStringLiteral("play"), QStringLiteral("pause"), QStringLiteral("next"),
                          QStringLiteral("previous"), QStringLiteral("previous"),
                          QStringLiteral("previous"), QStringLiteral("fastForward"),
                          QStringLiteral("rewind"), QStringLiteral("startPairing"),
                          QStringLiteral("stopPairing"), QStringLiteral("startPairing")}));
    fake.clearCommands();
    QVERIFY(fake.commands().isEmpty());
}

void BluezTest::presets()
{
    FakeBluezModel fake;
    QCOMPARE(FakeBluezModel::presets().size(), 6);
    QVERIFY(!fake.applyPreset(QStringLiteral("bogus")));
    QVERIFY(fake.lastPhone().isEmpty());
    QVERIFY(fake.applyPreset(QStringLiteral("last")));
    QVERIFY(!fake.connected());
    QCOMPARE(fake.lastPhone(), QStringLiteral("Pixel 8"));
    QVERIFY(fake.applyPreset(QStringLiteral("off")));

    QVERIFY(fake.applyPreset(QStringLiteral("connected")));
    QVERIFY(fake.connected());
    QCOMPARE(fake.deviceName(), QStringLiteral("Pixel 8"));
    QCOMPARE(fake.battery(), 73);
    QVERIFY(!fake.transportActive());

    QVERIFY(fake.applyPreset(QStringLiteral("active")));
    QVERIFY(fake.connected());
    QVERIFY(fake.transportActive());
    QCOMPARE(fake.status(), QStringLiteral("playing"));
    QCOMPARE(fake.trackNumber(), 4);
    QCOMPARE(fake.numberOfTracks(), 11);
    QCOMPARE(fake.positionMs(), qint64(125000));
    QCOMPARE(fake.durationMs(), qint64(228000));
    QCOMPARE(fake.volume(), 50);
    QVERIFY(fake.artUrl().isEmpty());

    QVERIFY(fake.applyPreset(QStringLiteral("art")));
    QVERIFY(fake.transportActive());
    QCOMPARE(fake.title(), QStringLiteral("Track Four"));
    QVERIFY(QFileInfo(QUrl(fake.artUrl()).toLocalFile()).size() > 0);

    QVERIFY(fake.applyPreset(QStringLiteral("pairing")));
    QVERIFY(fake.artUrl().isEmpty());
    QVERIFY(fake.pairing());
    QCOMPARE(fake.pairingRemaining(), 73);
    QVERIFY(!fake.connected());
    QVERIFY(!fake.transportActive());

    QVERIFY(fake.applyPreset(QStringLiteral("off")));
    QVERIFY(!fake.connected());
    QVERIFY(!fake.pairing());
    QVERIFY(fake.deviceName().isEmpty());
    QCOMPARE(fake.battery(), -1);
}

void BluezTest::transportPausesPlayer()
{
    Player player;
    QVERIFY(player.isValid());
    FakeBluezModel fake;
    QVERIFY(player.bluez() == nullptr);
    player.setBluez(&fake);
    QCOMPARE(player.bluez(), static_cast<BluezModel *>(&fake));

    player.play({entry()}, 0);
    QCOMPARE(player.status(), Player::Playing);

    fake.setTransportActive(true);
    QCOMPARE(player.status(), Player::Paused);

    fake.setTransportActive(false);
    QCOMPARE(player.status(), Player::Paused);

    player.playPause();
    QCOMPARE(player.status(), Player::Playing);

    fake.setTransportActive(true);
    QCOMPARE(player.status(), Player::Paused);
    fake.setTransportActive(false);

    player.setBluez(nullptr);
    QVERIFY(player.bluez() == nullptr);
    player.playPause();
    QCOMPARE(player.status(), Player::Playing);
    fake.setTransportActive(true);
    QCOMPARE(player.status(), Player::Playing);
    player.stop();
}

void BluezTest::transportActiveAtInstall()
{
    Player player;
    QVERIFY(player.isValid());
    player.play({entry()}, 0);
    QCOMPARE(player.status(), Player::Playing);

    auto *fake = new FakeBluezModel;
    fake->setTransportActive(true);
    player.setBluez(fake);
    QCOMPARE(player.status(), Player::Paused);

    delete fake;
    QVERIFY(player.bluez() == nullptr);
    player.playPause();
    QCOMPARE(player.status(), Player::Playing);
    player.stop();
}

void BluezTest::roleSwitch()
{
    FakeBluezModel fake;
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QVERIFY(!fake.switching());
    QSignalSpy roleSpy(&fake, &BluezModel::roleChanged);
    QSignalSpy switchingSpy(&fake, &BluezModel::switchingChanged);
    fake.setRole(QStringLiteral("receiver"));
    fake.setRole(QStringLiteral("source"));
    QVERIFY(fake.commands().isEmpty());

    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    fake.setRole(QStringLiteral("headphones"));
    QVERIFY(fake.switching());
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QCOMPARE(fake.activeRole(), QStringLiteral("headphones"));
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("applyRole headphones"), QStringLiteral("dropRole receiver")}));
    QCOMPARE(roleSpy.count(), 0);
    fake.setRole(QStringLiteral("headphones"));
    QCOMPARE(fake.commands().size(), 2);
    fake.setRoleDevices(QStringLiteral("receiver"), 0);
    QVERIFY(!fake.switching());
    QCOMPARE(fake.role(), QStringLiteral("headphones"));
    QCOMPARE(roleSpy.count(), 1);
    QCOMPARE(switchingSpy.count(), 2);

    fake.clearCommands();
    fake.setRole(QStringLiteral("receiver"));
    QVERIFY(!fake.switching());
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("applyRole receiver"), QStringLiteral("dropRole headphones")}));

    fake.clearCommands();
    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    fake.setRole(QStringLiteral("headphones"));
    QVERIFY(fake.switching());
    fake.setRole(QStringLiteral("receiver"));
    QVERIFY(!fake.switching());
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QCOMPARE(fake.commands(), QStringList({QStringLiteral("applyRole headphones"), QStringLiteral("dropRole receiver"),
                                           QStringLiteral("applyRole receiver")}));
}

void BluezTest::roleSwitchTimeout()
{
    qputenv("MICRODAP_BT_SWITCH_MS", QByteArrayLiteral("300"));
    const auto restore = qScopeGuard([] { qunsetenv("MICRODAP_BT_SWITCH_MS"); });
    FakeBluezModel fake;
    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    QElapsedTimer timer;
    timer.start();
    fake.setRole(QStringLiteral("headphones"));
    QVERIFY(fake.switching());
    QTRY_VERIFY_WITH_TIMEOUT(!fake.switching(), 3000);
    QVERIFY(timer.elapsed() >= 250);
    QCOMPARE(fake.role(), QStringLiteral("headphones"));
}

void BluezTest::windowsWaitForTheRole()
{
    FakeBluezModel fake;
    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    fake.startSearch(120);
    QVERIFY(fake.switching());
    QVERIFY(!fake.searching());
    QVERIFY(!fake.commands().contains(QStringLiteral("startSearch")));
    fake.setRoleDevices(QStringLiteral("receiver"), 0);
    QCOMPARE(fake.role(), QStringLiteral("headphones"));
    QVERIFY(fake.searching());
    QCOMPARE(fake.searchRemaining(), 120);
    fake.stopSearch();
    QVERIFY(!fake.searching());

    fake.setRoleDevices(QStringLiteral("headphones"), 1);
    fake.startPairing(60);
    QVERIFY(fake.switching());
    fake.stopPairing();
    fake.setRoleDevices(QStringLiteral("headphones"), 0);
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QVERIFY(!fake.pairing());
    fake.startPairing(20);
    QVERIFY(fake.pairing());
    fake.clearCommands();
    fake.setRole(QStringLiteral("headphones"));
    QVERIFY(!fake.pairing());
    QCOMPARE(fake.commands().first(), QStringLiteral("stopPairing"));
    fake.startSearch(120);
    QVERIFY(fake.searching());
    fake.setRole(QStringLiteral("receiver"));
    QVERIFY(!fake.searching());
    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    fake.startSearch(120);
    QVERIFY(fake.switching());
    fake.setRoleDevices(QStringLiteral("receiver"), 0);
    QVERIFY(fake.searching());
    fake.stopSearch();

    fake.clearCommands();
    fake.setRoleDevices(QStringLiteral("headphones"), 1);
    fake.startPairing(60);
    QVERIFY(fake.switching());
    QVERIFY(!fake.pairing());
    fake.startSearch(30);
    QCOMPARE(fake.activeRole(), QStringLiteral("headphones"));
    QVERIFY(!fake.switching());
    QVERIFY(fake.searching());
    QCOMPARE(fake.searchRemaining(), 30);
    fake.setRoleDevices(QStringLiteral("headphones"), 0);
    QVERIFY(!fake.pairing());

    fake.setRoleDevices(QStringLiteral("headphones"), 1);
    fake.startPairing(60);
    fake.setRole(QStringLiteral("headphones"));
    fake.setRoleDevices(QStringLiteral("headphones"), 0);
    QVERIFY(!fake.pairing());
    QCOMPARE(fake.role(), QStringLiteral("headphones"));

    fake.setRoleDevices(QStringLiteral("headphones"), 1);
    fake.startPairing(45);
    QVERIFY(fake.switching());
    fake.setRoleDevices(QStringLiteral("headphones"), 0);
    QCOMPARE(fake.role(), QStringLiteral("receiver"));
    QVERIFY(fake.pairing());
    QCOMPARE(fake.pairingRemaining(), 45);
}

void BluezTest::wrongRole()
{
    FakeBluezModel fake;
    QSignalSpy spy(&fake, &BluezModel::wrongRole);
    QVERIFY(fake.reportDevice(QStringLiteral("Buds"), false, true));
    QVERIFY(!fake.reportDevice(QStringLiteral("Pixel 8"), true, false));
    QVERIFY(!fake.reportDevice(QStringLiteral("Both"), true, true));
    QVERIFY(!fake.reportDevice(QStringLiteral("Keyboard"), false, false));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("Buds"));
    QCOMPARE(spy.at(0).at(1).toString(), QStringLiteral("headphones"));

    fake.setRoleDevices(QStringLiteral("receiver"), 1);
    fake.setRole(QStringLiteral("headphones"));
    QVERIFY(!fake.reportDevice(QStringLiteral("Pixel 8"), true, false));
    QVERIFY(!fake.reportDevice(QStringLiteral("Buds"), false, true));
    fake.setRoleDevices(QStringLiteral("receiver"), 0);
    QVERIFY(fake.reportDevice(QStringLiteral("Pixel 8"), true, false));
    QVERIFY(!fake.reportDevice(QStringLiteral("Buds"), false, true));
    QVERIFY(!fake.reportDevice(QStringLiteral("Both"), true, true));
    QCOMPARE(spy.count(), 2);
    QCOMPARE(spy.at(1).at(0).toString(), QStringLiteral("Pixel 8"));
    QCOMPARE(spy.at(1).at(1).toString(), QStringLiteral("phone"));
}

QTEST_GUILESS_MAIN(BluezTest)
#include "tst_bluez.moc"
