#include "FakeService.h"
#include "RgbControl.h"

#include <microset/client.h>
#include <microset/color.h>

#include <QDBusArgument>
#include <QSignalSpy>
#include <QTest>

#include <array>

using namespace microdap;

namespace {

constexpr int waitMs = 5000;

constexpr std::array<std::array<uint, RgbControl::levels + 1>, 36> accentColors{{
    {0xFF0000, 0xFF9999, 0xFF6666, 0xFF3333, 0xFF0000},
    {0xFF2B00, 0xFFAA99, 0xFF8066, 0xFF5533, 0xFF2B00},
    {0xFF5500, 0xFFBB99, 0xFF9966, 0xFF7733, 0xFF5500},
    {0xFF8000, 0xFFCC99, 0xFFB366, 0xFF9933, 0xFF8000},
    {0xFFAA00, 0xFFDD99, 0xFFCC66, 0xFFBB33, 0xFFAA00},
    {0xFFD500, 0xFFEE99, 0xFFE666, 0xFFDD33, 0xFFD500},
    {0xFFFF00, 0xFFFF99, 0xFFFF66, 0xFFFF33, 0xFFFF00},
    {0xD5FF00, 0xEEFF99, 0xE6FF66, 0xDDFF33, 0xD5FF00},
    {0xAAFF00, 0xDDFF99, 0xCCFF66, 0xBBFF33, 0xAAFF00},
    {0x80FF00, 0xCCFF99, 0xB3FF66, 0x99FF33, 0x80FF00},
    {0x55FF00, 0xBBFF99, 0x99FF66, 0x77FF33, 0x55FF00},
    {0x2BFF00, 0xAAFF99, 0x80FF66, 0x55FF33, 0x2BFF00},
    {0x00FF00, 0x99FF99, 0x66FF66, 0x33FF33, 0x00FF00},
    {0x00FF2B, 0x99FFAA, 0x66FF80, 0x33FF55, 0x00FF2B},
    {0x00FF55, 0x99FFBB, 0x66FF99, 0x33FF77, 0x00FF55},
    {0x00FF80, 0x99FFCC, 0x66FFB3, 0x33FF99, 0x00FF80},
    {0x00FFAA, 0x99FFDD, 0x66FFCC, 0x33FFBB, 0x00FFAA},
    {0x00FFD5, 0x99FFEE, 0x66FFE6, 0x33FFDD, 0x00FFD5},
    {0x00FFFF, 0x99FFFF, 0x66FFFF, 0x33FFFF, 0x00FFFF},
    {0x00D5FF, 0x99EEFF, 0x66E6FF, 0x33DDFF, 0x00D5FF},
    {0x00AAFF, 0x99DDFF, 0x66CCFF, 0x33BBFF, 0x00AAFF},
    {0x0080FF, 0x99CCFF, 0x66B3FF, 0x3399FF, 0x0080FF},
    {0x0055FF, 0x99BBFF, 0x6699FF, 0x3377FF, 0x0055FF},
    {0x002BFF, 0x99AAFF, 0x6680FF, 0x3355FF, 0x002BFF},
    {0x0000FF, 0x9999FF, 0x6666FF, 0x3333FF, 0x0000FF},
    {0x2B00FF, 0xAA99FF, 0x8066FF, 0x5533FF, 0x2B00FF},
    {0x5500FF, 0xBB99FF, 0x9966FF, 0x7733FF, 0x5500FF},
    {0x8000FF, 0xCC99FF, 0xB366FF, 0x9933FF, 0x8000FF},
    {0xAA00FF, 0xDD99FF, 0xCC66FF, 0xBB33FF, 0xAA00FF},
    {0xD500FF, 0xEE99FF, 0xE666FF, 0xDD33FF, 0xD500FF},
    {0xFF00FF, 0xFF99FF, 0xFF66FF, 0xFF33FF, 0xFF00FF},
    {0xFF00D5, 0xFF99EE, 0xFF66E6, 0xFF33DD, 0xFF00D5},
    {0xFF00AA, 0xFF99DD, 0xFF66CC, 0xFF33BB, 0xFF00AA},
    {0xFF0080, 0xFF99CC, 0xFF66B3, 0xFF3399, 0xFF0080},
    {0xFF0055, 0xFF99BB, 0xFF6699, 0xFF3377, 0xFF0055},
    {0xFF002B, 0xFF99AA, 0xFF6680, 0xFF3355, 0xFF002B},
}};

}

class RgbTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void colors();
    void systemHoldsNothing();
    void accentOverridesHueAndSaturation();
    void accentLevels();
    void hueDebounced();
    void offOverridesEnabled();
    void screenOffAndSaver();
    void storedChangeUnderOverride();
    void reoverrideOnNewOwner();
    void overridesGoneWithClient();
    void unsupportedSendsNothing();

private:
    QVariantMap overrides() const;
    int countOf(const QString &call) const;
    void started(RgbControl &control, const QString &mode, int hue = 200);

    FakeService m_set = FakeService::microSet(QStringLiteral(MICRODAP_FAKE_MICROSETD));
    std::unique_ptr<MicroSet::Client> m_client;
};

void RgbTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus, run under dbus-run-session");
}

void RgbTest::init()
{
    QVERIFY(m_set.start());
    m_client = std::make_unique<MicroSet::Client>(QDBusConnection::sessionBus());
}

void RgbTest::cleanup()
{
    m_client.reset();
    m_set.stop();
}

QVariantMap RgbTest::overrides() const
{
    return qdbus_cast<QVariantMap>(m_set.value(QStringLiteral("Overrides")));
}

int RgbTest::countOf(const QString &call) const
{
    return int(m_set.calls().count(call));
}

void RgbTest::started(RgbControl &control, const QString &mode, int hue)
{
    control.start(mode, hue);
    QVERIFY(control.probed());
    QVERIFY(control.supported());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
}

void RgbTest::colors()
{
    const std::array<int, RgbControl::levels + 1> saturation{100, 40, 60, 80, 100};
    for (int level = 0; level <= RgbControl::levels; ++level)
        QCOMPARE(RgbControl::saturationFor(level), saturation[level]);
    QCOMPARE(RgbControl::saturationFor(9), 100);
    QCOMPARE(RgbControl::saturationFor(-1), 100);
    for (int i = 0; i < 36; ++i)
        for (int level = 0; level <= RgbControl::levels; ++level)
            QCOMPARE(MicroSet::hueColor(i * 10, saturation[level]), accentColors[i][level]);
}

void RgbTest::systemHoldsNothing()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("system"));
    QTest::qWait(200);
    QVERIFY(overrides().isEmpty());
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 0);
    QVERIFY(!control.sent(RgbControl::enabledKey()));
}

void RgbTest::accentOverridesHueAndSaturation()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 200);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")).toBool(), true, waitMs);
    QCOMPARE(overrides().value(QStringLiteral("rgb.hue")).toUInt(), 200u);
    QCOMPARE(overrides().value(QStringLiteral("rgb.saturation")).toUInt(), 100u);
    QCOMPARE(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 200u);
    QVERIFY(!overrides().contains(QStringLiteral("rgb.brightness")));
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 1);
    QCOMPARE(countOf(QStringLiteral("Override rgb.hue")), 1);
    QCOMPARE(countOf(QStringLiteral("Override rgb.saturation")), 1);

    control.setMode(QStringLiteral("system"));
    QTRY_VERIFY_WITH_TIMEOUT(overrides().isEmpty(), waitMs);
    QCOMPARE(countOf(QStringLiteral("ClearOverride rgb.enabled")), 1);
    QCOMPARE(countOf(QStringLiteral("ClearOverride rgb.hue")), 1);
    QCOMPARE(countOf(QStringLiteral("ClearOverride rgb.saturation")), 1);
    QCOMPARE(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 0u);
    QCOMPARE(countOf(QStringLiteral("Override rgb.brightness")), 0);
}

void RgbTest::accentLevels()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 200);
    for (int level : {1, 2, 3, 4, 0}) {
        control.setColor(200, level);
        QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.saturation")).toUInt(),
                                  uint(RgbControl::saturationFor(level)), waitMs);
        QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
        QCOMPARE(overrides().value(QStringLiteral("rgb.hue")).toUInt(), 200u);
    }
    QCOMPARE(countOf(QStringLiteral("Override rgb.hue")), 1);
    QCOMPARE(countOf(QStringLiteral("Override rgb.saturation")), 5);
    QVERIFY(!overrides().contains(QStringLiteral("rgb.brightness")));
    QCOMPARE(countOf(QStringLiteral("Override rgb.brightness")), 0);
}

