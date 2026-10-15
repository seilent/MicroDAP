#include "DeviceReservation.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDBusPendingReply>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest>

#include <cstdio>
#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int replyMs = 300;
constexpr int nameLostMs = 400;

const DeviceReservation::Bounds quick{1000, replyMs, nameLostMs};

QString controlService()
{
    return QStringLiteral("net.seilent.MicroDap.FakeReserve");
}

QString controlPath()
{
    return QStringLiteral("/control");
}

QString reserveName()
{
    return QStringLiteral("Audio1");
}

QString cardId()
{
    return QStringLiteral("B");
}

QString service()
{
    return DeviceReservation::serviceFor(reserveName());
}

struct FakeState
{
    QString owner;
    bool stuck = false;
    uint calls = 0;
    int lastReplyMs = -1;
    QString lastError;
    bool lastAnswer = false;
    uint pending = 0;
};

QDBusMessage control(const QString &method, const QList<QVariant> &arguments = {})
{
    QDBusMessage call = QDBusMessage::createMethodCall(controlService(), controlPath(), controlService(), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

FakeState fakeState()
{
    const QList<QVariant> values = control(QStringLiteral("State")).arguments();
    FakeState state;
    state.owner = values.value(0).toString();
    state.stuck = values.value(1).toBool();
    state.calls = values.value(2).toUInt();
    state.lastReplyMs = values.value(3).toInt();
    state.lastError = values.value(4).toString();
    state.lastAnswer = values.value(5).toBool();
    state.pending = values.value(6).toUInt();
    return state;
}

QString owner()
{
    return fakeState().owner;
}

QDBusPendingReply<bool> requestRelease(int priority)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service(), DeviceReservation::pathFor(reserveName()),
                                                       DeviceReservation::interfaceName(),
                                                       QStringLiteral("RequestRelease"));
    call << priority;
    return QDBusConnection::sessionBus().asyncCall(call, busTimeoutMs);
}

int child(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    DeviceReservation reservation(DeviceReservation::Bounds{});
    QObject::connect(&reservation, &DeviceReservation::acquired, &app, [] {
        std::fputs("held\n", stdout);
        std::fflush(stdout);
    });
    QObject::connect(&reservation, &DeviceReservation::failed, &app, [&app] {
        app.exit(1);
    });
    QObject::connect(&reservation, &DeviceReservation::refused, &app, [&app] {
        app.exit(1);
    });
    reservation.acquire(reserveName(), cardId(), 1);
    return app.exec();
}

}

class ReservationTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();
    void cleanupTestCase();
    void acquireTakesName();
    void exportedBeforeRequestName();
    void acquireRefused();
    void refuseWhileGuiBlocked();
    void grantWhenCardGone();
    void grantAfterMarkGone();
    void acceptHigherPriority();
    void releaseTimesOutWithoutNameLost();
    void releaseAnswersPending();
    void waitReleasedAfterRelease();
    void waitReleasedBound();
    void waitReleasedIgnoresEarlierRelease();
    void falseAtBoundDropsRequest();
    void lateGrantReleases();
    void twoCycles();
    void quitAnswersPending();
    void killedHolder();
    void unrequestedNameLost();
    void markGoneClearedByAcquire();
    void propertiesReadable();
    void connectionLostOnBusDrop();

private:
    void hold();
    void holdWithPendingCall();
    void writeCardId(const QString &id);

    QProcess m_fake;
    QTemporaryDir m_asound;
    std::unique_ptr<DeviceReservation> m_reservation;
};

void ReservationTest::hold()
{
    m_reservation = std::make_unique<DeviceReservation>(quick);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    m_reservation->acquire(reserveName(), cardId(), 7);
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, busTimeoutMs);
    QCOMPARE(acquired.first().first().toULongLong(), quint64(7));
    QTRY_VERIFY_WITH_TIMEOUT(fakeState().lastReplyMs >= 0, busTimeoutMs);
    const QString held = owner();
    QVERIFY(!held.isEmpty());
    QVERIFY(held != QLatin1StringView("fake"));
}

