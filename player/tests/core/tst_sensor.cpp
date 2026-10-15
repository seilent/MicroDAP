#include "SensorRotation.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QProcess>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int shortMs = 200;
constexpr int earlyMs = 300;
constexpr int settledMs = 800;

QString controlService()
{
    return QStringLiteral("net.seilent.MicroDap.FakeSensorProxy");
}

QString controlPath()
{
    return QStringLiteral("/net/seilent/MicroDap/FakeSensorProxy");
}

}

class SensorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();
    void turnOf();
    void noOwner();
    void supportedFollowsHas();
    void claimReadsOrientation();
    void settleHolds();
    void shortValueReverted();
    void undefinedKeepsTurn();
    void firstUndefined();
    void releaseClearsTurn();
    void releaseKeepingTurn();
    void forgetAfterKeptRelease();
    void reclaimSameValue();
    void reclaimNewValue();
    void noSignalBeforeClaimReply();
    void vanishKeepsTurn();
    void vanishStaysAway();
    void vanishWithoutAccelerometer();
    void ownerChangeReclaims();

private:
    QDBusMessage control(const QString &method, const QList<QVariant> &arguments = {});
    uint count(const QString &method);
    bool owned();
    void appear();
    void vanish();
    void setOrientation(const QString &value);
    void claimSettled(const QString &value);

    QProcess m_fake;
    std::unique_ptr<SensorRotation> m_sensor;
};

QDBusMessage SensorTest::control(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(controlService(), controlPath(), controlService(), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

uint SensorTest::count(const QString &method)
{
    return control(method).arguments().value(0).toUInt();
}

bool SensorTest::owned()
{
    return QDBusConnection::sessionBus().interface()->isServiceRegistered(SensorRotation::service());
}

void SensorTest::appear()
{
    control(QStringLiteral("Appear"));
    QTRY_VERIFY_WITH_TIMEOUT(owned(), busTimeoutMs);
}

void SensorTest::vanish()
{
    control(QStringLiteral("Vanish"));
    QTRY_VERIFY_WITH_TIMEOUT(!owned(), busTimeoutMs);
}

void SensorTest::setOrientation(const QString &value)
{
    const QDBusMessage reply = control(QStringLiteral("SetOrientation"), {value});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
}

void SensorTest::claimSettled(const QString &value)
{
    appear();
    setOrientation(value);
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->supported(), busTimeoutMs);
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), SensorRotation::turnOf(value, m_sensor->sensorTurn()),
                              busTimeoutMs);
}

void SensorTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    m_fake.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_fake.start(QStringLiteral("python3"), {QStringLiteral(MICRODAP_FAKE_SENSORPROXY)});
    if (!m_fake.waitForStarted(busTimeoutMs))
        QSKIP("fake SensorProxy did not start");
    QElapsedTimer timer;
    timer.start();
    while (!QDBusConnection::sessionBus().interface()->isServiceRegistered(controlService())) {
        if (m_fake.state() != QProcess::Running || timer.elapsed() > busTimeoutMs)
            QSKIP("fake SensorProxy did not start (python3 with dbus and gi needed)");
        QTest::qWait(50);
    }
    qputenv("MICRODAP_SENSOR_BUS", "session");
}

void SensorTest::init()
{
    control(QStringLiteral("ClearCounts"));
    control(QStringLiteral("SetClaimDelay"), {0u});
    control(QStringLiteral("SetHas"), {true});
    setOrientation(QStringLiteral("undefined"));
}

void SensorTest::cleanup()
{
    m_sensor.reset();
    vanish();
}

void SensorTest::cleanupTestCase()
{
    m_sensor.reset();
    if (m_fake.state() == QProcess::Running) {
        control(QStringLiteral("Quit"));
        if (!m_fake.waitForFinished(busTimeoutMs)) {
            m_fake.terminate();
            m_fake.waitForFinished(busTimeoutMs);
        }
    }
}

void SensorTest::turnOf()
{
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("normal"), 1), 0);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("bottom-up"), 1), 2);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("left-up"), 1), 1);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("right-up"), 1), 3);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("left-up"), 3), 3);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("right-up"), 3), 1);
    QCOMPARE(SensorRotation::turnOf(QStringLiteral("undefined"), 1), -1);
    QCOMPARE(SensorRotation::turnOf(QString(), 1), -1);
    QCOMPARE(SensorRotation::defaultSensorTurn(), 1);
}

void SensorTest::noOwner()
{
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QTest::qWait(shortMs);
    QVERIFY(!m_sensor->supported());
    QCOMPARE(m_sensor->turn(), -1);
    m_sensor->setWanted(true);
    QTest::qWait(shortMs);
    QCOMPARE(count(QStringLiteral("Claims")), 0u);
    QVERIFY(!m_sensor->claimed());
    QCOMPARE(m_sensor->turn(), -1);
}

