#include "DbusPad.h"
#include "Intercept.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QList>
#include <QProcess>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QtTest>

#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int quietMs = 150;
constexpr int firstEarlyMs = 20;
constexpr int firstLateMs = 100;
constexpr int gapSlackMs = 10;
constexpr int meanToleranceMs = 25;
constexpr int repeatCount = 6;

QString controlService()
{
    return Intercept::service();
}

QString controlPath()
{
    return QStringLiteral("/net/seilent/MicroDap/FakeInputPlumber");
}

QString controlInterface()
{
    return QStringLiteral("net.seilent.MicroDap.FakeInputPlumber");
}

QString devicePath(int index)
{
    return Intercept::rootPath() + QStringLiteral("/CompositeDevice%1").arg(index);
}

QString targetPath(const QString &name)
{
    return Intercept::rootPath() + QStringLiteral("/devices/target/") + name;
}

QString arbiterControl()
{
    return QStringLiteral("net.seilent.MicroDap.FakeArbiter");
}

QDBusMessage arbiterCall(const QDBusConnection &bus, const QString &method)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                             Intercept::arbiterInterface(), method);
    return bus.call(call, QDBus::Block, busTimeoutMs);
}

QString holder()
{
    QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                       QStringLiteral("org.freedesktop.DBus.Properties"),
                                                       QStringLiteral("Get"));
    call << Intercept::arbiterInterface() << QStringLiteral("Holder");
    const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return QStringLiteral("error");
    return qvariant_cast<QDBusVariant>(reply.arguments().value(0)).variant().toString();
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
    void showAcquires();
    void pressRelease();
    void repeat();
    void names_data();
    void names();
    void threshold();
    void dropped();
    void lateDevice();
    void screenOff();
    void otherHolderOnTop();
    void arbiterRestart();
    void rumble();
    void hideReleases();
    void quickToggle();
    void targetsComeAndGo();
    void noModeWrites();

private:
    QDBusMessage control(const QString &method, const QList<QVariant> &arguments = {});
    int mode(const QString &path);
    QList<uint> writes(const QString &path);
    QList<double> rumbles(const QString &path);
    void emitEvent(const QString &target, const QString &name, double value);
    bool startFake();
    bool startArbiter();
    void stopArbiter();

    QProcess m_fake;
    QProcess m_arbiter;
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

QList<double> DbusPadTest::rumbles(const QString &path)
{
    const QDBusMessage reply = control(QStringLiteral("Rumbles"), {path});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return {};
    return qdbus_cast<QList<double>>(reply.arguments().value(0));
}

void DbusPadTest::emitEvent(const QString &target, const QString &name, double value)
{
    const QDBusMessage reply = control(QStringLiteral("Emit"), {target, name, value});
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
}

bool DbusPadTest::startFake()
{
    m_fake.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_fake.start(QStringLiteral("python3"), {QStringLiteral(MICRODAP_FAKE_INPUTPLUMBER)});
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

bool DbusPadTest::startArbiter()
{
    m_arbiter.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_arbiter.start(QStringLiteral("python3"), {QStringLiteral(MICRODAP_FAKE_ARBITER)});
    if (!m_arbiter.waitForStarted(busTimeoutMs))
        return false;
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < busTimeoutMs) {
        if (iface->isServiceRegistered(Intercept::arbiterService()))
            return true;
        if (m_arbiter.state() != QProcess::Running)
            return false;
        QTest::qWait(50);
    }
    return false;
}

void DbusPadTest::stopArbiter()
{
    if (m_arbiter.state() != QProcess::Running)
        return;
    const QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                             arbiterControl(), QStringLiteral("Quit"));
    QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
    if (!m_arbiter.waitForFinished(busTimeoutMs)) {
        m_arbiter.terminate();
        m_arbiter.waitForFinished(busTimeoutMs);
    }
}

void DbusPadTest::initTestCase()
{
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    if (!startFake())
        QSKIP("fake InputPlumber did not start (python3 with dbus and gi needed)");
    QVERIFY(startArbiter());

    qputenv("MICRODAP_INPUTPLUMBER_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QDBusConnection bus = Intercept::bus();
    QVERIFY2(bus.isConnected(), qPrintable(bus.lastError().message()));
    QVERIFY(bus.name() != session.name());

    qRegisterMetaType<PadSource::Action>();
    m_pad = std::make_unique<DbusPad>(session, bus);
}

void DbusPadTest::cleanupTestCase()
{
    m_pad.reset();
    stopArbiter();
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
    QVERIFY(!m_pad->holding());
    QCOMPARE(holder(), QString());

    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 0);
}