void ReservationTest::holdWithPendingCall()
{
    hold();
    QSignalSpy requested(m_reservation.get(), &DeviceReservation::releaseRequested);
    control(QStringLiteral("Request"), {20});
    QTRY_COMPARE_WITH_TIMEOUT(requested.count(), 1, busTimeoutMs);
    const FakeState state = fakeState();
    QCOMPARE(state.pending, 1u);
    QCOMPARE(state.lastReplyMs, -1);
}

void ReservationTest::writeCardId(const QString &id)
{
    const QString dir = m_asound.path() + QStringLiteral("/card1");
    QVERIFY(QDir().mkpath(dir));
    QFile file(dir + QStringLiteral("/id"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(id.toUtf8() + '\n');
}

void ReservationTest::initTestCase()
{
    if (!QDBusConnection::sessionBus().isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    QVERIFY(m_asound.isValid());
    qputenv("MICRODAP_ASOUND_DIR", m_asound.path().toUtf8());
    m_fake.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_fake.start(QStringLiteral("python3"), {QStringLiteral(MICRODAP_FAKE_RESERVE)});
    if (!m_fake.waitForStarted(busTimeoutMs))
        QSKIP("fake ReserveDevice1 did not start");
    QElapsedTimer timer;
    timer.start();
    while (!QDBusConnection::sessionBus().interface()->isServiceRegistered(controlService())) {
        if (m_fake.state() != QProcess::Running || timer.elapsed() > busTimeoutMs)
            QSKIP("fake ReserveDevice1 did not start (python3 with dbus and gi needed)");
        QTest::qWait(50);
    }
}

void ReservationTest::init()
{
    control(QStringLiteral("Reset"));
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    writeCardId(cardId());
}

void ReservationTest::cleanup()
{
    m_reservation.reset();
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QVERIFY(!fakeState().stuck);
}

void ReservationTest::cleanupTestCase()
{
    m_reservation.reset();
    if (m_fake.state() == QProcess::Running) {
        control(QStringLiteral("Quit"));
        if (!m_fake.waitForFinished(busTimeoutMs)) {
            m_fake.terminate();
            m_fake.waitForFinished(busTimeoutMs);
        }
    }
}

void ReservationTest::acquireTakesName()
{
    hold();
    const FakeState state = fakeState();
    QCOMPARE(state.calls, 1u);
    QVERIFY(!state.lastAnswer);
    QVERIFY(!state.stuck);
}

void ReservationTest::exportedBeforeRequestName()
{
    hold();
    const FakeState state = fakeState();
    QVERIFY(state.lastReplyMs >= 0);
    QCOMPARE(state.lastError, QString());
}

void ReservationTest::acquireRefused()
{
    control(QStringLiteral("SetAnswer"), {false});
    m_reservation = std::make_unique<DeviceReservation>(quick);
    QSignalSpy refused(m_reservation.get(), &DeviceReservation::refused);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    m_reservation->acquire(reserveName(), cardId(), 3);
    QTRY_COMPARE_WITH_TIMEOUT(refused.count(), 1, busTimeoutMs);
    QCOMPARE(refused.first().first().toULongLong(), quint64(3));
    QCOMPARE(acquired.count(), 0);
    QCOMPARE(owner(), QStringLiteral("fake"));
}

void ReservationTest::refuseWhileGuiBlocked()
{
    hold();
    const QString held = owner();
    control(QStringLiteral("Replug"));
    QThread::msleep(2000);
    const FakeState state = fakeState();
    QCOMPARE(state.calls, 2u);
    QVERIFY(state.lastReplyMs >= 0);
    QVERIFY(state.lastReplyMs < 1000);
    QCOMPARE(state.lastError, QString());
    QVERIFY(!state.lastAnswer);
    QCOMPARE(state.owner, held);
}

void ReservationTest::grantWhenCardGone()
{
    hold();
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    QVERIFY(QFile::remove(m_asound.path() + QStringLiteral("/card1/id")));
    control(QStringLiteral("Replug"));
    QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, busTimeoutMs);
    QCOMPARE(lost.at(0).at(0).toBool(), true);
    const FakeState state = fakeState();
    QVERIFY(state.lastAnswer);
    QCOMPARE(state.owner, QStringLiteral("fake"));
}

void ReservationTest::grantAfterMarkGone()
{
    hold();
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    control(QStringLiteral("Replug"));
    QTRY_COMPARE_WITH_TIMEOUT(fakeState().calls, 2u, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fakeState().lastReplyMs >= 0, busTimeoutMs);
    QVERIFY(!fakeState().lastAnswer);
    QVERIFY(owner() != QLatin1StringView("fake"));
    m_reservation->markGone();
    control(QStringLiteral("Replug"));
    QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, busTimeoutMs);
    QCOMPARE(lost.at(0).at(0).toBool(), true);
    QVERIFY(fakeState().lastAnswer);
    QCOMPARE(owner(), QStringLiteral("fake"));
}

void ReservationTest::acceptHigherPriority()
{
    hold();
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    QSignalSpy timedOut(m_reservation.get(), &DeviceReservation::releaseTimedOut);
    QList<int> priorities;
    int replyAtRequest = 0;
    connect(m_reservation.get(), &DeviceReservation::releaseRequested, this, [&](int priority) {
        priorities.append(priority);
        replyAtRequest = fakeState().lastReplyMs;
        m_reservation->grantRelease();
    });
    control(QStringLiteral("Request"), {20});
    QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, busTimeoutMs);
    QCOMPARE(lost.at(0).at(0).toBool(), true);
    QCOMPARE(priorities, QList<int>{20});
    QCOMPARE(replyAtRequest, -1);
    const FakeState state = fakeState();
    QVERIFY(state.lastAnswer);
    QCOMPARE(state.owner, QStringLiteral("fake"));
    QTest::qWait(nameLostMs + 200);
    QCOMPARE(timedOut.count(), 0);
}

