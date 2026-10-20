#include "Eq.h"
#include "FakeService.h"
#include "LinkSettings.h"
#include "PowerBridge.h"
#include "Rotation.h"
#include "Settings.h"
#include "SettingsRows.h"

#include <microset/client.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace microdap;

namespace {

constexpr int waitMs = 5000;

const char *autoEq =
    "Preamp: -6.2 dB\n"
    "Filter 1: ON LSC Fc 105 Hz Gain 6.3 dB Q 0.70\n"
    "Filter 2: ON PK Fc 1000 Hz Gain -3.2 dB Q 1.41\n";

bool near(double a, double b)
{
    return std::abs(a - b) < 1e-6;
}

bool writeFile(const QString &path, const QString &text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    return true;
}

}

class SettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void defaultsWithoutDaemon();
    void loadsFromDaemon();
    void writesTypedKeys();
    void rootsStoreEmpty();
    void rootsOldDefaults();
    void followsChanged();
    void refusedSetReverts();
    void daemonLater();
    void legacyMap();
    void importOnce();
    void importDroppedReply();
    void importKeepsStored();
    void bootSessions();
    void eqKeysThroughDaemon();
    void rotationSubscribe();
    void rowTable();
    void balanceCrossfeedSlot();
    void btCodecKeys();
    void volumeKeys();
    void brightnessRow();

private:
    QString ini(const QString &text);
    QStringList stored() const;

    FakeService m_set = FakeService::microSet(QStringLiteral(MICRODAP_FAKE_MICROSETD));
    std::unique_ptr<QTemporaryDir> m_dir;
};

void SettingsTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus, run under dbus-run-session");
}

void SettingsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("MICRODAP_EQ_DIR", (m_dir->path() + QStringLiteral("/config")).toUtf8());
    QVERIFY(m_set.start({m_dir->path() + QStringLiteral("/state.json")}));
}

void SettingsTest::cleanup()
{
    m_set.stop();
}

QString SettingsTest::ini(const QString &text)
{
    const QString path = m_dir->path() + QStringLiteral("/config/settings.ini");
    if (!writeFile(path, text))
        return {};
    return path;
}

QStringList SettingsTest::stored() const
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    return client.stored().keys();
}

void SettingsTest::defaultsWithoutDaemon()
{
    QVERIFY(Settings::legacyPath().endsWith(QStringLiteral("/microdap/settings.ini")));
    Settings settings(nullptr);
    QVERIFY(!settings.available());
    QCOMPARE(settings.output(), QStringLiteral("auto"));
    QCOMPARE(settings.hpGain(), QStringLiteral("high"));
    QCOMPARE(settings.hpAmp(), QStringLiteral("lohifi"));
    QCOMPARE(settings.spkGain(), 102);
    QCOMPARE(settings.volumeStep(), 5);
    QCOMPARE(settings.btCodec(), QStringLiteral("auto"));
    QCOMPARE(settings.rgb(), QStringLiteral("system"));
    QCOMPARE(settings.orientation(), QStringLiteral("portrait"));
    QVERIFY(settings.autoRotate());
    QCOMPARE(settings.roots(), Settings::defaultRoots());
    QCOMPARE(settings.accentHue(), 350);
    QCOMPARE(settings.scale(), 1.0);
    QCOMPARE(settings.saverSeconds(), 30);
    QVERIFY(!settings.saverLyrics());
    QCOMPARE(settings.screenOffSeconds(), 300);
    QCOMPARE(settings.suspendMinutes(), 10);
    QVERIFY(settings.touch());
    QCOMPARE(settings.outputLevel(), QStringLiteral("off"));
    QCOMPARE(settings.sampleRate(), QStringLiteral("auto"));
    QCOMPARE(settings.playThrough(), QStringLiteral("off"));
    QCOMPARE(settings.resume(), QStringLiteral("position"));
    QVERIFY(!settings.autoNowPlaying());
    QCOMPARE(settings.clock(), QStringLiteral("24h"));
    QVERIFY(!settings.batteryPercent());
    QVERIFY(settings.quickplay());
    settings.setPlayThrough(QStringLiteral("genres"));
    QCOMPARE(settings.playThrough(), QStringLiteral("off"));
    QCOMPARE(settings.replaygain(), QStringLiteral("off"));
    settings.setReplaygain(QStringLiteral("loud"));
    QCOMPARE(settings.replaygain(), QStringLiteral("off"));
    settings.setReplaygain(QStringLiteral("album"));
    QCOMPARE(settings.replaygain(), QStringLiteral("album"));
    settings.setReplaygain(QStringLiteral("off"));
    QCOMPARE(settings.crossfadeMs(), 0);
    settings.setCrossfadeMs(1500);
    QCOMPARE(settings.crossfadeMs(), 0);
    settings.setCrossfadeMs(3000);
    QCOMPARE(settings.crossfadeMs(), 3000);
    settings.setCrossfadeMs(0);
    settings.setResume(QStringLiteral("track"));
    QCOMPARE(settings.resume(), QStringLiteral("track"));
    settings.setResume(QStringLiteral("position"));
    QCOMPARE(settings.value(QStringLiteral("audio.eq.bt.filters")).toStringList(), QStringList{QStringLiteral("preamp 0")});
    QCOMPARE(Settings::outputs(), QStringList({QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"),
                                               QStringLiteral("usb"), QStringLiteral("bt")}));
    QCOMPARE(Settings::rgbModes(), QStringList({QStringLiteral("accent"), QStringLiteral("off"), QStringLiteral("system")}));

    QSignalSpy scale(&settings, &Settings::scaleChanged);
    settings.setScale(1.14);
    QCOMPARE(settings.scale(), 1.1);
    settings.setScale(1.1);
    QCOMPARE(scale.count(), 1);
    settings.setScale(9.0);
    QCOMPARE(settings.scale(), Settings::maxScale);
    settings.setOutput(QStringLiteral("tape"));
    QCOMPARE(settings.output(), QStringLiteral("auto"));
    settings.setBtCodec(QStringLiteral("AAC"));
    QCOMPARE(settings.btCodec(), QStringLiteral("aac"));
    QCOMPARE(settings.btCodecHeadphones(), QStringLiteral("auto"));
    settings.setBtCodecHeadphones(QStringLiteral("APTX_HD"));
    QCOMPARE(settings.btCodecHeadphones(), QStringLiteral("aptx_hd"));
    settings.setBtCodecHeadphones(QStringLiteral("opus"));
    QCOMPARE(settings.btCodecHeadphones(), QStringLiteral("aptx_hd"));
    settings.setAccentHue(198);
    QCOMPARE(settings.accentHue(), 200);
    settings.setAccentHue(-10);
    QCOMPARE(settings.accentHue(), 350);
    settings.setSaverSeconds(5);
    QCOMPARE(settings.saverSeconds(), Settings::minSaverSeconds);
    settings.setRoots({});
    QCOMPARE(settings.roots(), Settings::defaultRoots());
    settings.setOrientation(QStringLiteral("sideways"));
    QCOMPARE(settings.orientation(), QStringLiteral("portrait"));
    settings.setOutputLevel(QStringLiteral("EXCLUSIVE"));
    QCOMPARE(settings.outputLevel(), QStringLiteral("exclusive"));
    settings.setSampleRate(QStringLiteral("22050"));
    QCOMPARE(settings.sampleRate(), QStringLiteral("auto"));
    QVERIFY(settings.bootSessions().isEmpty());
    QCOMPARE(settings.brightness(), 2442);
    QVERIFY(settings.brightnessSteps().isEmpty());
    QCOMPARE(settings.brightnessStep(2442, 1), 2442);
    QTest::ignoreMessage(QtWarningMsg, "microdap settings: unknown key \"audio.nope\"");
    settings.setValue(QStringLiteral("audio.nope"), 1);
}