void DbusPadTest::showAcquires()
{
    QSignalSpy holding(m_pad->intercept(), &Intercept::holdingChanged);
    m_pad->setShown(true);
    QVERIFY(m_pad->shown());
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QCOMPARE(holding.count(), 1);
    QCOMPARE(holder(), QDBusConnection::sessionBus().baseService());
    QCOMPARE(mode(devicePath(0)), 0);
    QCOMPARE(mode(devicePath(1)), 0);
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

    QTest::ignoreMessage(QtInfoMsg, "microdap dbuspad: dropped \"ui_guide\"");
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
    QVERIFY(m_pad->holding());
    QSignalSpy devices(m_pad->intercept(), &Intercept::devicesChanged);
    const QDBusMessage reply = control(QStringLiteral("AddDevice"));
    QVERIFY2(reply.type() != QDBusMessage::ErrorMessage, qPrintable(reply.errorMessage()));
    const QString path = reply.arguments().value(0).toString();
    QCOMPARE(path, devicePath(2));
    QTRY_COMPARE_WITH_TIMEOUT(devices.count(), 1, busTimeoutMs);
    QVERIFY(m_pad->intercept()->devices().contains(path));
    QTest::qWait(quietMs);
    QCOMPARE(mode(path), 0);
    QCOMPARE(writes(path), QList<uint>());
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

void DbusPadTest::otherHolderOnTop()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_down"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);

    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("other"));
    auto disconnectOther = qScopeGuard([] { QDBusConnection::disconnectFromBus(QStringLiteral("other")); });
    QVERIFY(other.isConnected());
    QCOMPARE(arbiterCall(other, QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_pad->holding(), busTimeoutMs);
    QCOMPARE(holder(), other.baseService());
    QCOMPARE(released.count(), 1);
    QCOMPARE(actionAt(released, 0), PadSource::Down);

    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_down"), 0.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 1);

    QCOMPARE(arbiterCall(other, QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 2, busTimeoutMs);
    QCOMPARE(actionAt(pressed, 1), PadSource::A);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 2, busTimeoutMs);
}

void DbusPadTest::arbiterRestart()
{
    QVERIFY(m_pad->holding());
    stopArbiter();
    QTRY_VERIFY_WITH_TIMEOUT(!m_pad->holding(), busTimeoutMs);
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
    QTest::qWait(quietMs);
    QCOMPARE(pressed.count(), 0);

    QVERIFY(startArbiter());
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QCOMPARE(holder(), QDBusConnection::sessionBus().baseService());
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    emitEvent(QStringLiteral("dbus0"), QStringLiteral("ui_accept"), 0.0);
}

void DbusPadTest::rumble()
{
    QCOMPARE(control(QStringLiteral("ClearWrites")).type(), QDBusMessage::ReplyMessage);
    m_pad->intercept()->rumble({15, 50, 15});
    const QList<double> expected{1.0, 0.0, 1.0, 0.0};
    QTRY_COMPARE_WITH_TIMEOUT(rumbles(devicePath(0)), expected, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(rumbles(devicePath(1)), expected, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(rumbles(devicePath(2)), expected, busTimeoutMs);
}

void DbusPadTest::hideReleases()
{
    QSignalSpy pressed(m_pad.get(), &PadSource::pressed);
    QSignalSpy released(m_pad.get(), &PadSource::released);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_down"), 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(pressed.count(), 1, busTimeoutMs);
    QVERIFY(m_pad->shown());

    m_pad->setShown(false);
    QVERIFY(!m_pad->shown());
    QVERIFY(!m_pad->holding());
    QCOMPARE(released.count(), 1);
    QCOMPARE(actionAt(released, 0), PadSource::Down);
    QTRY_COMPARE_WITH_TIMEOUT(holder(), QString(), busTimeoutMs);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_down"), 0.0);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_accept"), 1.0);
    QTest::qWait(quietMs);
    QCOMPARE(released.count(), 1);
    QCOMPARE(pressed.count(), 1);
    emitEvent(QStringLiteral("dbus1"), QStringLiteral("ui_accept"), 0.0);
}

void DbusPadTest::quickToggle()
{
    QVERIFY(!m_pad->shown());
    m_pad->setShown(true);
    m_pad->setShown(false);
    m_pad->setShown(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QCOMPARE(holder(), QDBusConnection::sessionBus().baseService());

    m_pad->setShown(false);
    m_pad->setShown(true);
    m_pad->setShown(false);
    QTRY_COMPARE_WITH_TIMEOUT(holder(), QString(), busTimeoutMs);
    QTest::qWait(quietMs);
    QVERIFY(!m_pad->holding());
    QCOMPARE(holder(), QString());
}

void DbusPadTest::targetsComeAndGo()
{
    m_pad->setShown(true);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
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

void DbusPadTest::noModeWrites()
{
    QCOMPARE(writes(devicePath(0)), QList<uint>());
    QCOMPARE(writes(devicePath(1)), QList<uint>());
    QCOMPARE(writes(devicePath(2)), QList<uint>());
}

QTEST_GUILESS_MAIN(DbusPadTest)
#include "tst_dbuspad.moc"