void ReservationTest::releaseTimesOutWithoutNameLost()
{
    hold();
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    QSignalSpy timedOut(m_reservation.get(), &DeviceReservation::releaseTimedOut);
    connect(m_reservation.get(), &DeviceReservation::releaseRequested, this, [this] {
        m_reservation->grantRelease();
    });
    QElapsedTimer timer;
    timer.start();
    QDBusPendingReply<bool> reply = requestRelease(20);
    QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), busTimeoutMs);
    QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), true);
    QTRY_COMPARE_WITH_TIMEOUT(timedOut.count(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= nameLostMs - 20);
    QCOMPARE(lost.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
}

void ReservationTest::releaseAnswersPending()
{
    hold();
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    connect(m_reservation.get(), &DeviceReservation::releaseRequested, this, [this] {
        m_reservation->release();
    });
    control(QStringLiteral("Request"), {20});
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fakeState().lastReplyMs >= 0, busTimeoutMs);
    const FakeState state = fakeState();
    QVERIFY(state.lastAnswer);
    QCOMPARE(state.lastError, QString());
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QVERIFY(!fakeState().stuck);
}

void ReservationTest::waitReleasedAfterRelease()
{
    holdWithPendingCall();
    m_reservation->release();
    QVERIFY(m_reservation->waitReleased(1000));
    QTRY_COMPARE_WITH_TIMEOUT(fakeState().pending, 0u, busTimeoutMs);
    const FakeState state = fakeState();
    QVERIFY(state.lastAnswer);
    QCOMPARE(state.lastError, QString());
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QVERIFY(!fakeState().stuck);
}

void ReservationTest::waitReleasedBound()
{
    hold();
    QElapsedTimer timer;
    timer.start();
    QVERIFY(!m_reservation->waitReleased(200));
    QVERIFY(timer.elapsed() >= 190);
    QVERIFY(owner() != QLatin1StringView("fake"));
}