void SensorTest::supportedFollowsHas()
{
    control(QStringLiteral("SetHas"), {false});
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QSignalSpy spy(m_sensor.get(), &SensorRotation::supportedChanged);
    appear();
    QTest::qWait(shortMs);
    QVERIFY(!m_sensor->supported());
    m_sensor->setWanted(true);
    QTest::qWait(shortMs);
    QCOMPARE(count(QStringLiteral("Claims")), 0u);
    control(QStringLiteral("SetHas"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->supported(), busTimeoutMs);
    QCOMPARE(spy.count(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(count(QStringLiteral("Claims")), 1u);
    control(QStringLiteral("SetHas"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(!m_sensor->supported(), busTimeoutMs);
    QCOMPARE(spy.count(), 2);
}

void SensorTest::claimReadsOrientation()
{
    appear();
    setOrientation(QStringLiteral("bottom-up"));
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->supported(), busTimeoutMs);
    QCOMPARE(count(QStringLiteral("Claims")), 0u);
    QCOMPARE(m_sensor->raw(), QString());
    m_sensor->setWanted(true);
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(count(QStringLiteral("Claims")), 1u);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("bottom-up"), busTimeoutMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), 2, busTimeoutMs);
}

void SensorTest::settleHolds()
{
    claimSettled(QStringLiteral("normal"));
    QSignalSpy spy(m_sensor.get(), &SensorRotation::turnChanged);
    QElapsedTimer clock;
    setOrientation(QStringLiteral("left-up"));
    clock.start();
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("left-up"), busTimeoutMs);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("normal"));
    QTest::qWait(earlyMs);
    QCOMPARE(m_sensor->turn(), 0);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("normal"));
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), m_sensor->sensorTurn(), busTimeoutMs);
    QVERIFY2(clock.elapsed() >= SensorRotation::settleMs - 20, qPrintable(QString::number(clock.elapsed())));
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QCOMPARE(spy.count(), 1);
}

void SensorTest::shortValueReverted()
{
    claimSettled(QStringLiteral("left-up"));
    QSignalSpy spy(m_sensor.get(), &SensorRotation::turnChanged);
    setOrientation(QStringLiteral("normal"));
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("normal"), busTimeoutMs);
    QTest::qWait(shortMs);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    setOrientation(QStringLiteral("left-up"));
    QTest::qWait(settledMs);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QCOMPARE(spy.count(), 0);
}

void SensorTest::undefinedKeepsTurn()
{
    claimSettled(QStringLiteral("left-up"));
    QSignalSpy spy(m_sensor.get(), &SensorRotation::turnChanged);
    setOrientation(QStringLiteral("undefined"));
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("undefined"), busTimeoutMs);
    QTest::qWait(settledMs);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QCOMPARE(spy.count(), 0);
}

void SensorTest::firstUndefined()
{
    appear();
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->supported(), busTimeoutMs);
    m_sensor->setWanted(true);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("undefined"), busTimeoutMs);
    QTest::qWait(settledMs);
    QVERIFY(m_sensor->claimed());
    QCOMPARE(m_sensor->turn(), -1);
}

void SensorTest::releaseClearsTurn()
{
    claimSettled(QStringLiteral("left-up"));
    m_sensor->setWanted(false);
    QCOMPARE(m_sensor->turn(), -1);
    QVERIFY(!m_sensor->claimed());
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Releases")), 1u, busTimeoutMs);
    QVERIFY(m_sensor->supported());
    setOrientation(QStringLiteral("normal"));
    QTest::qWait(settledMs);
    QCOMPARE(m_sensor->turn(), -1);
    QCOMPARE(m_sensor->raw(), QStringLiteral("left-up"));
    QCOMPARE(m_sensor->settledRaw(), QString());
    setOrientation(QStringLiteral("left-up"));
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTest::qWait(earlyMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), m_sensor->sensorTurn(), busTimeoutMs);
}

void SensorTest::releaseKeepingTurn()
{
    claimSettled(QStringLiteral("left-up"));
    m_sensor->setWanted(false, true);
    QCOMPARE(m_sensor->turn(), -1);
    QVERIFY(!m_sensor->claimed());
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Releases")), 1u, busTimeoutMs);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
}

void SensorTest::forgetAfterKeptRelease()
{
    claimSettled(QStringLiteral("left-up"));
    m_sensor->setWanted(false, true);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QSignalSpy settled(m_sensor.get(), &SensorRotation::settledRawChanged);
    m_sensor->setWanted(false, false);
    QCOMPARE(m_sensor->turn(), -1);
    QCOMPARE(m_sensor->settledRaw(), QString());
    QCOMPARE(settled.count(), 1);
    m_sensor->setWanted(false, false);
    QCOMPARE(settled.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Releases")), 1u, busTimeoutMs);
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTest::qWait(earlyMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), m_sensor->sensorTurn(), busTimeoutMs);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
}

