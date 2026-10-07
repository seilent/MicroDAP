#include "DbusPad.h"
#include "Intercept.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QElapsedTimer>
#include <QList>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace casdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int quietMs = 150;
constexpr int firstEarlyMs = 20;
constexpr int firstLateMs = 100;
constexpr int gapSlackMs = 10;
constexpr int meanToleranceMs = 25;
constexpr int repeatCount = 6;
constexpr int writeDelayMs = 200;

QString controlService()
{
    return Intercept::service();
}

QString controlPath()
{
    return QStringLiteral("/org/casdap/FakeInputPlumber");
}

QString controlInterface()
{
    return QStringLiteral("org.casdap.FakeInputPlumber");
}

QString devicePath(int index)
{
    return Intercept::rootPath() + QStringLiteral("/CompositeDevice%1").arg(index);
}

QString targetPath(const QString &name)
{
    return Intercept::rootPath() + QStringLiteral("/devices/target/") + name;
}

PadSource::Action actionAt(const QSignalSpy &spy, int i)
{
    return spy.at(i).at(0).value<PadSource::Action>();
}

bool repeatAt(const QSignalSpy &spy, int i)
{
    return spy.at(i).at(1).toBool();
}

}

class DbusPadTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void connectedAtStart();
    void showWritesThree();
    void pressRelease();
    void repeat();
    void names_data();
    void names();
    void threshold();
    void dropped();
    void lateDevice();
    void pocketControls();
    void screenOff();
    void hideWritesZero();
    void serializedWrites();
    void targetsComeAndGo();
    void destructorWritesZero();
    void hideThenDestroy();

private:
    QDBusMessage control(const QString &method, const QList<QVariant> &arguments = {});
    int mode(const QString &path);
    QList<uint> writes(const QString &path);
    void emitEvent(const QString &target, const QString &name, double value);
    bool startFake();

    QProcess m_fake;
    std::unique_ptr<DbusPad> m_pad;
};

QDBusMessage DbusPadTest::control(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(controlService(), controlPath(), controlInterface(),
                                                       method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

int DbusPadTest::mode(const QString &path)
{
    const QDBusMessage reply = control(QStringLiteral("GetMode"), {path});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return -1;
    return reply.arguments().value(0).toInt();
}

QList<uint> DbusPadTest::writes(const QString &path)
{
    const QDBusMessage reply = control(QStringLiteral("Writes"), {path});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return {};
    return qdbus_cast<QList<uint>>(reply.arguments().value(0));
}

void DbusPadTest::emitEvent(const QString &target, const QString &name, double value)
{
    const QDBusMessage reply = control(QStringLiteral("Emit"), {target, name, value});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
}

bool DbusPadTest::startFake()
{
    m_fake.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_fake.start(QStringLiteral("python3"), {QStringLiteral(CASDAP_FAKE_INPUTPLUMBER)});
    if (!m_fake.waitForStarted(busTimeoutMs))
        return false;
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < busTimeoutMs) {
        if (iface->isServiceRegistered(controlService()))
            return true;
        if (m_fake.state() != QProcess::Running)
            return false;
        QTest::qWait(50);
    }
    return false;
}

void DbusPadTest::initTestCase()
{
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    if (!startFake())
        QSKIP("fake InputPlumber did not start (python3 with dbus and gi needed)");

    qputenv("CASDAP_INPUTPLUMBER_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QDBusConnection bus = Intercept::bus();
    QVERIFY2(bus.isConnected(), qPrintable(bus.lastError().message()));
    QVERIFY(bus.name() != session.name());

    qRegisterMetaType<PadSource::Action>();
    m_pad = std::make_unique<DbusPad>(bus);
}

void DbusPadTest::cleanupTestCase()
{
    m_pad.reset();
    if (m_fake.state() == QProcess::Running) {
        control(QStringLiteral("Quit"));
        if (!m_fake.waitForFinished(busTimeoutMs)) {
            m_fake.terminate();
            m_fake.waitForFinished(busTimeoutMs);
        }
    }
}

void DbusPadTest::connectedAtStart()
{
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->connected(), busTimeoutMs);
    const QStringList devices{devicePath(0), devicePath(1)};
    const QStringList targets{targetPath(QStringLiteral("dbus0")), targetPath(QStringLiteral("dbus1"))};
    QCOMPARE(m_pad->intercept()->devices(), devices);
    QCOMPARE(m_pad->intercept()->targets(), targets);
    QVERIFY(!m_pad->shown());
    QCOMPARE(mode(devicePath(0)), 0);
    QCOMPARE(mode(devicePath(1)), 0);
}

void DbusPadTest::showWritesThree()
{
    m_pad->setShown(true);
    QVERIFY(m_pad->shown());
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 3, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(1)), 3, busTimeoutMs);
}