void ReservationTest::waitReleasedIgnoresEarlierRelease()
{
    hold();
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    m_reservation->release();
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    m_reservation->acquire(reserveName(), cardId(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, busTimeoutMs);
    const QString held = owner();
    QVERIFY(held != QLatin1StringView("fake"));
    m_reservation->release();
    QVERIFY(m_reservation->waitReleased(1000));
    const QDBusReply<QString> after = QDBusConnection::sessionBus().interface()->serviceOwner(service());
    QVERIFY(!after.isValid() || after.value() != held);
}

void ReservationTest::falseAtBoundDropsRequest()
{
    hold();
    const QString held = owner();
    QSignalSpy requested(m_reservation.get(), &DeviceReservation::releaseRequested);
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    QDBusPendingReply<bool> reply = requestRelease(20);
    QThread::msleep(replyMs + 400);
    QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), busTimeoutMs);
    QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), false);
    QTest::qWait(200);
    QCOMPARE(requested.count(), 0);
    QCOMPARE(released.count(), 0);
    QCOMPARE(owner(), held);
}

void ReservationTest::lateGrantReleases()
{
    hold();
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    connect(m_reservation.get(), &DeviceReservation::releaseRequested, this, [this] {
        QThread::msleep(replyMs + 300);
        m_reservation->grantRelease();
    });
    QDBusPendingReply<bool> reply = requestRelease(20);
    QTRY_VERIFY_WITH_TIMEOUT(reply.isFinished(), busTimeoutMs);
    QVERIFY2(!reply.isError(), qPrintable(reply.error().message()));
    QCOMPARE(reply.value(), false);
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
}

void ReservationTest::twoCycles()
{
    hold();
    const QString first = owner();
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    m_reservation->release();
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    m_reservation->acquire(reserveName(), cardId(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, busTimeoutMs);
    const QString second = owner();
    QVERIFY(second != QLatin1StringView("fake"));
    QVERIFY(!second.isEmpty());
    QVERIFY(second != first);
    QVERIFY(!QDBusConnection::sessionBus().interface()->isServiceRegistered(first));
}

void ReservationTest::quitAnswersPending()
{
    holdWithPendingCall();
    m_reservation->quitAndJoin(1000);
    QVERIFY(m_reservation->waitReleased(0));
    QTRY_COMPARE_WITH_TIMEOUT(fakeState().pending, 0u, busTimeoutMs);
    const FakeState state = fakeState();
    QVERIFY(state.lastAnswer);
    QCOMPARE(state.lastError, QString());
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QVERIFY(!fakeState().stuck);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    m_reservation->acquire(reserveName(), cardId(), 1);
    m_reservation->release();
    m_reservation->markGone();
    QTest::qWait(200);
    QCOMPARE(acquired.count(), 0);
    QCOMPARE(owner(), QStringLiteral("fake"));
}

void ReservationTest::killedHolder()
{
    QProcess holder;
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("MICRODAP_RESERVE_CHILD"), QStringLiteral("1"));
    env.insert(QStringLiteral("MICRODAP_RESERVE_BUS"), env.value(QStringLiteral("DBUS_SESSION_BUS_ADDRESS")));
    holder.setProcessEnvironment(env);
    holder.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    holder.start(QCoreApplication::applicationFilePath(), {});
    QVERIFY(holder.waitForStarted(busTimeoutMs));
    QByteArray out;
    QTRY_VERIFY_WITH_TIMEOUT((out += holder.readAllStandardOutput()).contains("held"), busTimeoutMs);
    QVERIFY(owner() != QLatin1StringView("fake"));
    holder.kill();
    QVERIFY(holder.waitForFinished(busTimeoutMs));
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QVERIFY(!fakeState().stuck);
}