void SettingsTest::loadsFromDaemon()
{
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.output"), FakeService::variant(QStringLiteral("usb"))});
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.volume_step"), FakeService::variant(10u)});
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("microdap.roots"),
                                             FakeService::variant(QStringList{QStringLiteral("/music")})});
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("microdap.scale"), FakeService::variant(1.2)});
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("display.auto_rotate"), FakeService::variant(false)});
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QVERIFY(settings.available());
    QCOMPARE(settings.output(), QStringLiteral("usb"));
    QCOMPARE(settings.volumeStep(), 10);
    QCOMPARE(settings.roots(), QStringList{QStringLiteral("/music")});
    QCOMPARE(settings.scale(), 1.2);
    QVERIFY(!settings.autoRotate());
    QCOMPARE(settings.hpGain(), QStringLiteral("high"));
    QCOMPARE(settings.rgb(), QStringLiteral("system"));
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set ")).size(), 0);
}

void SettingsTest::writesTypedKeys()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    settings.setOutput(QStringLiteral("headphones"));
    settings.setVolumeStep(2);
    settings.setSpkGain(96);
    settings.setScale(1.3);
    settings.setRoots({QStringLiteral("/a"), QStringLiteral("/b")});
    settings.setAccentHue(120);
    settings.setTouch(false);
    settings.setRgb(QStringLiteral("ACCENT"));
    settings.setSaverSeconds(60);
    settings.setSaverLyrics(true);
    settings.setHpGain(QStringLiteral("low"));
    settings.setBootSession(QStringLiteral("microemu"));
    QTRY_COMPARE_WITH_TIMEOUT(m_set.calls().filter(QStringLiteral("Set ")).size(), 12, waitMs);
    QTest::qWait(100);
    QCOMPARE(client.get(QStringLiteral("audio.output")).toString(), QStringLiteral("headphones"));
    QCOMPARE(client.get(QStringLiteral("audio.volume_step")).toUInt(), 2u);
    QCOMPARE(client.get(QStringLiteral("audio.speaker_gain")).toUInt(), 96u);
    QCOMPARE(client.get(QStringLiteral("microdap.scale")).toDouble(), 1.3);
    QCOMPARE(client.get(QStringLiteral("microdap.roots")).toStringList(), QStringList({QStringLiteral("/a"), QStringLiteral("/b")}));
    QCOMPARE(client.get(QStringLiteral("microdap.accent_hue")).toUInt(), 120u);
    QCOMPARE(client.get(QStringLiteral("microdap.touch")).toBool(), false);
    QCOMPARE(client.get(QStringLiteral("microdap.rgb")).toString(), QStringLiteral("accent"));
    QCOMPARE(client.get(QStringLiteral("microdap.saver_seconds")).toUInt(), 60u);
    QCOMPARE(client.get(QStringLiteral("microdap.saver_lyrics")).toBool(), true);
    QCOMPARE(client.get(QStringLiteral("audio.hp_gain")).toString(), QStringLiteral("low"));
    QCOMPARE(client.get(QStringLiteral("session.default")).toString(), QStringLiteral("microemu"));
    QCOMPARE(settings.output(), QStringLiteral("headphones"));
    settings.setOutput(QStringLiteral("headphones"));
    settings.setBootSession(QStringLiteral("nowhere"));
    QTest::qWait(200);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set ")).size(), 12);
}

void SettingsTest::rootsStoreEmpty()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QVERIFY(settings.rootsDefault());
    settings.setRoots({QStringLiteral("/a")});
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("microdap.roots")).toStringList(), QStringList{QStringLiteral("/a")},
                              waitMs);
    QVERIFY(!settings.rootsDefault());
    QSignalSpy roots(&settings, &Settings::rootsChanged);
    settings.refreshRoots();
    QCOMPARE(roots.count(), 0);
    settings.setRoots({});
    QCOMPARE(roots.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(m_set.calls().filter(QStringLiteral("Set ")).size(), 2, waitMs);
    QCOMPARE(client.get(QStringLiteral("microdap.roots")).toStringList(), QStringList());
    QVERIFY(settings.rootsDefault());
    QCOMPARE(settings.roots(), Settings::defaultRoots());
    settings.refreshRoots();
    QCOMPARE(roots.count(), 2);
}

void SettingsTest::rootsOldDefaults()
{
    const QStringList old{QStandardPaths::writableLocation(QStandardPaths::MusicLocation), QStringLiteral("/run/media")};
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("microdap.roots"), FakeService::variant(old)});
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QVERIFY(settings.rootsDefault());
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("microdap.roots")).toStringList(), QStringList(), waitMs);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set ")).size(), 1);
    QVERIFY(Settings::legacyValues(ini(QStringLiteral("[General]\nroots=") + old.join(QLatin1Char(','))
                                       + QStringLiteral("\n")))
                .isEmpty());
}

