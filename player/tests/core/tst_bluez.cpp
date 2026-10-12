#include "BluezModel.h"
#include "FakeBluezModel.h"
#include "Player.h"
#include "Queue.h"

#include <QDir>
#include <QDirIterator>
#include <QtTest>

#include <gst/gst.h>

using namespace casdap;

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
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));

    QDirIterator it(QStringLiteral(CASDAP_FIXTURE_MUSIC), {QStringLiteral("*.flac")}, QDir::Files,
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
    QCOMPARE(FakeBluezModel::presets().size(), 4);
    QVERIFY(!fake.applyPreset(QStringLiteral("bogus")));

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

    QVERIFY(fake.applyPreset(QStringLiteral("pairing")));
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

QTEST_GUILESS_MAIN(BluezTest)
#include "tst_bluez.moc"