void DbusPadTest::pressRelease()
{
    QSignalSpy actions(m_pad.get(), &PadSource::action);
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_down"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(pressed, 0), PadSource::Down);
    QCOMPARE(actions.count(), 1);
    QCOMPARE(actionAt(actions, 0), PadSource::Down);
    QVERIFY(!repeatAt(actions, 0));
    QCOMPARE(released.count(), 0);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_down"), 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(released, 0), PadSource::Down);
    QTest::qWait(quietMs);
    QCOMPARE(actions.count(), 1);
    QCOMPARE(pressed.count(), 1);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_down"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(released.count(), 1);
}

void DbusPadTest::repeat()
{
    QList<qint64> stamps;
    QList<bool> repeats;
    QElapsedTimer clock;
    clock.start();
    QMetaObject::Connection tick = connect(m_pad.get(), &PadSource::action, this,
                                           [&stamps, &repeats, &clock](PadSource::Action a, bool repeat) {
        if (a != PadSource::Up)
            return;
        stamps.append(clock.elapsed());
        repeats.append(repeat);
    });
    auto untick = qScopeGuard([tick] { disconnect(tick); });
    QSignalSpy released(m_pad.get(), &PadSource::released);

    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_up"), 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(stamps.size() >= repeatCount, busTimeoutMs);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_up"), 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(released, 0), PadSource::Up);
    const int settled = static_cast<int>(stamps.size());
    QTest::qWait(quietMs);
    QCOMPARE(static_cast<int>(stamps.size()), settled);

    QCOMPARE(repeats.first(), false);
    for (int i = 1; i < repeats.size(); ++i)
        QVERIFY(repeats.at(i));

    const qint64 first = stamps.at(1) - stamps.at(0);
    QVERIFY2(first >= DbusPad::RepeatInitialMs - firstEarlyMs && first <= DbusPad::RepeatInitialMs + firstLateMs,
             qPrintable(QStringLiteral("first repeat after %1 ms").arg(first)));
    qint64 sum = 0;
    for (int i = 2; i < stamps.size(); ++i) {
        const qint64 gap = stamps.at(i) - stamps.at(i - 1);
        QVERIFY2(gap >= DbusPad::RepeatIntervalMs - gapSlackMs,
                 qPrintable(QStringLiteral("repeat %1 after %2 ms").arg(i).arg(gap)));
        sum += gap;
    }
    const double mean = static_cast<double>(sum) / (stamps.size() - 2);
    QVERIFY2(qAbs(mean - DbusPad::RepeatIntervalMs) <= meanToleranceMs,
             qPrintable(QStringLiteral("mean repeat gap %1 ms").arg(mean)));
}

void DbusPadTest::names_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<int>("action");
    QTest::addColumn<QString>("label");

    QTest::newRow("up") << QStringLiteral("ui_up") << int(PadSource::Up) << QStringLiteral("up");
    QTest::newRow("down") << QStringLiteral("ui_down") << int(PadSource::Down) << QStringLiteral("down");
    QTest::newRow("left") << QStringLiteral("ui_left") << int(PadSource::Left) << QStringLiteral("left");
    QTest::newRow("right") << QStringLiteral("ui_right") << int(PadSource::Right) << QStringLiteral("right");
    QTest::newRow("accept") << QStringLiteral("ui_accept") << int(PadSource::A) << QStringLiteral("a");
    QTest::newRow("back") << QStringLiteral("ui_back") << int(PadSource::B) << QStringLiteral("b");
    QTest::newRow("context") << QStringLiteral("ui_context") << int(PadSource::X) << QStringLiteral("x");
    QTest::newRow("action") << QStringLiteral("ui_action") << int(PadSource::Y) << QStringLiteral("y");
    QTest::newRow("l1") << QStringLiteral("ui_l1") << int(PadSource::LB) << QStringLiteral("lb");
    QTest::newRow("r1") << QStringLiteral("ui_r1") << int(PadSource::RB) << QStringLiteral("rb");
    QTest::newRow("l2") << QStringLiteral("ui_l2") << int(PadSource::LT) << QStringLiteral("lt");
    QTest::newRow("r2") << QStringLiteral("ui_r2") << int(PadSource::RT) << QStringLiteral("rt");
    QTest::newRow("option") << QStringLiteral("ui_option") << int(PadSource::Start) << QStringLiteral("start");
    QTest::newRow("select") << QStringLiteral("ui_select") << int(PadSource::Back) << QStringLiteral("back");
}