void SettingsTest::followsChanged()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QSignalSpy output(&settings, &Settings::outputChanged);
    QSignalSpy step(&settings, &Settings::volumeStepChanged);
    QSignalSpy values(&settings, &Settings::valueChanged);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.output"), FakeService::variant(QStringLiteral("speaker"))});
    QTRY_COMPARE_WITH_TIMEOUT(output.count(), 1, waitMs);
    QCOMPARE(settings.output(), QStringLiteral("speaker"));
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.volume_step"), FakeService::variant(10u)});
    QTRY_COMPARE_WITH_TIMEOUT(step.count(), 1, waitMs);
    QCOMPARE(settings.volumeStep(), 10);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.eq.usb.on"), FakeService::variant(true)});
    QTRY_VERIFY_WITH_TIMEOUT(settings.value(QStringLiteral("audio.eq.usb.on")).toBool(), waitMs);
    QVERIFY(values.count() >= 3);
    QCOMPARE(values.last().at(0).toString(), QStringLiteral("audio.eq.usb.on"));
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("rgb.hue"), FakeService::variant(5u)});
    QTest::qWait(200);
    QCOMPARE(values.last().at(0).toString(), QStringLiteral("audio.eq.usb.on"));
}

void SettingsTest::refusedSetReverts()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    m_set.call(QStringLiteral("RefuseSet"), {QStringLiteral("microdap.scale")});
    QSignalSpy scale(&settings, &Settings::scaleChanged);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^microdap settings: set microdap.scale refused")));
    settings.setScale(1.2);
    QCOMPARE(settings.scale(), 1.2);
    QTRY_COMPARE_WITH_TIMEOUT(scale.count(), 2, waitMs);
    QCOMPARE(settings.scale(), 1.0);
    settings.setScale(1.2);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("microdap.scale")).toDouble(), 1.2, waitMs);
    QCOMPARE(settings.scale(), 1.2);
}