void ReservationTest::unrequestedNameLost()
{
    hold();
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    control(QStringLiteral("Steal"));
    QTRY_COMPARE_WITH_TIMEOUT(lost.count(), 1, busTimeoutMs);
    QCOMPARE(lost.at(0).at(0).toBool(), false);
    QCOMPARE(owner(), QStringLiteral("fake"));
    QVERIFY(m_reservation->waitReleased(0));
}

void ReservationTest::markGoneClearedByAcquire()
{
    hold();
    m_reservation->markGone();
    QSignalSpy released(m_reservation.get(), &DeviceReservation::released);
    m_reservation->release();
    QTRY_COMPARE_WITH_TIMEOUT(released.count(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(owner(), QStringLiteral("fake"), busTimeoutMs);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    QSignalSpy lost(m_reservation.get(), &DeviceReservation::nameLost);
    m_reservation->acquire(reserveName(), cardId(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(fakeState().calls, 2u, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fakeState().lastReplyMs >= 0, busTimeoutMs);
    control(QStringLiteral("Replug"));
    QTRY_COMPARE_WITH_TIMEOUT(fakeState().calls, 3u, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(fakeState().lastReplyMs >= 0, busTimeoutMs);
    QVERIFY(!fakeState().lastAnswer);
    QCOMPARE(lost.count(), 0);
    QVERIFY(owner() != QLatin1StringView("fake"));
}

void ReservationTest::propertiesReadable()
{
    hold();
    const auto property = [](const QString &name) {
        QDBusMessage call = QDBusMessage::createMethodCall(service(), DeviceReservation::pathFor(reserveName()),
                                                           QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        call << DeviceReservation::interfaceName() << name;
        const QDBusMessage reply = QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
        return qvariant_cast<QDBusVariant>(reply.arguments().value(0)).variant();
    };
    const QVariant priority = property(QStringLiteral("Priority"));
    QCOMPARE(priority.metaType(), QMetaType::fromType<int>());
    QCOMPARE(priority.toInt(), 10);
    QCOMPARE(property(QStringLiteral("ApplicationName")).toString(), QStringLiteral("microdap"));
    QCOMPARE(property(QStringLiteral("ApplicationDeviceName")).toString(), QStringLiteral("hw:CARD=B"));
}

void ReservationTest::connectionLostOnBusDrop()
{
    QProcess daemon;
    daemon.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    daemon.start(QStringLiteral("dbus-daemon"), {QStringLiteral("--session"), QStringLiteral("--nofork"),
                                                 QStringLiteral("--print-address")});
    if (!daemon.waitForStarted(busTimeoutMs))
        QSKIP("no dbus-daemon");
    QByteArray address;
    QTRY_VERIFY_WITH_TIMEOUT((address += daemon.readAllStandardOutput()).contains('\n'), busTimeoutMs);
    qputenv("MICRODAP_RESERVE_BUS", address.trimmed());
    m_reservation = std::make_unique<DeviceReservation>(quick);
    QSignalSpy acquired(m_reservation.get(), &DeviceReservation::acquired);
    QSignalSpy dropped(m_reservation.get(), &DeviceReservation::connectionLost);
    m_reservation->acquire(reserveName(), cardId(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(acquired.count(), 1, busTimeoutMs);
    qunsetenv("MICRODAP_RESERVE_BUS");
    QVERIFY(!m_reservation->waitReleased(0));
    daemon.kill();
    QVERIFY(daemon.waitForFinished(busTimeoutMs));
    QTRY_COMPARE_WITH_TIMEOUT(dropped.count(), 1, busTimeoutMs);
    QVERIFY(m_reservation->waitReleased(0));
}

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIntValue("MICRODAP_RESERVE_CHILD") == 1)
        return child(argc, argv);
    QCoreApplication app(argc, argv);
    ReservationTest test;
    QTEST_SET_MAIN_SOURCE_PATH
    return QTest::qExec(&test, argc, argv);
}

#include "tst_reservation.moc"