void DbusPadTest::names()
{
    QFETCH(QString, name);
    QFETCH(int, action);
    QFETCH(QString, label);

    QCOMPARE(DbusPad::actionFor(name), action);
    QCOMPARE(m_pad->actionName(static_cast<PadSource::Action>(action)), label);

    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    emitEvent(QStringLiteral("dbus1"), name, 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QCOMPARE(int(actionAt(pressed, 0)), action);
    emitEvent(QStringLiteral("dbus1"), name, 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QCOMPARE(int(actionAt(released, 0)), action);
}

void DbusPadTest::threshold()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_l2"), 0.49);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 0);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_l2"), 0.5);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(pressed, 0), PadSource::LT);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_l2"), 0.7);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(released.count(), 0);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_l2"), 0.3);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(released, 0), PadSource::LT);
}

void DbusPadTest::dropped()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy actions(m_pad.get(), &PadSource::action);
    QCOMPARE(m_pad->dropped(), QStringList());

    QTest::ignoreMessage(QtInfoMsg, "casdap dbuspad: dropped \"ui_guide\"");
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_guide"), 1.0);
    const QStringList guideOnly{QStringLiteral("ui_guide")};
    QTRY_COMPARE_WITH_TIMEOUT(m_pad->dropped(), guideOnly, busTimeoutMs);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_guide"), 0.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_guide"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_quick"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_l3"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_bogus"), 1.0);
    const QStringList expected{QStringLiteral("ui_bogus"), QStringLiteral("ui_guide"), QStringLiteral("ui_l3"),
                               QStringLiteral("ui_quick")};
    QTRY_COMPARE_WITH_TIMEOUT(m_pad->dropped(), expected, busTimeoutMs);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 0);
    QCOMPARE(actions.count(), 0);
}

void DbusPadTest::lateDevice()
{
    QVERIFY(m_pad->shown());
    QSignalSpy devices(m_pad->intercept(), &Intercept::devicesChanged);
    const QDBusMessage reply = control(QStringLiteral("AddDevice"));
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    const QString path = reply.arguments().value(0).toString();
    QCOMPARE(path, devicePath(2));
    QTRY_COMPARE_WITH_TIMEOUT(devices.count(), 1, busTimeoutMs);
    QVERIFY(m_pad->intercept()->devices().contains(path));
    QTRY_COMPARE_WITH_TIMEOUT(mode(path), 3, busTimeoutMs);
    QCOMPARE(mode(devicePath(0)), 3);
    QCOMPARE(mode(devicePath(1)), 3);
}

void DbusPadTest::pocketControls()
{
    QSignalSpy changed(m_pad.get(), &PadSource::pocketControlsChanged);
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    QVERIFY(!m_pad->pocketControls());
    m_pad->setPocketControls(true);
    QCOMPARE(changed.count(), 1);
    QVERIFY(m_pad->pocketControls());
    QVERIFY(!m_pad->screenOff());
    QVERIFY(m_pad->shown());
    QCOMPARE(mode(devicePath(0)), 3);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QCOMPARE(actionAt(pressed, 0), PadSource::A);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);

    m_pad->setPocketControls(false);
    QCOMPARE(changed.count(), 2);
}