void SettingsTest::daemonLater()
{
    m_set.stop();
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QVERIFY(!settings.available());
    QCOMPARE(settings.output(), QStringLiteral("auto"));
    QSignalSpy available(&settings, &Settings::availableChanged);
    QSignalSpy output(&settings, &Settings::outputChanged);
    QSignalSpy cap(&settings, &Settings::maxVolumeChanged);
    int capAtAvailable = -1;
    connect(&settings, &Settings::availableChanged, this, [&] { capAtAvailable = settings.maxVolume(); });
    const QString state = m_dir->path() + QStringLiteral("/later.json");
    QVERIFY(writeFile(state, QStringLiteral("{\"audio.output\": \"usb\", \"audio.max_volume\": 60}")));
    QVERIFY(m_set.start({state}));
    QTRY_VERIFY_WITH_TIMEOUT(settings.available(), waitMs);
    QCOMPARE(available.count(), 1);
    QCOMPARE(capAtAvailable, 60);
    QCOMPARE(cap.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(settings.output(), QStringLiteral("usb"), waitMs);
    QCOMPARE(output.count(), 1);
}

void SettingsTest::legacyMap()
{
    const QString hp = m_dir->path() + QStringLiteral("/config/eq/HD600 ParametricEQ.txt");
    QVERIFY(writeFile(hp, QString::fromUtf8(autoEq)));
    EqProfile custom = EqProfile::flat();
    custom.bands[1].gain = 2.5;
    QVERIFY(EqStore::write(m_dir->path() + QStringLiteral("/config/eq-usb.ini"), custom));
    const QString path = ini(QStringLiteral("[General]\noutput=usb\nhpGain=low\nhpAmp=ULP\nspkGain=96\nvolumeStep=10\n"
                                            "btCodec=LDAC\norientation=landscape\nautoRotate=true\nrgb=armada\n"
                                            "accent=#1BA1E2\neqHeadphones=") + hp
                             + QStringLiteral("\neqUsb=custom\neqSpeaker=off\ntouch=false\nscale=1.24\nsaverSeconds=60\n"
                                              "screenOffSeconds=120\nsuspendMinutes=30\nwifiOffScreenOff=true\n"
                                              "overlayToasts=true\nroots=/m1,/m2\naccentFromArt=true\n"
                                              "outputLevel=Bitperfect\nsampleRate=96000\n"));
    const QVariantMap map = Settings::legacyValues(path);
    QVERIFY(!map.contains(QStringLiteral("audio.hp_gain")));
    QCOMPARE(map.value(QStringLiteral("audio.output")).toString(), QStringLiteral("usb"));
    QCOMPARE(map.value(QStringLiteral("audio.hp_amp")).toString(), QStringLiteral("ulp"));
    QCOMPARE(map.value(QStringLiteral("audio.speaker_gain")).typeId(), QMetaType::UInt);
    QCOMPARE(map.value(QStringLiteral("audio.speaker_gain")).toUInt(), 96u);
    QCOMPARE(map.value(QStringLiteral("audio.volume_step")).toUInt(), 10u);
    QCOMPARE(map.value(QStringLiteral("audio.bt_codec_headphones")).toString(), QStringLiteral("ldac"));
    QVERIFY(!map.contains(QStringLiteral("audio.bt_codec")));
    QCOMPARE(map.value(QStringLiteral("display.orientation")).toString(), QStringLiteral("landscape"));
    QCOMPARE(map.value(QStringLiteral("display.auto_rotate")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("microdap.rgb")).toString(), QStringLiteral("system"));
    QCOMPARE(map.value(QStringLiteral("microdap.accent_hue")).toUInt(), 200u);
    QCOMPARE(map.value(QStringLiteral("microdap.touch")), QVariant(false));
    QCOMPARE(map.value(QStringLiteral("microdap.scale")).toDouble(), 1.2);
    QCOMPARE(map.value(QStringLiteral("microdap.saver_seconds")).toUInt(), 60u);
    QCOMPARE(map.value(QStringLiteral("microdap.screen_off_seconds")).toUInt(), 120u);
    QCOMPARE(map.value(QStringLiteral("microdap.suspend_minutes")).toUInt(), 30u);
    QCOMPARE(map.value(QStringLiteral("microdap.wifi_off_screen_off")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("microdap.overlay_toasts")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("microdap.accent_from_art")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("microdap.roots")).toStringList(), QStringList({QStringLiteral("/m1"), QStringLiteral("/m2")}));
    QCOMPARE(map.value(QStringLiteral("microdap.output_level")).toString(), QStringLiteral("bitperfect"));
    QCOMPARE(map.value(QStringLiteral("microdap.sample_rate")).toString(), QStringLiteral("96000"));
    QCOMPARE(map.value(QStringLiteral("audio.eq.headphones.on")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("audio.eq.headphones.name")).toString(), QStringLiteral("HD600"));
    QCOMPARE(map.value(QStringLiteral("audio.eq.headphones.filters")).toStringList(),
             EqFilters::encode(*EqParser::parse(QString::fromUtf8(autoEq))));
    QCOMPARE(map.value(QStringLiteral("audio.eq.usb.on")), QVariant(true));
    QCOMPARE(map.value(QStringLiteral("audio.eq.usb.name")).toString(), QString());
    QCOMPARE(map.value(QStringLiteral("audio.eq.usb.filters")).toStringList(), EqFilters::encode(custom));
    QCOMPARE(map.value(QStringLiteral("audio.eq.speaker.on")), QVariant(false));
    QVERIFY(!map.contains(QStringLiteral("audio.eq.speaker.filters")));
    QVERIFY(!map.contains(QStringLiteral("audio.eq.bt.on")));

    const QVariantMap steps = Settings::legacyValues(ini(QStringLiteral("[General]\nsaverSeconds=45\nscreenOffSeconds=200\n"
                                                                         "suspendMinutes=17\n")));
    QCOMPARE(steps.value(QStringLiteral("microdap.saver_seconds")).toUInt(), 30u);
    QCOMPARE(steps.value(QStringLiteral("microdap.screen_off_seconds")).toUInt(), 120u);
    QCOMPARE(steps.value(QStringLiteral("microdap.suspend_minutes")).toUInt(), 10u);
    const QVariantMap far = Settings::legacyValues(ini(QStringLiteral("[General]\nsaverSeconds=9999\nscreenOffSeconds=-5\n"
                                                                       "suspendMinutes=1000\n")));
    QCOMPARE(far.value(QStringLiteral("microdap.saver_seconds")).toUInt(), 300u);
    QCOMPARE(far.value(QStringLiteral("microdap.screen_off_seconds")).toUInt(), 0u);
    QCOMPARE(far.value(QStringLiteral("microdap.suspend_minutes")).toUInt(), 60u);
    QCOMPARE(Settings::snap(45, Settings::saverSteps()), 30);
    QCOMPARE(Settings::snap(46, Settings::saverSteps()), 60);
    Settings local(nullptr);
    local.setScreenOffSeconds(400);
    QCOMPARE(local.screenOffSeconds(), 300);
    local.setSuspendMinutes(25);
    QCOMPARE(local.suspendMinutes(), 30);
    local.setSaverSeconds(100);
    QCOMPARE(local.saverSeconds(), 120);

    const QVariantMap hue = Settings::legacyValues(ini(QStringLiteral("[General]\naccent=#1BA1E2\naccentHue=90\nrgb=nope\n")));
    QCOMPARE(hue.value(QStringLiteral("microdap.accent_hue")).toUInt(), 90u);
    QVERIFY(!hue.contains(QStringLiteral("microdap.rgb")));
    const QVariantMap missing = Settings::legacyValues(ini(QStringLiteral("[General]\neqHeadphones=/gone.txt\noutput=tape\n")));
    QVERIFY(missing.isEmpty());
    QVERIFY(Settings::legacyValues(m_dir->path() + QStringLiteral("/none.ini")).isEmpty());
}

void SettingsTest::importOnce()
{
    const QString path = ini(QStringLiteral("[General]\noutput=usb\nhpGain=low\nhpAmp=ulp\nvolumeStep=10\nrgb=accent\n"
                                            "saverSeconds=45\n"));
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QSignalSpy imported(&settings, &Settings::imported);
    QSignalSpy output(&settings, &Settings::outputChanged);
    settings.importLegacy(path);
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, waitMs);
    QVERIFY(imported.at(0).at(0).toBool());
    QVERIFY(!QFile::exists(path));
    QVERIFY(QFile::exists(path + QStringLiteral(".imported")));
    QCOMPARE(m_set.calls().filter(QStringLiteral("Import")), QStringList{QStringLiteral("Import 5")});
    QTRY_COMPARE_WITH_TIMEOUT(settings.output(), QStringLiteral("usb"), waitMs);
    QCOMPARE(output.count(), 1);
    QCOMPARE(settings.volumeStep(), 10);
    QCOMPARE(settings.rgb(), QStringLiteral("accent"));
    QCOMPARE(settings.hpAmp(), QStringLiteral("ulp"));
    QCOMPARE(settings.hpGain(), QStringLiteral("high"));
    QVERIFY(!stored().contains(QStringLiteral("audio.hp_gain")));

    settings.importLegacy(path);
    QTest::qWait(200);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Import")).size(), 1);
}

void SettingsTest::importDroppedReply()
{
    const QString path = ini(QStringLiteral("[General]\noutput=usb\nvolumeStep=10\n"));
    m_set.call(QStringLiteral("DropImport"), {true});
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QSignalSpy imported(&settings, &Settings::imported);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^microdap settings: import failed")));
    settings.importLegacy(path);
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, waitMs);
    QVERIFY(!imported.at(0).at(0).toBool());
    QVERIFY(QFile::exists(path));
    QVERIFY(!QFile::exists(path + QStringLiteral(".imported")));
    QVERIFY(m_set.waitExit());

    QVERIFY(m_set.start({m_dir->path() + QStringLiteral("/state.json")}));
    QCOMPARE(stored(), QStringList({QStringLiteral("audio.output"), QStringLiteral("audio.volume_step")}));
    QVERIFY(writeFile(path, QStringLiteral("[General]\noutput=speaker\nvolumeStep=2\nhpAmp=hifi\n")));
    settings.importLegacy(path);
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 2, waitMs);
    QVERIFY(imported.at(1).at(0).toBool());
    QVERIFY(!QFile::exists(path));
    QCOMPARE(client.get(QStringLiteral("audio.output")).toString(), QStringLiteral("usb"));
    QCOMPARE(client.get(QStringLiteral("audio.volume_step")).toUInt(), 10u);
    QCOMPARE(client.get(QStringLiteral("audio.hp_amp")).toString(), QStringLiteral("hifi"));
    QTRY_COMPARE_WITH_TIMEOUT(settings.hpAmp(), QStringLiteral("hifi"), waitMs);
}

