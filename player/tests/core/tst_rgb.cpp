#include "FakeService.h"
#include "RgbControl.h"

#include <microset/client.h>

#include <QDBusArgument>
#include <QSignalSpy>
#include <QTest>

using namespace microdap;

namespace {

constexpr int waitMs = 5000;

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
    void accentOverridesColor();
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
    QCOMPARE(RgbControl::colorFor(0, 0), 0xFF0000u);
    QCOMPARE(RgbControl::colorFor(120, 4), 0x00FF00u);
    QCOMPARE(RgbControl::colorFor(240, 0), 0x0000FFu);
    QCOMPARE(RgbControl::colorFor(60, 0), 0xFFFF00u);
    QCOMPARE(RgbControl::colorFor(350, 0), 0xFF002Bu);
    QCOMPARE(RgbControl::colorFor(0, 1), 0xFF9999u);
    QCOMPARE(RgbControl::colorFor(200, 2), 0x66CCFFu);
    QCOMPARE(RgbControl::colorFor(355, 0), RgbControl::colorFor(0, 0));
    QCOMPARE(RgbControl::colorFor(200, 9), RgbControl::colorFor(200, 0));
    QVERIFY(RgbControl::colorFor(200, 1) != RgbControl::colorFor(200, 4));
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

void RgbTest::accentOverridesColor()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 200);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")).toBool(), true, waitMs);
    QCOMPARE(overrides().value(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(200, 0));
    QCOMPARE(m_client->get(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(200, 0));
    QVERIFY(!overrides().contains(QStringLiteral("rgb.brightness")));
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 1);
    QCOMPARE(countOf(QStringLiteral("Override rgb.color")), 1);

    control.setMode(QStringLiteral("system"));
    QTRY_VERIFY_WITH_TIMEOUT(overrides().isEmpty(), waitMs);
    QCOMPARE(countOf(QStringLiteral("ClearOverride rgb.enabled")), 1);
    QCOMPARE(countOf(QStringLiteral("ClearOverride rgb.color")), 1);
    QCOMPARE(m_client->get(QStringLiteral("rgb.color")).toUInt(), 16777215u);
}

void RgbTest::hueDebounced()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 200);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(QStringLiteral("Override rgb.color")), 1, waitMs);
    control.setHue(210);
    control.setHue(220);
    control.setColor(230, 3);
    QTest::qWait(RgbControl::hueDelayMs / 2);
    QCOMPARE(countOf(QStringLiteral("Override rgb.color")), 1);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(QStringLiteral("Override rgb.color")), 2, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(230, 3), waitMs);
    control.setColor(230, 3);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(countOf(QStringLiteral("Override rgb.color")), 2);
    QCOMPARE(countOf(QStringLiteral("Override rgb.enabled")), 1);
}

void RgbTest::offOverridesEnabled()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("off"));
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(false), waitMs);
    QVERIFY(!overrides().contains(QStringLiteral("rgb.color")));
    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true), waitMs);
    QVERIFY(overrides().contains(QStringLiteral("rgb.color")));
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
    QVERIFY(overrides().contains(QStringLiteral("rgb.color")));
    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true), waitMs);
}

void RgbTest::storedChangeUnderOverride()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 120);
    QTRY_COMPARE_WITH_TIMEOUT(m_client->get(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(120, 0), waitMs);
    MicroSet::Error error;
    bool done = false;
    m_client->set(QStringLiteral("rgb.color"), QVariant::fromValue(255u), [&](const MicroSet::Error &e) {
        error = e;
        done = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(done, waitMs);
    QVERIFY(error.ok());
    QCOMPARE(m_client->get(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(120, 0));
    QCOMPARE(m_client->stored(QStringLiteral("rgb.")).value(QStringLiteral("rgb.color")).toUInt(), 255u);
    control.setMode(QStringLiteral("system"));
    QTRY_COMPARE_WITH_TIMEOUT(m_client->get(QStringLiteral("rgb.color")).toUInt(), 255u, waitMs);
}

void RgbTest::reoverrideOnNewOwner()
{
    RgbControl control(m_client.get());
    started(control, QStringLiteral("accent"), 40);
    QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 2, waitMs);
    m_set.stop();
    QVERIFY(m_set.start());
    QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 2, waitMs);
    QCOMPARE(overrides().value(QStringLiteral("rgb.color")).toUInt(), RgbControl::colorFor(40, 0));
    QCOMPARE(overrides().value(QStringLiteral("rgb.enabled")), QVariant(true));
}

void RgbTest::overridesGoneWithClient()
{
    {
        QDBusConnection own = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("rgb-own"));
        MicroSet::Client client(own);
        RgbControl control(&client);
        started(control, QStringLiteral("accent"), 300);
        QTRY_COMPARE_WITH_TIMEOUT(overrides().size(), 2, waitMs);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("rgb-own"));
    QTRY_VERIFY_WITH_TIMEOUT(overrides().isEmpty(), waitMs);
    QCOMPARE(m_client->get(QStringLiteral("rgb.color")).toUInt(), 16777215u);
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