void DbusPadTest::screenOff()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    QSignalSpy actions(m_pad.get(), &PadSource::action);
    QSignalSpy changed(m_pad.get(), &PadSource::screenOffChanged);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_right"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);

    m_pad->setScreenOff(true);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(released.count(), 1);
    QCOMPARE(actionAt(released, 0), PadSource::Right);
    const int before = actions.count();
    QTest::qWait(DbusPad::RepeatInitialMs + DbusPad::RepeatIntervalMs);
    QCOMPARE(actions.count(), before);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_right"), 0.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 1);
    QCOMPARE(actions.count(), before);

    m_pad->setScreenOff(false);
    QCOMPARE(changed.count(), 2);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 2, busTimeoutMs);
    QCOMPARE(actionAt(pressed, 1), PadSource::A);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 2, busTimeoutMs);
}

void DbusPadTest::hideWritesZero()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_down"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QVERIFY(m_pad->shown());

    m_pad->setShown(false);
    QVERIFY(!m_pad->shown());
    QCOMPARE(released.count(), 1);
    QCOMPARE(actionAt(released, 0), PadSource::Down);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 0, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(1)), 0, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(2)), 0, busTimeoutMs);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_down"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(released.count(), 1);

    m_pad->setShown(false);
    QTest::qWait(quietMs);
    QCOMPARE(mode(devicePath(0)), 0);
}

void DbusPadTest::serializedWrites()
{
    QVERIFY(!m_pad->shown());
    QTest::qWait(quietMs);
    QCOMPARE(control(QStringLiteral("ClearWrites")).type(), QDBusMessage::ReplyMessage);
    QCOMPARE(control(QStringLiteral("SetDelay"), {QVariant::fromValue(uint(writeDelayMs))}).type(),
             QDBusMessage::ReplyMessage);
    auto restore = qScopeGuard([this] { control(QStringLiteral("SetDelay"), {QVariant::fromValue(0u)}); });

    m_pad->setShown(true);
    m_pad->setShown(false);
    m_pad->setShown(true);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 3, busTimeoutMs);
    QTest::qWait(writeDelayMs + quietMs);
    const QList<uint> shown{3};
    QCOMPARE(writes(devicePath(0)), shown);
    QCOMPARE(writes(devicePath(1)), shown);
    QCOMPARE(writes(devicePath(2)), shown);
    QCOMPARE(mode(devicePath(1)), 3);

    m_pad->setShown(false);
    m_pad->setShown(true);
    m_pad->setShown(false);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 0, busTimeoutMs);
    QTest::qWait(writeDelayMs + quietMs);
    const QList<uint> hidden{3, 0};
    QCOMPARE(writes(devicePath(0)), hidden);
    QCOMPARE(mode(devicePath(1)), 0);
}

void DbusPadTest::targetsComeAndGo()
{
    QSignalSpy connected(m_pad.get(), &PadSource::connectedChanged);
    QVERIFY(m_pad->connected());

    QDBusMessage reply = control(QStringLiteral("RemoveTarget"), {QStringLiteral("dbus0")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    QTRY_COMPARE_WITH_TIMEOUT(connected.count(), 1, busTimeoutMs);
    QVERIFY(m_pad->connected());

    reply = control(QStringLiteral("RemoveTarget"), {QStringLiteral("dbus1")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    QTRY_VERIFY_WITH_TIMEOUT(!m_pad->connected(), busTimeoutMs);
    QCOMPARE(connected.count(), 2);

    reply = control(QStringLiteral("AddTarget"), {QStringLiteral("dbus0")});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->connected(), busTimeoutMs);
    QCOMPARE(connected.count(), 3);

    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
}

void DbusPadTest::destructorWritesZero()
{
    m_pad->setShown(true);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 3, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(1)), 3, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(2)), 3, busTimeoutMs);

    m_pad.reset();
    QCOMPARE(mode(devicePath(0)), 0);
    QCOMPARE(mode(devicePath(1)), 0);
    QCOMPARE(mode(devicePath(2)), 0);
}

void DbusPadTest::hideThenDestroy()
{
    auto *pad = new DbusPad(Intercept::bus());
    pad->setShown(true);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 3, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(1)), 3, busTimeoutMs);

    pad->setShown(false);
    delete pad;
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(0)), 0, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(mode(devicePath(1)), 0, busTimeoutMs);
}

QTEST_GUILESS_MAIN(DbusPadTest)
#include "tst_dbuspad.moc"