void SettingsTest::importKeepsStored()
{
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.output"), FakeService::variant(QStringLiteral("speaker"))});
    const QString path = ini(QStringLiteral("[General]\noutput=usb\nbtCodec=aac\n"));
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QSignalSpy imported(&settings, &Settings::imported);
    settings.importLegacy(path);
    QTRY_COMPARE_WITH_TIMEOUT(imported.count(), 1, waitMs);
    QCOMPARE(settings.output(), QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(settings.btCodecHeadphones(), QStringLiteral("aac"), waitMs);
    QCOMPARE(settings.btCodec(), QStringLiteral("auto"));

    m_set.stop();
    const QString again = ini(QStringLiteral("[General]\noutput=usb\n"));
    MicroSet::Client gone(QDBusConnection::sessionBus());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^microdap settings: microsetd not reachable")));
    Settings offline(&gone);
    QSignalSpy failed(&offline, &Settings::imported);
    Settings none(nullptr);
    none.importLegacy(again);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^microdap settings: import failed")));
    offline.importLegacy(again);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, waitMs);
    QVERIFY(!failed.at(0).at(0).toBool());
    QVERIFY(QFile::exists(again));
}

void SettingsTest::bootSessions()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QCOMPARE(settings.bootSessions(), QStringList({QStringLiteral("steam"), QStringLiteral("microdap"), QStringLiteral("microemu")}));
    QCOMPARE(settings.bootSession(), QStringLiteral("steam"));
    QSignalSpy boot(&settings, &Settings::bootSessionChanged);
    settings.setBootSession(QStringLiteral("microdap"));
    QCOMPARE(boot.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("session.default")).toString(), QStringLiteral("microdap"), waitMs);
}

void SettingsTest::eqKeysThroughDaemon()
{
    const QString hp = m_dir->path() + QStringLiteral("/config/eq/HD600.txt");
    QVERIFY(writeFile(hp, QString::fromUtf8(autoEq)));
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    EqControl eq(&settings);
    eq.cycleProfile(QStringLiteral("usb"), 1);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.eq.usb.on")).toBool(), true, waitMs);
    QCOMPARE(client.get(QStringLiteral("audio.eq.usb.name")).toString(), QStringLiteral("HD600"));
    QCOMPARE(client.get(QStringLiteral("audio.eq.usb.filters")).toStringList(),
             EqFilters::encode(*EqParser::parse(QString::fromUtf8(autoEq))));
    m_set.call(QStringLiteral("Clear"));

    for (int i = 0; i < 6; ++i)
        eq.stepGain(QStringLiteral("usb"), 1, 1);
    QTest::qWait(EqControl::writeDelayMs / 2);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set audio.eq.usb.filters")).size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(m_set.calls().filter(QStringLiteral("Set audio.eq.usb.filters")).size(), 1, waitMs);
    QTest::qWait(EqControl::writeDelayMs + 200);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set audio.eq.usb.filters")).size(), 1);
    QCOMPARE(client.get(QStringLiteral("audio.eq.usb.filters")).toStringList().value(2), QStringLiteral("peak 1000 -2.6 1.41"));
    QCOMPARE(client.get(QStringLiteral("audio.eq.usb.name")).toString(), QString());
    QVERIFY(near(eq.profile(QStringLiteral("usb")).bands[1].gain, -2.6));

    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.eq.usb.filters"),
                                             FakeService::variant(QStringList{QStringLiteral("preamp -1"),
                                                                              QStringLiteral("highpass 80 0 0.7")})});
    QTRY_COMPARE_WITH_TIMEOUT(eq.profile(QStringLiteral("usb")).bands[0].type, EqBand::HighPass, waitMs);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.eq.usb.on"), FakeService::variant(false)});
    QTRY_COMPARE_WITH_TIMEOUT(eq.key(QStringLiteral("usb")), QStringLiteral("off"), waitMs);
}

void SettingsTest::rotationSubscribe()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Rotation rotation(&client);
    QVERIFY(rotation.supported());
    QCOMPARE(rotation.turn(), -1);
    QCOMPARE(rotation.raw(), QStringLiteral("portrait"));
    QCOMPARE(Rotation::turnOf(QStringLiteral("landscape"), 1), 1);
    QCOMPARE(Rotation::turnOf(QStringLiteral("landscape-flipped"), 1), 3);
    QCOMPARE(Rotation::turnOf(QStringLiteral("landscape"), 3), 3);
    QCOMPARE(Rotation::turnOf(QStringLiteral("portrait-flipped"), 1), 2);
    QCOMPARE(Rotation::turnOf(QStringLiteral("sideways"), 1), -1);

    QSignalSpy turn(&rotation, &Rotation::turnChanged);
    m_set.call(QStringLiteral("Clear"));
    rotation.setWanted(true, true);
    QTRY_COMPARE_WITH_TIMEOUT(m_set.value(QStringLiteral("Subscribers"), {Rotation::key()}).toUInt(), 1u, waitMs);
    const QStringList order = m_set.calls();
    QVERIFY(order.indexOf(QStringLiteral("Subscribe display.rotation")) >= 0);
    QVERIFY(order.indexOf(QStringLiteral("Get display.rotation")) > order.indexOf(QStringLiteral("Subscribe display.rotation")));
    QCOMPARE(rotation.turn(), 0);
    m_set.call(QStringLiteral("SetLive"), {Rotation::key(), FakeService::variant(QStringLiteral("landscape"))});
    QTRY_COMPARE_WITH_TIMEOUT(rotation.turn(), Rotation::defaultSensorTurn(), waitMs);
    QCOMPARE(rotation.settledRaw(), QStringLiteral("landscape"));
    rotation.setWanted(true, true);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Subscribe ")).size(), 1);

    rotation.setWanted(false, true);
    QTRY_COMPARE_WITH_TIMEOUT(m_set.value(QStringLiteral("Subscribers"), {Rotation::key()}).toUInt(), 0u, waitMs);
    QCOMPARE(rotation.turn(), -1);
    QCOMPARE(rotation.settledRaw(), QStringLiteral("landscape"));
    rotation.setWanted(false, false);
    QCOMPARE(rotation.settledRaw(), QString());
    m_set.call(QStringLiteral("SetLive"), {Rotation::key(), FakeService::variant(QStringLiteral("portrait-flipped"))});
    QTRY_COMPARE_WITH_TIMEOUT(rotation.raw(), QStringLiteral("portrait-flipped"), waitMs);
    QCOMPARE(rotation.turn(), -1);

    m_set.stop();
    MicroSet::Client gone(QDBusConnection::sessionBus());
    Rotation offline(&gone);
    QVERIFY(!offline.supported());
    Rotation none(nullptr);
    none.setWanted(true);
    QCOMPARE(none.turn(), -1);
}

