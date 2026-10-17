#include "Eq.h"
#include "FakeService.h"
#include "Rotation.h"
#include "Settings.h"

#include <microset/client.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    QCOMPARE(settings.screenOffSeconds(), 300);
    QCOMPARE(settings.suspendMinutes(), 10);
    QVERIFY(settings.touch());
    QCOMPARE(settings.outputLevel(), QStringLiteral("off"));
    QCOMPARE(settings.sampleRate(), QStringLiteral("auto"));
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
    settings.setHpGain(QStringLiteral("low"));
    settings.setBootSession(QStringLiteral("microemu"));
    QTRY_COMPARE_WITH_TIMEOUT(m_set.calls().filter(QStringLiteral("Set ")).size(), 11, waitMs);
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
    QCOMPARE(client.get(QStringLiteral("audio.hp_gain")).toString(), QStringLiteral("low"));
    QCOMPARE(client.get(QStringLiteral("session.default")).toString(), QStringLiteral("microemu"));
    QCOMPARE(settings.output(), QStringLiteral("headphones"));
    settings.setOutput(QStringLiteral("headphones"));
    settings.setBootSession(QStringLiteral("nowhere"));
    QTest::qWait(200);
    QCOMPARE(m_set.calls().filter(QStringLiteral("Set ")).size(), 11);
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
    const QString state = m_dir->path() + QStringLiteral("/later.json");
    QVERIFY(writeFile(state, QStringLiteral("{\"audio.output\": \"usb\"}")));
    QVERIFY(m_set.start({state}));
    QTRY_VERIFY_WITH_TIMEOUT(settings.available(), waitMs);
    QCOMPARE(available.count(), 1);
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
    QCOMPARE(map.value(QStringLiteral("audio.bt_codec")).toString(), QStringLiteral("ldac"));
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
    QTRY_COMPARE_WITH_TIMEOUT(settings.btCodec(), QStringLiteral("aac"), waitMs);

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

QTEST_GUILESS_MAIN(SettingsTest)
#include "tst_settings.moc"