void RgbTest::hueDebounced()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 200);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(QStringLiteral("Override rgb.hue")), 1, waitMs);
    control.setHue(210);
    control.setHue(220);
    control.setColor(230, 3);
    QTest::qWait(RgbControl::hueDelayMs / 2);
    QCOMPARE(countOf(QStringLiteral("Override rgb.hue")), 1);
    QCOMPARE(countOf(QStringLiteral("Override rgb.saturation")), 1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(QStringLiteral("Override rgb.hue")), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.hue")).toUInt(), 230u, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.saturation")).toUInt(), 80u, waitMs);
    control.setColor(230, 3);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(countOf(QStringLiteral("Override rgb.hue")), 2);
    QCOMPARE(countOf(QStringLiteral("Override rgb.saturation")), 2);
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 1);
}

void RgbTest::offOverridesEnabled()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("off"));
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(false), waitMs);
    QVERIFY(!overrides().contains(QStringLiteral("rgb.hue")));
    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true), waitMs);
    QVERIFY(overrides().contains(QStringLiteral("rgb.hue")));
}

void RgbTest::screenOffAndSaver()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("system"));
    control.setSaver(true);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(false), waitMs);
    control.setScreenOff(true);
    QTest::qWait(100);
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 1);
    control.setSaver(false);
    QTest::qWait(100);
    QCOMPARE(overrides().value(QStringLiteral("rgb.enabled")), QVariant(false));
    control.setScreenOff(false);
    QTRY_VERIFY_WITH_TIMEOUT(overrides().isEmpty(), waitMs);

    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true), waitMs);
    control.setScreenOff(true);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(false), waitMs);
    QVERIFY(overrides().contains(QStringLiteral("rgb.hue")));
    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true), waitMs);
}

void RgbTest::storedChangeUnderOverride()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 120);
    QTRY_COMPARE_WITH_TIMEOUT(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 120u, waitMs);
    MicroSet::Error error;
    bool done = false;
    m_client->set(QStringLiteral("rgb.hue"), QVariant::fromValue(300u), [&](const MicroSet::Error &e) {
        error = e;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, waitMs);
    QVERIFY(error.ok());
    QCOMPARE(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 120u);
    QCOMPARE(m_client->stored(QStringLiteral("rgb.")).value(QStringLiteral("rgb.hue")).toUInt(), 300u);
    control.setMode(QStringLiteral("system"));
    QTRY_COMPARE_WITH_TIMEOUT(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 300u, waitMs);
}

void RgbTest::reoverrideOnNewOwner()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 40);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 3, waitMs);
    m_set.stop();
    QVERIFY(m_set.start());
    QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 3, waitMs);
    QCOMPARE(overrides().value(QStringLiteral("rgb.hue")).toUInt(), 40u);
    QCOMPARE(overrides().value(QStringLiteral("rgb.saturation")).toUInt(), 100u);
    QCOMPARE(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true));
}

void RgbTest::overridesGoneWithClient()
{
    {
        QDBusConnection own = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("rgb-own"));
        MicroSet::Client client(own);
        RgbControl control(&client);
        started(control, QStringLiteral("accent"), 300);
        QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 3, waitMs);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("rgb-own"));
    QTRY_VERIFY_WITH_TIMEOUT(overrides().isEmpty(), waitMs);
    QCOMPARE(m_client->get(QStringLiteral("rgb.hue")).toUInt(), 0u);
}

void RgbTest::unsupportedSendsNothing()
{
    RgbControl none(nullptr);
    none.start(QStringLiteral("accent"), 10);
    QVERIFY(none.probed());
    QVERIFY(!none.supported());
    none.setMode(QStringLiteral("off"));
    QVERIFY(!none.busy());

    m_set.stop();
    MicroSet::Client client(QDBusConnection::sessionBus());
    RgbControl control(&client);
    control.start(QStringLiteral("accent"), 10);
    QVERIFY(control.probed());
    QVERIFY(!control.supported());
    QVERIFY(!control.sent(RgbControl::enabledKey()));
}

QTEST_GUILESS_MAIN(RgbTest)
#include "tst_rgb.moc"