void SettingsTest::rowTable()
{
    const auto strings = [](const QVariantList &values) {
        QStringList out;
        for (const QVariant &v : values)
            out.push_back(v.toString());
        return out;
    };
    const auto ints = [](const QVariantList &values) {
        QList<int> out;
        for (const QVariant &v : values)
            out.push_back(v.toInt());
        return out;
    };
    QSet<QString> keys;
    for (const SettingsRows::Row &row : SettingsRows::all()) {
        QVERIFY2(!keys.contains(row.key), qPrintable(row.key));
        keys.insert(row.key);
        QVERIFY2(SettingsRows::linkPivots().contains(row.pivot), qPrintable(row.key));
        QVERIFY2(SettingsRows::types().contains(row.type), qPrintable(row.key));
        QVERIFY2(SettingsRows::kinds().contains(row.kind), qPrintable(row.key));
        QVERIFY2(!row.title.isEmpty(), qPrintable(row.key));
        QVERIFY2(!row.draft || !row.reason.isEmpty(), qPrintable(row.key));
        if (row.type == QLatin1StringView("choice") || row.type == QLatin1StringView("steps")
            || row.type == QLatin1StringView("range") || row.type == QLatin1StringView("toggle")) {
            const QByteArray name = row.setting.toLatin1();
            QVERIFY2(!name.isEmpty() && (Settings::staticMetaObject.indexOfProperty(name.constData()) >= 0
                                         || EqControl::staticMetaObject.indexOfProperty(name.constData()) >= 0
                                         || (row.key == QLatin1StringView("sleep")
                                             && PowerBridge::staticMetaObject.indexOfProperty(name.constData()) >= 0)),
                     qPrintable(row.key));
        }
        if (row.type == QLatin1StringView("toggle"))
            QCOMPARE(row.kind, QStringLiteral("flip"));
        if (row.kind == QLatin1StringView("action")) {
            QCOMPARE(row.type, QStringLiteral("action"));
            QVERIFY2(!row.action.isEmpty(), qPrintable(row.key));
            QVERIFY2(row.setting.isEmpty(), qPrintable(row.key));
        }
    }
    for (const char *key : {"rescan", "pair", "about", "closePlayer", "folders", "eq"}) {
        const SettingsRows::Row *row = SettingsRows::find(QString::fromLatin1(key));
        QVERIFY2(row, key);
        QCOMPARE(row->kind, QStringLiteral("action"));
        QVERIFY(!row->action.isEmpty());
        QVERIFY(row->setting.isEmpty());
    }
    const SettingsRows::Row *phoneApp = SettingsRows::find(QStringLiteral("phoneApp"));
    QVERIFY(phoneApp && phoneApp->draft && !phoneApp->bounded);
    QCOMPARE(phoneApp->kind, QStringLiteral("sheet"));
    QCOMPARE(phoneApp->pivot, QStringLiteral("system"));
    QCOMPARE(phoneApp->setting, QStringLiteral("companion"));
    QCOMPARE(phoneApp->values, QVariantList({false, true}));
    const SettingsRows::Row *pairApp = SettingsRows::find(QStringLiteral("pairApp"));
    QVERIFY(pairApp && pairApp->kind == QLatin1StringView("action"));
    QCOMPARE(pairApp->shown, QStringLiteral("companion"));
    for (const char *key : {"hpGain", "hpAmp", "spkGain"}) {
        const SettingsRows::Row *row = SettingsRows::find(QString::fromLatin1(key));
        QVERIFY2(row && row->bounded && row->draft, key);
    }
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("hpGain"))->values), Settings::hpGains());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("hpAmp"))->values), Settings::hpAmps());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("spkGain"))->values), Settings::spkGains());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("volumeStep"))->values), Settings::volumeSteps());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("codec"))->values), Settings::codecs());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("level"))->values), Settings::outputLevels());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("orientation"))->values), Settings::orientations());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("rgb"))->values), Settings::rgbModes());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("playThrough"))->values), Settings::playThroughModes());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("resume"))->values), Settings::resumeModes());
    QCOMPARE(strings(SettingsRows::find(QStringLiteral("clock"))->values), Settings::clockModes());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("saver"))->values), Settings::saverSteps());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("screenOff"))->values), Settings::screenOffSteps());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("suspend"))->values), Settings::suspendSteps());
    for (const QString &v : strings(SettingsRows::find(QStringLiteral("output"))->values))
        QVERIFY2(Settings::outputs().contains(v), qPrintable(v));
    for (const QString &v : strings(SettingsRows::find(QStringLiteral("rate"))->values))
        QVERIFY2(Settings::sampleRates().contains(v), qPrintable(v));
    const SettingsRows::Row *scale = SettingsRows::find(QStringLiteral("scale"));
    QVERIFY(near(scale->min, Settings::minScale));
    QVERIFY(near(scale->max, Settings::maxScale));
    const SettingsRows::Row *saverLyrics = SettingsRows::find(QStringLiteral("saverLyrics"));
    QVERIFY(saverLyrics);
    QCOMPARE(saverLyrics->pivot, QStringLiteral("display"));
    QCOMPARE(saverLyrics->kind, QStringLiteral("flip"));
    QCOMPARE(saverLyrics->setting, QStringLiteral("saverLyrics"));
    for (const char *key : {"maxVolume", "powerOnVolume"}) {
        const SettingsRows::Row *row = SettingsRows::find(QString::fromLatin1(key));
        QVERIFY2(row && row->bounded && row->draft && row->kind == QLatin1StringView("sheet"), key);
    }
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("maxVolume"))->values), Settings::maxVolumeSteps());
    QCOMPARE(ints(SettingsRows::find(QStringLiteral("powerOnVolume"))->values), Settings::powerOnVolumeSteps());
    const SettingsRows::Row *balance = SettingsRows::find(QStringLiteral("balance"));
    QVERIFY(balance && balance->bounded && !balance->draft);
    QCOMPARE(balance->kind, QStringLiteral("edit"));
    QCOMPARE(int(balance->min), -Settings::maxBalance);
    QCOMPARE(int(balance->max), Settings::maxBalance);
    const SettingsRows::Row *crossfeed = SettingsRows::find(QStringLiteral("crossfeed"));
    QVERIFY(crossfeed && crossfeed->draft && !crossfeed->bounded);
    QCOMPARE(crossfeed->kind, QStringLiteral("sheet"));
    QCOMPARE(strings(crossfeed->values), Settings::crossfeedModes());
    const SettingsRows::Row *replaygain = SettingsRows::find(QStringLiteral("replaygain"));
    QVERIFY(replaygain && replaygain->draft && !replaygain->bounded);
    QCOMPARE(replaygain->kind, QStringLiteral("sheet"));
    QCOMPARE(replaygain->setting, QStringLiteral("replaygain"));
    QCOMPARE(replaygain->readOnly, QStringLiteral("bitPerfect"));
    QCOMPARE(strings(replaygain->values), Settings::replaygainModes());
    const SettingsRows::Row *crossfade = SettingsRows::find(QStringLiteral("crossfade"));
    QVERIFY(crossfade && crossfade->draft && !crossfade->bounded);
    QCOMPARE(crossfade->kind, QStringLiteral("sheet"));
    QCOMPARE(crossfade->setting, QStringLiteral("crossfadeMs"));
    QCOMPARE(crossfade->readOnly, QStringLiteral("bitPerfect"));
    QCOMPARE(ints(crossfade->values), Settings::crossfadeSteps());
    const SettingsRows::Row *sleep = SettingsRows::find(QStringLiteral("sleep"));
    QVERIFY(sleep && sleep->draft && !sleep->bounded);
    QCOMPARE(sleep->kind, QStringLiteral("sheet"));
    QCOMPARE(sleep->pivot, QStringLiteral("power"));
    QCOMPARE(sleep->setting, QStringLiteral("sleepMinutesLeft"));
    QCOMPARE(sleep->linkKey, QStringLiteral("sleep"));
    QCOMPARE(sleep->shown, QStringLiteral("dap"));
    QCOMPARE(ints(sleep->values), QList<int>({0, 15, 30, 45, 60, 90, 120}));
    const SettingsRows::Row *codec = SettingsRows::find(QStringLiteral("codec"));
    QVERIFY(codec && codec->draft && !codec->bounded);
    QCOMPARE(codec->kind, QStringLiteral("sheet"));
    QCOMPARE(codec->pivot, QStringLiteral("bluetooth"));
    QCOMPARE(codec->title, QStringLiteral("codec"));
    QCOMPARE(SettingsRows::pivots(), QStringList({QStringLiteral("output"), QStringLiteral("library"),
                                                  QStringLiteral("display"), QStringLiteral("power"),
                                                  QStringLiteral("system")}));
    QCOMPARE(SettingsRows::linkPivots(), SettingsRows::pivots() << QStringLiteral("bluetooth"));
    QCOMPARE(codec->setting, QStringLiteral("btCodec"));
    QVERIFY(codec->readOnly.isEmpty());
    QCOMPARE(strings(codec->values), Settings::codecs());
    QCOMPARE(Settings::codecs(), QStringList({QStringLiteral("auto"), QStringLiteral("aptx"), QStringLiteral("aac"),
                                              QStringLiteral("sbc")}));
    QCOMPARE(codec->headphonesSetting, QStringLiteral("btCodecHeadphones"));
    QCOMPARE(strings(codec->headphonesValues), Settings::headphoneCodecs());
    QCOMPARE(SettingsRows::maps().size(), SettingsRows::all().size());
}