void SensorTest::reclaimSameValue()
{
    claimSettled(QStringLiteral("left-up"));
    m_sensor->setWanted(false, true);
    QSignalSpy spy(m_sensor.get(), &SensorRotation::turnChanged);
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(count(QStringLiteral("Claims")), 2u);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QTest::qWait(settledMs);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QCOMPARE(spy.count(), 1);
}

void SensorTest::reclaimNewValue()
{
    claimSettled(QStringLiteral("left-up"));
    m_sensor->setWanted(false, true);
    setOrientation(QStringLiteral("right-up"));
    const int next = SensorRotation::turnOf(QStringLiteral("right-up"), m_sensor->sensorTurn());
    m_sensor->setWanted(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("right-up"), busTimeoutMs);
    QTest::qWait(earlyMs);
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("left-up"));
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), next, busTimeoutMs);
    QCOMPARE(m_sensor->settledRaw(), QStringLiteral("right-up"));
}

void SensorTest::noSignalBeforeClaimReply()
{
    appear();
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->supported(), busTimeoutMs);
    control(QStringLiteral("SetClaimDelay"), {400u});
    m_sensor->setWanted(true);
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Claims")), 1u, busTimeoutMs);
    setOrientation(QStringLiteral("bottom-up"));
    QTest::qWait(shortMs);
    QVERIFY(!m_sensor->claimed());
    QCOMPARE(m_sensor->raw(), QString());
    QCOMPARE(m_sensor->turn(), -1);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->raw(), QStringLiteral("bottom-up"), busTimeoutMs);
}

void SensorTest::vanishKeepsTurn()
{
    qputenv("MICRODAP_SENSOR_RECLAIM_MS", "30000");
    claimSettled(QStringLiteral("left-up"));
    qunsetenv("MICRODAP_SENSOR_RECLAIM_MS");
    QSignalSpy supported(m_sensor.get(), &SensorRotation::supportedChanged);
    QSignalSpy turns(m_sensor.get(), &SensorRotation::turnChanged);
    vanish();
    QTest::qWait(shortMs);
    QVERIFY(m_sensor->supported());
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QVERIFY(!m_sensor->claimed());
    appear();
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Claims")), 2u, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QTest::qWait(settledMs);
    QVERIFY(m_sensor->supported());
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(supported.count(), 0);
    QCOMPARE(turns.count(), 0);
}

void SensorTest::vanishStaysAway()
{
    claimSettled(QStringLiteral("left-up"));
    vanish();
    QTest::qWait(shortMs);
    QVERIFY(m_sensor->supported());
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QTRY_VERIFY_WITH_TIMEOUT(!m_sensor->supported(), SensorRotation::reclaimMs + busTimeoutMs);
    QCOMPARE(m_sensor->turn(), -1);
    QCOMPARE(m_sensor->settledRaw(), QString());
    QCOMPARE(count(QStringLiteral("Claims")), 1u);
    appear();
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QCOMPARE(m_sensor->turn(), -1);
    QTRY_COMPARE_WITH_TIMEOUT(m_sensor->turn(), m_sensor->sensorTurn(), busTimeoutMs);
}

void SensorTest::vanishWithoutAccelerometer()
{
    control(QStringLiteral("SetHas"), {false});
    appear();
    m_sensor = std::make_unique<SensorRotation>(SensorRotation::bus());
    m_sensor->setWanted(true);
    QTest::qWait(shortMs);
    QVERIFY(!m_sensor->supported());
    QSignalSpy spy(m_sensor.get(), &SensorRotation::supportedChanged);
    vanish();
    QTest::qWait(shortMs);
    QVERIFY(!m_sensor->supported());
    QTest::qWait(SensorRotation::reclaimMs + shortMs);
    QVERIFY(!m_sensor->supported());
    QCOMPARE(spy.count(), 0);
    QCOMPARE(count(QStringLiteral("Claims")), 0u);
}

void SensorTest::ownerChangeReclaims()
{
    claimSettled(QStringLiteral("left-up"));
    QSignalSpy supported(m_sensor.get(), &SensorRotation::supportedChanged);
    QSignalSpy turns(m_sensor.get(), &SensorRotation::turnChanged);
    control(QStringLiteral("Handover"));
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("Claims")), 2u, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_sensor->claimed(), busTimeoutMs);
    QTest::qWait(settledMs);
    QVERIFY(m_sensor->supported());
    QCOMPARE(m_sensor->turn(), m_sensor->sensorTurn());
    QCOMPARE(supported.count(), 0);
    QCOMPARE(turns.count(), 0);
}

QTEST_GUILESS_MAIN(SensorTest)
#include "tst_sensor.moc"