void SettingsTest::btCodecKeys()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QSignalSpy headphones(&settings, &Settings::btCodecHeadphonesChanged);
    settings.setBtCodecHeadphones(QStringLiteral("aptx_hd"));
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.bt_codec_headphones")).toString(), QStringLiteral("aptx_hd"),
                              waitMs);
    QCOMPARE(client.get(QStringLiteral("audio.bt_codec")).toString(), QStringLiteral("auto"));
    QCOMPARE(headphones.count(), 1);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.bt_codec_headphones"), FakeService::variant(QStringLiteral("ldac"))});
    QTRY_COMPARE_WITH_TIMEOUT(settings.btCodecHeadphones(), QStringLiteral("ldac"), waitMs);
    QCOMPARE(settings.btCodec(), QStringLiteral("auto"));
    settings.setBtCodec(QStringLiteral("aac"));
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.bt_codec")).toString(), QStringLiteral("aac"), waitMs);
    settings.setBtCodec(QStringLiteral("ldac"));
    QCOMPARE(settings.btCodec(), QStringLiteral("aac"));
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.bt_codec"), FakeService::variant(QStringLiteral("ldac"))});
    QTRY_COMPARE_WITH_TIMEOUT(settings.btCodec(), QStringLiteral("auto"), waitMs);
}

void SettingsTest::volumeKeys()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QCOMPARE(settings.maxVolume(), 100);
    QCOMPARE(settings.powerOnVolume(), 0);
    QSignalSpy cap(&settings, &Settings::maxVolumeChanged);
    settings.setMaxVolume(70);
    settings.setMaxVolume(52);
    settings.setPowerOnVolume(40);
    settings.setPowerOnVolume(5);
    QCOMPARE(settings.maxVolume(), 70);
    QCOMPARE(settings.powerOnVolume(), 40);
    QCOMPARE(cap.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.max_volume")).toUInt(), 70u, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.power_on_volume")).toUInt(), 40u, waitMs);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.max_volume"), FakeService::variant(55u)});
    QTRY_COMPARE_WITH_TIMEOUT(settings.maxVolume(), 55, waitMs);
    QCOMPARE(cap.count(), 2);
}

void SettingsTest::balanceCrossfeedSlot()
{
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QCOMPARE(settings.outputSlot(), QString());
    settings.setBalance(-4);
    settings.setCrossfeed(QStringLiteral("default"));
    QCOMPARE(settings.balance(), 0);
    QCOMPARE(settings.crossfeed(), QStringLiteral("off"));

    QSignalSpy balance(&settings, &Settings::balanceChanged);
    QSignalSpy crossfeed(&settings, &Settings::crossfeedChanged);
    settings.setOutputSlot(QStringLiteral("headphones"));
    QCOMPARE(balance.count(), 1);
    QCOMPARE(crossfeed.count(), 1);
    settings.setBalance(-7);
    settings.setCrossfeed(QStringLiteral("janMeier"));
    QCOMPARE(settings.balance(), -7);
    QCOMPARE(settings.crossfeed(), QStringLiteral("janMeier"));
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.balance.headphones")).toInt(), -7, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.crossfeed.headphones")).toString(),
                              QStringLiteral("janMeier"), waitMs);

    settings.setOutputSlot(QStringLiteral("speaker"));
    QCOMPARE(settings.balance(), 0);
    QCOMPARE(settings.crossfeed(), QStringLiteral("off"));
    const int sets = m_set.calls().filter(QStringLiteral("Set ")).size();
    settings.setCrossfeed(QStringLiteral("default"));
    settings.setBalance(25);
    QCOMPARE(settings.balance(), Settings::maxBalance);
    QTRY_COMPARE_WITH_TIMEOUT(client.get(QStringLiteral("audio.balance.speaker")).toInt(), Settings::maxBalance, waitMs);
    QTest::qWait(100);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set ")).size(), sets + 1);

    settings.setOutputSlot(QStringLiteral("headphones"));
    QCOMPARE(settings.balance(), -7);
    QCOMPARE(settings.crossfeed(), QStringLiteral("janMeier"));

    balance.clear();
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.balance.headphones"), FakeService::variant(4)});
    QTRY_COMPARE_WITH_TIMEOUT(settings.balance(), 4, waitMs);
    QCOMPARE(balance.count(), 1);
    m_set.call(QStringLiteral("SetStored"), {QStringLiteral("audio.balance.usb"), FakeService::variant(-2)});
    QTRY_COMPARE_WITH_TIMEOUT(settings.value(QStringLiteral("audio.balance.usb")).toInt(), -2, waitMs);
    QCOMPARE(settings.balance(), 4);
    QCOMPARE(balance.count(), 1);

    settings.setOutputSlot(QStringLiteral("hdmi"));
    QCOMPARE(settings.outputSlot(), QString());
    QCOMPARE(settings.balance(), 0);
}

void SettingsTest::brightnessRow()
{
    const QString key = QStringLiteral("display.brightness");
    MicroSet::Client client(QDBusConnection::sessionBus());
    Settings settings(&client);
    QCOMPARE(settings.brightness(), 2442);
    QVariantList steps;
    for (int bp = 1000; bp <= 10000; bp += 500)
        steps.append(bp);
    QCOMPARE(settings.brightnessSteps(), steps);
    QCOMPARE(settings.brightnessStep(2442, 1), 2500);
    QCOMPARE(settings.brightnessStep(2442, -1), 2000);
    QCOMPARE(settings.brightnessStep(300, 1), 1000);
    QCOMPARE(settings.brightnessStep(300, -1), 300);
    QCOMPARE(settings.brightnessStep(5000, 1), 5500);
    QCOMPARE(settings.brightnessStep(5000, -1), 4500);
    QCOMPARE(settings.brightnessStep(1000, -1), 1000);
    QCOMPARE(settings.brightnessStep(10000, 1), 10000);
    const SettingsRows::Row *row = SettingsRows::find(QStringLiteral("brightness"));
    QVERIFY(row);
    QVERIFY(row->bounded);
    QCOMPARE(row->type, QStringLiteral("steps"));
    QCOMPARE(row->readOnly, QStringLiteral("brightness"));

    settings.previewBrightness(5000);
    QTRY_COMPARE_WITH_TIMEOUT(settings.brightness(), 5000, waitMs);
    QVERIFY(!client.stored().contains(key));
    settings.cancelBrightness();
    QTRY_COMPARE_WITH_TIMEOUT(settings.brightness(), 2442, waitMs);
    QVERIFY(!client.stored().contains(key));
    QCOMPARE(m_set.calls().filter(key), QStringList({QStringLiteral("Override ") + key,
                                                     QStringLiteral("ClearOverride ") + key}));

    settings.previewBrightness(6000);
    QTRY_COMPARE_WITH_TIMEOUT(settings.brightness(), 6000, waitMs);
    settings.confirmBrightness(6000);
    QTRY_COMPARE_WITH_TIMEOUT(client.stored().value(key).toUInt(), 6000u, waitMs);
    QCOMPARE(m_set.calls().filter(key).mid(2), QStringList({QStringLiteral("Override ") + key,
                                                            QStringLiteral("Set ") + key,
                                                            QStringLiteral("ClearOverride ") + key}));
    QCOMPARE(settings.brightness(), 6000);

    m_set.call(QStringLiteral("SetStored"), {key, FakeService::variant(2442u)});
    QTRY_COMPARE_WITH_TIMEOUT(settings.brightness(), 2442, waitMs);
    LinkSettings link(&settings, QStringLiteral("test"));
    QVERIFY(link.text().contains(QStringLiteral("brightness=24\n")));
    QStringList kept;
    QStringList refused;
    QCOMPARE(link.set(QStringLiteral("brightness=56\n"), kept, refused), uint8_t(0));
    QCOMPARE(kept, QStringList{QStringLiteral("brightness")});
    QTRY_COMPARE_WITH_TIMEOUT(client.stored().value(key).toUInt(), 5500u, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(settings.brightness(), 5500, waitMs);
}

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
