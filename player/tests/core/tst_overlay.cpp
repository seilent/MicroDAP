#include "DbusPad.h"
#include "Intercept.h"
#include "Keep.h"
#include "OverlayBridge.h"
#include "Player.h"
#include "PowerBridge.h"
#include "SleepInhibitor.h"
#include "State.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QScreen>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;
constexpr int fadeTimeoutMs = 2000;
constexpr int watchdogTimeoutMs = 8000;

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

QString login1ControlPath()
{
    return QStringLiteral("/net/seilent/MicroDap/FakeLogin1");
}

QString login1ControlInterface()
{
    return QStringLiteral("net.seilent.MicroDap.FakeLogin1");
}

bool writeScript(const QString &path, const QByteArray &body)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(body);
    file.close();
    return file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
}

QString readText(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

}

class OverlayTest : public QObject
{
    Q_OBJECT

public slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated);

private slots:
    void initTestCase();
    void cleanupTestCase();
    void hiddenAtStart();
    void readyBeforeWatchdog();
    void toggleShows();
    void watchdogShown();
    void hideWritesZeroBeforeFade();
    void showRecreatesWindow();
    void toggleDuringShowFade();
    void toggleDuringHideFade();
    void showDuringRelease();
    void showDuringShowFadeNoOp();
    void screenOffIgnored();
    void powerMenuIgnored();
    void switchPromptNotSupported();
    void introspection();
    void sleepInhibitorHeld();
    void prepareForSleepHides();
    void resumeTakesNewInhibitor();
    void prepareForSleepWhileHidden();
    void wakeKeepsReleased();
    void arbiterRestartWhileShown();
    void otherHolderBlocksPad();
    void lowerAtMap();
    void buriedDestroysWindow();
    void toggleOnBuriedRaises();
    void holderBackRecreates();
    void hideOnBuriedReleases();
    void showOnBuriedRaises();
    void arbiterRestartWhileBuried();
    void releaseFailureRetries();
    void hideSavesStack();
    void closePlayer();
    void closePlayerTimeout();
    void hideWithDestroyedWindowNoOp();

private:
    QDBusMessage control(const QString &method, const QList<QVariant> &arguments = {});
    QDBusMessage login1(const QString &method, const QList<QVariant> &arguments = {});
    bool held();
    QString holder();
    QDBusMessage arbiter(const QString &method, const QList<QVariant> &arguments = {});
    QDBusMessage otherCall(const QString &method);
    QString other();
    QStringList stack();
    uint changes();
    void showSettled();
    bool windowUp() const;
    bool startArbiter();
    void stopArbiter();
    void sleepAndResume();
    int inhibitCount();
    int openFds();
    int depth() const;
    bool startFake(QProcess &process, const QString &script, const QString &service);
    bool openNotifySocket();
    QByteArray readNotify(int timeoutMs);
    void drainNotify();
    bool sawChange(const QString &name, const QVariant &value) const;
    void settleHidden();

    QTemporaryDir m_dir;
    QProcess m_fake;
    QProcess m_arbiter;
    QProcess m_login1;
    QString m_stateFile;
    QString m_keepFile;
    QString m_closeLog;
    int m_notifyFd = -1;
    std::unique_ptr<Player> m_player;
    std::unique_ptr<State> m_state;
    std::unique_ptr<DbusPad> m_pad;
    std::unique_ptr<OverlayBridge> m_overlay;
    std::unique_ptr<PowerBridge> m_power;
    std::unique_ptr<QDBusInterface> m_remote;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    QPointer<QQuickWindow> m_window;
    QList<QVariantMap> m_changes;
    QStringList m_lowered;
};

void OverlayTest::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                      const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface == PowerBridge::interfaceName())
        m_changes.append(changed);
}

QDBusMessage OverlayTest::control(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(controlService(), controlPath(), controlInterface(),
                                                       method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

QDBusMessage OverlayTest::login1(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(SleepInhibitor::service(), login1ControlPath(),
                                                       login1ControlInterface(), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

QString OverlayTest::holder()
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

QDBusMessage OverlayTest::arbiter(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                       QStringLiteral("net.seilent.MicroDap.FakeArbiter"), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

QDBusMessage OverlayTest::otherCall(const QString &method)
{
    const QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                             Intercept::arbiterInterface(), method);
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("other"))
        .call(call, QDBus::Block, busTimeoutMs);
}

QString OverlayTest::other()
{
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("other")).baseService();
}

QStringList OverlayTest::stack()
{
    return arbiter(QStringLiteral("Stack")).arguments().value(0).toStringList();
}

uint OverlayTest::changes()
{
    return arbiter(QStringLiteral("Changes")).arguments().value(0).toUInt();
}

void OverlayTest::showSettled()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QVERIFY(windowUp());
}

bool OverlayTest::windowUp() const
{
    return m_window && m_window->handle() && m_window->isVisible();
}

bool OverlayTest::held()
{
    return holder() == QDBusConnection::sessionBus().baseService();
}

bool OverlayTest::startArbiter()
{
    return startFake(m_arbiter, QStringLiteral(MICRODAP_FAKE_ARBITER), Intercept::arbiterService());
}

void OverlayTest::stopArbiter()
{
    if (m_arbiter.state() != QProcess::Running)
        return;
    const QDBusMessage call = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                             QStringLiteral("net.seilent.MicroDap.FakeArbiter"),
                                                             QStringLiteral("Quit"));
    QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
    if (!m_arbiter.waitForFinished(busTimeoutMs)) {
        m_arbiter.terminate();
        m_arbiter.waitForFinished(busTimeoutMs);
    }
}

void OverlayTest::sleepAndResume()
{
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->sleeping(), busTimeoutMs);
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->sleeping(), busTimeoutMs);
}

int OverlayTest::inhibitCount()
{
    const QDBusMessage reply = login1(QStringLiteral("InhibitCount"));
    if (reply.type() == QDBusMessage::ErrorMessage)
        return -1;
    return reply.arguments().value(0).toInt();
}

int OverlayTest::openFds()
{
    const QDBusMessage reply = login1(QStringLiteral("OpenFds"));
    if (reply.type() == QDBusMessage::ErrorMessage)
        return -1;
    return reply.arguments().value(0).toInt();
}

int OverlayTest::depth() const
{
    if (!m_window)
        return -1;
    QObject *pages = qvariant_cast<QObject *>(m_window->property("pageStack"));
    return pages ? pages->property("depth").toInt() : -1;
}

bool OverlayTest::startFake(QProcess &process, const QString &script, const QString &service)
{
    process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    process.start(QStringLiteral("python3"), {script});
    if (!process.waitForStarted(busTimeoutMs))
        return false;
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < busTimeoutMs) {
        if (iface->isServiceRegistered(service))
            return true;
        if (process.state() != QProcess::Running)
            return false;
        QTest::qWait(50);
    }
    return false;
}

bool OverlayTest::openNotifySocket()
{
    const QByteArray path = (m_dir.path() + QStringLiteral("/notify")).toUtf8();
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path)))
        return false;
    std::memcpy(address.sun_path, path.constData(), static_cast<size_t>(path.size()));
    m_notifyFd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (m_notifyFd < 0)
        return false;
    if (bind(m_notifyFd, reinterpret_cast<const sockaddr *>(&address), sizeof(address)) != 0)
        return false;
    qputenv("NOTIFY_SOCKET", path);
    return true;
}

QByteArray OverlayTest::readNotify(int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    char buffer[256];
    while (timer.elapsed() < timeoutMs) {
        const ssize_t got = recv(m_notifyFd, buffer, sizeof(buffer), 0);
        if (got > 0)
            return QByteArray(buffer, static_cast<qsizetype>(got));
        QTest::qWait(50);
    }
    return {};
}

void OverlayTest::drainNotify()
{
    char buffer[256];
    while (recv(m_notifyFd, buffer, sizeof(buffer), 0) > 0) {
    }
}

bool OverlayTest::sawChange(const QString &name, const QVariant &value) const
{
    for (const QVariantMap &change : m_changes) {
        if (change.contains(name) && change.value(name) == value)
            return true;
    }
    return false;
}

void OverlayTest::settleHidden()
{
    if (m_overlay->shown())
        m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(held(), false, busTimeoutMs);
}

void OverlayTest::initTestCase()
{
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.isConnected())
        QSKIP("no session bus, run under dbus-run-session");
    if (!startFake(m_fake, QStringLiteral(MICRODAP_FAKE_INPUTPLUMBER), controlService()))
        QSKIP("fake InputPlumber did not start (python3 with dbus and gi needed)");
    if (!startFake(m_login1, QStringLiteral(MICRODAP_FAKE_LOGIN1), SleepInhibitor::service()))
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    QVERIFY(startArbiter());
    QVERIFY(m_dir.isValid());
    QVERIFY(openNotifySocket());
    m_stateFile = m_dir.path() + QStringLiteral("/config/microdap/state.json");
    m_keepFile = m_dir.path() + QStringLiteral("/config/microdap/keep");
    qputenv("MICRODAP_KEEP_FILE", m_keepFile.toUtf8());
    qputenv("MICRODAP_BOOT_ID_FILE", (m_dir.path() + QStringLiteral("/boot_id")).toUtf8());
    QFile bootId(m_dir.path() + QStringLiteral("/boot_id"));
    QVERIFY(bootId.open(QIODevice::WriteOnly));
    bootId.write("0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10\n");
    bootId.close();
    m_closeLog = m_dir.path() + QStringLiteral("/close.log");
    const QByteArray logLine = QByteArrayLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> '")
                               + m_closeLog.toUtf8() + QByteArrayLiteral("'\n");
    QVERIFY(writeScript(m_dir.path() + QStringLiteral("/microdap-bt"), QByteArrayLiteral("#!/bin/sh\n") + logLine));
    QVERIFY(writeScript(m_dir.path() + QStringLiteral("/slow-bt"),
                        QByteArrayLiteral("#!/bin/sh\n") + logLine + QByteArrayLiteral("exec sleep 10\n")));
    qputenv("MICRODAP_BT_BIN", (m_dir.path() + QStringLiteral("/microdap-bt")).toUtf8());

    qputenv("MICRODAP_INPUTPLUMBER_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    qputenv("MICRODAP_LOGIN1_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QDBusConnection bus = Intercept::bus();
    QVERIFY2(bus.isConnected(), qPrintable(bus.lastError().message()));

    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("MICRODAP_AUDIO_SINK"))
        qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));
    qRegisterMetaType<PadSource::Action>();
    m_player = std::make_unique<Player>();
    QVERIFY(m_player->positionPollEnabled());
    m_state = std::make_unique<State>(m_player.get(), m_stateFile);
    m_pad = std::make_unique<DbusPad>(session, bus);
    m_overlay = std::make_unique<OverlayBridge>(m_pad.get(), m_player.get());
    m_overlay->setState(m_state.get());
    m_power = std::make_unique<PowerBridge>(m_player.get(), session);
    QVERIFY(m_power->isRegistered());
    m_power->setOverlay(m_overlay.get());
    QVERIFY(m_power->overlayMode());
    QCOMPARE(m_power->sleep(), nullptr);

    QVERIFY(session.connect(PowerBridge::serviceName(), PowerBridge::objectPath(),
                            QStringLiteral("org.freedesktop.DBus.Properties"),
                            QStringLiteral("PropertiesChanged"), this,
                            SLOT(onPropertiesChanged(QString,QVariantMap,QStringList))));
    m_remote = std::make_unique<QDBusInterface>(PowerBridge::serviceName(), PowerBridge::objectPath(),
                                                PowerBridge::interfaceName(), session);
    QVERIFY2(m_remote->isValid(), qPrintable(m_remote->lastError().message()));
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->connected(), busTimeoutMs);

    QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(QStringLiteral("Player"), m_player.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("Pad"), m_pad.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("PowerBridge"), m_power.get());
    m_engine->rootContext()->setContextProperty(QStringLiteral("OverlayBridge"), m_overlay.get());
    m_engine->loadFromModule("MicroDap", "Main");
    QVERIFY(!m_engine->rootObjects().isEmpty());
    m_window = qobject_cast<QQuickWindow *>(m_engine->rootObjects().constFirst());
    QVERIFY(m_window);
    QVERIFY(!m_window->handle());
    QVERIFY(!m_window->isVisible());
    QVERIFY(!m_overlay->ready());
    m_overlay->setWindow(m_window, [](QWindow *target) {
        target->create();
        target->setWindowState(Qt::WindowFullScreen);
    });
    QVERIFY(m_overlay->ready());
    QCOMPARE(m_overlay->window(), static_cast<QWindow *>(m_window.data()));
    m_overlay->setLower([this](QWindow *target) {
        const QString name = target == m_window.data() ? QStringLiteral("page") : QStringLiteral("toast");
        m_lowered << (target->isVisible() ? name : name + QStringLiteral(" unmapped"));
    });
}

void OverlayTest::cleanupTestCase()
{
    m_engine.reset();
    m_power.reset();
    m_overlay.reset();
    m_pad.reset();
    stopArbiter();
    m_state.reset();
    m_player.reset();
    if (m_notifyFd >= 0)
        close(m_notifyFd);
    if (m_fake.state() == QProcess::Running) {
        control(QStringLiteral("Quit"));
        if (!m_fake.waitForFinished(busTimeoutMs)) {
            m_fake.terminate();
            m_fake.waitForFinished(busTimeoutMs);
        }
    }
    if (m_login1.state() == QProcess::Running) {
        login1(QStringLiteral("Quit"));
        if (!m_login1.waitForFinished(busTimeoutMs)) {
            m_login1.terminate();
            m_login1.waitForFinished(busTimeoutMs);
        }
    }
}

void OverlayTest::hiddenAtStart()
{
    QVERIFY(!m_overlay->shown());
    QVERIFY(!m_overlay->mapped());
    QVERIFY(!m_overlay->fading());
    QCOMPARE(m_overlay->opacity(), 0.0);
    QCOMPARE(m_overlay->mapCount(), 0);
    QVERIFY(!m_player->positionPollEnabled());
    QVERIFY(m_overlay->watchdogActive());
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QCOMPARE(held(), false);
    QVERIFY(m_window->handle());
    QVERIFY(!m_window->isVisible());
    QCOMPARE(depth(), 1);
}

void OverlayTest::readyBeforeWatchdog()
{
    QVERIFY(m_overlay->ready());
    QCOMPARE(readNotify(watchdogTimeoutMs), QByteArrayLiteral("READY=1\n"));
    QCOMPARE(readNotify(watchdogTimeoutMs), QByteArrayLiteral("WATCHDOG=1\n"));
    QCOMPARE(readNotify(watchdogTimeoutMs), QByteArrayLiteral("WATCHDOG=1\n"));
}

void OverlayTest::toggleShows()
{
    m_changes.clear();
    bool heldAtShown = true;
    bool fadingAtShown = true;
    const QMetaObject::Connection probe = connect(m_overlay.get(), &OverlayBridge::shownChanged, this,
                                                 [this, &heldAtShown, &fadingAtShown] {
        heldAtShown = held();
        fadingAtShown = m_overlay->fading();
    });
    QSignalSpy mappedSpy(m_overlay.get(), &OverlayBridge::mappedChanged);

    QDBusReply<void> reply = m_remote->call(QStringLiteral("ToggleOverlay"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->shown(), busTimeoutMs);
    disconnect(probe);
    QVERIFY(!heldAtShown);
    QVERIFY(!fadingAtShown);
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());
    QVERIFY(m_window->isVisible());
    QCOMPARE(mappedSpy.count(), 1);
    QCOMPARE(m_overlay->mapCount(), 1);
    QVERIFY(m_overlay->fading());
    QVERIFY(m_player->positionPollEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("OverlayShown"), true), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QCOMPARE(m_remote->property("OverlayShown").toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->fading(), fadeTimeoutMs);
    QVERIFY(m_overlay->mapped());
    QCOMPARE(mappedSpy.count(), 1);
}

void OverlayTest::watchdogShown()
{
    QVERIFY(m_overlay->shown());
    drainNotify();
    QCOMPARE(readNotify(watchdogTimeoutMs), QByteArrayLiteral("WATCHDOG=1\n"));
}

void OverlayTest::hideWritesZeroBeforeFade()
{
    QVERIFY(m_overlay->shown());
    m_changes.clear();
    bool fadingAtShown = true;
    int heldAtFirstFade = -1;
    bool padShownAtFirstFade = true;
    const QMetaObject::Connection shownProbe = connect(m_overlay.get(), &OverlayBridge::shownChanged, this,
                                                      [this, &fadingAtShown] { fadingAtShown = m_overlay->fading(); });
    const QMetaObject::Connection fadeProbe = connect(m_overlay.get(), &OverlayBridge::opacityChanged, this,
                                                     [this, &heldAtFirstFade, &padShownAtFirstFade] {
        if (heldAtFirstFade < 0) {
            heldAtFirstFade = held() ? 1 : 0;
            padShownAtFirstFade = m_pad->shown();
        }
    });
    QSignalSpy mappedSpy(m_overlay.get(), &OverlayBridge::mappedChanged);

    QDBusReply<void> reply = m_remote->call(QStringLiteral("HideOverlay"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->shown(), busTimeoutMs);
    QVERIFY(!fadingAtShown);
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_overlay->fading());
    QVERIFY(m_window->handle());
    QTRY_VERIFY_WITH_TIMEOUT(heldAtFirstFade >= 0, fadeTimeoutMs);
    QVERIFY(!padShownAtFirstFade);
    QTRY_COMPARE_WITH_TIMEOUT(held(), false, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    disconnect(shownProbe);
    disconnect(fadeProbe);
    QCOMPARE(m_overlay->opacity(), 0.0);
    QCOMPARE(mappedSpy.count(), 1);
    QVERIFY(!m_window->handle());
    QVERIFY(!m_window->isVisible());
    QCOMPARE(m_engine->rootObjects().constFirst(), static_cast<QObject *>(m_window.data()));
    QCOMPARE(depth(), 1);
    QVERIFY(!m_player->positionPollEnabled());
    QCOMPARE(held(), false);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("OverlayShown"), false), busTimeoutMs);
}

void OverlayTest::showRecreatesWindow()
{
    settleHidden();
    QVERIFY(!m_window->handle());
    const QSize screen = QGuiApplication::primaryScreen()->size();
    const int maps = m_overlay->mapCount();

    m_overlay->show();
    QVERIFY(m_window->handle());
    QVERIFY(m_window->isVisible());
    QCOMPARE(m_window->windowState(), Qt::WindowFullScreen);
    QCOMPARE(m_window->size(), screen);
    QCOMPARE(m_overlay->mapCount(), maps + 1);
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QVERIFY(m_window->handle());
    QVERIFY(m_window->isVisible());
    QCOMPARE(m_window->windowState(), Qt::WindowFullScreen);
    QCOMPARE(m_window->size(), screen);
    QCOMPARE(depth(), 1);

    m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    QVERIFY(!m_window->handle());
    QVERIFY(!m_window->isVisible());
    QCOMPARE(depth(), 1);
}

void OverlayTest::toggleDuringShowFade()
{
    settleHidden();
    const int maps = m_overlay->mapCount();
    m_overlay->show();
    QVERIFY(m_overlay->shown());
    QCOMPARE(m_overlay->mapCount(), maps + 1);
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->opacity() > 0.0 && m_overlay->opacity() < 1.0, fadeTimeoutMs);
    QVERIFY(m_overlay->fading());

    m_overlay->toggle();
    QVERIFY(!m_overlay->shown());
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_overlay->fading());
    QTRY_COMPARE_WITH_TIMEOUT(held(), false, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    QCOMPARE(m_overlay->opacity(), 0.0);
    QCOMPARE(m_overlay->mapCount(), maps + 1);
    QVERIFY(!m_player->positionPollEnabled());
}

void OverlayTest::toggleDuringHideFade()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    const int maps = m_overlay->mapCount();
    QSignalSpy mappedSpy(m_overlay.get(), &OverlayBridge::mappedChanged);

    m_overlay->hide();
    QVERIFY(!m_overlay->shown());
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->opacity() > 0.0 && m_overlay->opacity() < 1.0, fadeTimeoutMs);
    QVERIFY(m_overlay->fading());
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());

    m_overlay->toggle();
    QVERIFY(m_overlay->shown());
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());
    QVERIFY(m_window->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());
    QCOMPARE(m_overlay->mapCount(), maps);
    QCOMPARE(mappedSpy.count(), 0);
    QVERIFY(m_player->positionPollEnabled());
}

void OverlayTest::showDuringRelease()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->fading(), fadeTimeoutMs);
    const int maps = m_overlay->mapCount();
    auto armed = std::make_shared<bool>(true);
    auto releasingAtProbe = std::make_shared<bool>(false);
    const QMetaObject::Connection probe = connect(m_overlay.get(), &OverlayBridge::fadingChanged, this,
                                                 [this, armed, releasingAtProbe] {
        if (!*armed || m_overlay->fading() || m_overlay->shown())
            return;
        *armed = false;
        *releasingAtProbe = m_overlay->releasing();
        m_overlay->show();
    }, Qt::QueuedConnection);
    QSignalSpy mappedSpy(m_overlay.get(), &OverlayBridge::mappedChanged);

    m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!*armed, fadeTimeoutMs);
    disconnect(probe);
    QVERIFY(*releasingAtProbe);
    QVERIFY(m_overlay->shown());
    QVERIFY(!m_overlay->releasing());
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());
    QVERIFY(m_window->isVisible());
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QTest::qWait(OverlayBridge::ReleaseFrameMs * 2);
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_window->handle());
    QCOMPARE(mappedSpy.count(), 0);
    QCOMPARE(m_overlay->mapCount(), maps);
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
}

void OverlayTest::showDuringShowFadeNoOp()
{
    settleHidden();
    QSignalSpy shownSpy(m_overlay.get(), &OverlayBridge::shownChanged);
    const int maps = m_overlay->mapCount();
    m_overlay->show();
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->opacity() > 0.0 && m_overlay->opacity() < 1.0, fadeTimeoutMs);
    QVERIFY(m_overlay->fading());
    const qreal before = m_overlay->opacity();
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowOverlay"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    m_overlay->show();
    QCOMPARE(shownSpy.count(), 1);
    QCOMPARE(m_overlay->mapCount(), maps + 1);
    QVERIFY(m_overlay->opacity() >= before);
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QCOMPARE(shownSpy.count(), 1);
}

void OverlayTest::screenOffIgnored()
{
    QSignalSpy spy(m_power.get(), &PowerBridge::screenOffChanged);
    QTest::ignoreMessage(QtInfoMsg, "microdap power: SetScreenOff ignored in overlay mode true");
    QDBusReply<void> reply = m_remote->call(QStringLiteral("SetScreenOff"), true);
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCoreApplication::processEvents();
    QVERIFY(!m_power->screenOff());
    QCOMPARE(spy.count(), 0);
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
    QVERIFY(!m_pad->screenOff());
}

void OverlayTest::powerMenuIgnored()
{
    QSignalSpy spy(m_power.get(), &PowerBridge::showPowerMenu);
    QTest::ignoreMessage(QtInfoMsg, "microdap power: ShowPowerMenu ignored in overlay mode");
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowPowerMenu"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 0);
}

void OverlayTest::switchPromptNotSupported()
{
    QSignalSpy spy(m_power.get(), &PowerBridge::showSwitchPrompt);
    QTest::ignoreMessage(QtInfoMsg, "microdap power: ShowSwitchPrompt");
    QTest::ignoreMessage(QtInfoMsg, "microdap power: ShowSwitchPrompt needs dap mode");
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowSwitchPrompt"));
    QVERIFY(!reply.isValid());
    QCOMPARE(reply.error().name(), PowerBridge::notSupportedError());
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 0);
}

void OverlayTest::introspection()
{
    QDBusInterface introspectable(PowerBridge::serviceName(), PowerBridge::objectPath(),
                                  QStringLiteral("org.freedesktop.DBus.Introspectable"),
                                  QDBusConnection::sessionBus());
    QDBusReply<QString> xml = introspectable.call(QStringLiteral("Introspect"));
    QVERIFY2(xml.isValid(), qPrintable(xml.error().message()));
    QVERIFY(xml.value().contains(QStringLiteral("ShowOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("HideOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("ToggleOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("ShowSwitchPrompt")));
    QVERIFY(xml.value().contains(QStringLiteral("OverlayShown")));
    settleHidden();
}

void OverlayTest::sleepInhibitorHeld()
{
    settleHidden();
    QVERIFY(m_overlay->sleep() != nullptr);
    QVERIFY(m_overlay->sleep()->subscribed());
    QVERIFY(m_overlay->sleep()->held());
    QCOMPARE(m_overlay->sleep()->takeCount(), 1);
    QCOMPARE(inhibitCount(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(openFds(), 1, busTimeoutMs);
    const QDBusMessage reply = login1(QStringLiteral("LastInhibit"));
    QCOMPARE(reply.type(), QDBusMessage::ReplyMessage);
    QCOMPARE(reply.arguments().value(0).toStringList(),
             QStringList({QStringLiteral("sleep"), QStringLiteral("microdap"), QStringLiteral("hide overlay"),
                          QStringLiteral("delay")}));
    QVERIFY(!m_overlay->sleeping());
}

void OverlayTest::prepareForSleepHides()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    m_changes.clear();
    bool heldAtHidden = false;
    const QMetaObject::Connection probe = connect(m_overlay.get(), &OverlayBridge::shownChanged, this,
                                                 [this, &heldAtHidden] { heldAtHidden = m_overlay->sleep()->held(); });

    const QDBusMessage sent = login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QCOMPARE(sent.type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->shown(), busTimeoutMs);
    disconnect(probe);
    QVERIFY(heldAtHidden);
    QVERIFY(m_overlay->sleeping());
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QTRY_COMPARE_WITH_TIMEOUT(held(), false, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->sleep()->held(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(openFds(), 0, busTimeoutMs);
    QCOMPARE(inhibitCount(), 1);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("OverlayShown"), false), busTimeoutMs);
}

void OverlayTest::resumeTakesNewInhibitor()
{
    QVERIFY(m_overlay->sleeping());
    QVERIFY(!m_overlay->sleep()->held());
    const QDBusMessage sent = login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QCOMPARE(sent.type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->sleeping(), busTimeoutMs);
    QVERIFY(m_overlay->sleep()->held());
    QCOMPARE(m_overlay->sleep()->takeCount(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(inhibitCount(), 2, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(openFds(), 1, busTimeoutMs);
    QCOMPARE(held(), false);
    QVERIFY(!m_overlay->shown());
    QVERIFY(!m_overlay->mapped());
}

void OverlayTest::prepareForSleepWhileHidden()
{
    settleHidden();
    QVERIFY(m_overlay->sleep()->held());
    const int takes = m_overlay->sleep()->takeCount();
    QSignalSpy shownSpy(m_overlay.get(), &OverlayBridge::shownChanged);
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->sleeping(), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->sleep()->held(), busTimeoutMs);
    QCOMPARE(shownSpy.count(), 0);
    QCOMPARE(held(), false);
    QVERIFY(QFile::exists(m_stateFile));
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->sleeping(), busTimeoutMs);
    QVERIFY(m_overlay->sleep()->held());
    QCOMPARE(m_overlay->sleep()->takeCount(), takes + 1);
}

void OverlayTest::wakeKeepsReleased()
{
    settleHidden();
    sleepAndResume();
    QTest::qWait(200);
    QCOMPARE(holder(), QString());
    QVERIFY(!m_overlay->shown());
}

void OverlayTest::arbiterRestartWhileShown()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    stopArbiter();
    QTRY_VERIFY_WITH_TIMEOUT(!m_pad->holding(), busTimeoutMs);
    QVERIFY(m_overlay->shown());
    QVERIFY(startArbiter());
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QCOMPARE(held(), true);
    settleHidden();
    QCOMPARE(holder(), QString());
}

void OverlayTest::otherHolderBlocksPad()
{
    settleHidden();
    m_overlay->show();
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("other"));
    auto disconnectOther = qScopeGuard([] { QDBusConnection::disconnectFromBus(QStringLiteral("other")); });
    QVERIFY(other.isConnected());
    const QDBusMessage acquire = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                                Intercept::arbiterInterface(),
                                                                QStringLiteral("Acquire"));
    QCOMPARE(other.call(acquire, QDBus::Block, busTimeoutMs).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_pad->holding(), busTimeoutMs);
    QVERIFY(m_overlay->shown());
    QCOMPARE(holder(), other.baseService());
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->handle(), busTimeoutMs);
    const QDBusMessage release = QDBusMessage::createMethodCall(Intercept::arbiterService(), Intercept::arbiterPath(),
                                                                Intercept::arbiterInterface(),
                                                                QStringLiteral("Release"));
    QCOMPARE(other.call(release, QDBus::Block, busTimeoutMs).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_pad->holding(), busTimeoutMs);
    QVERIFY(windowUp());
    settleHidden();
}

void OverlayTest::lowerAtMap()
{
    settleHidden();
    m_lowered.clear();
    m_overlay->show();
    QCOMPARE(m_lowered, QStringList{QStringLiteral("page")});
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QVERIFY(!m_overlay->prepareToast());
    settleHidden();
    QWindow toast;
    toast.resize(320, 40);
    m_overlay->setToastWindow(&toast);
    m_lowered.clear();
    QVERIFY(m_overlay->prepareToast());
    QVERIFY(m_lowered.isEmpty());
    toast.setVisible(true);
    QCOMPARE(m_lowered, QStringList{QStringLiteral("toast")});
    m_overlay->releaseToast();
    QVERIFY(!toast.handle());
    m_overlay->setToastWindow(nullptr);
}

void OverlayTest::buriedDestroysWindow()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->handle(), busTimeoutMs);
    QVERIFY(m_overlay->shown());
    QVERIFY(m_overlay->buried());
    QVERIFY(!m_overlay->mapped());
    QCOMPARE(m_overlay->opacity(), 1.0);
    QVERIFY(!m_pad->holding());
    QCOMPARE(m_remote->property("OverlayShown").toBool(), true);
    QVERIFY(!m_overlay->prepareToast());
    QCOMPARE(stack(), (QStringList{QDBusConnection::sessionBus().baseService(), other()}));
    QCOMPARE(otherCall(QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
    settleHidden();
}

void OverlayTest::toggleOnBuriedRaises()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->buried(), busTimeoutMs);
    const uint before = changes();
    const int maps = m_overlay->mapCount();
    QSignalSpy holders(m_pad->intercept(), &Intercept::holderChanged);
    m_lowered.clear();
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ToggleOverlay"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(windowUp(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QTest::qWait(100);
    QCOMPARE(changes(), before + 1);
    QCOMPARE(holders.count(), 1);
    QCOMPARE(stack(), (QStringList{other(), QDBusConnection::sessionBus().baseService()}));
    QCOMPARE(m_lowered, QStringList{QStringLiteral("page")});
    QCOMPARE(m_overlay->mapCount(), maps + 1);
    QVERIFY(m_overlay->shown());
    QVERIFY(!m_overlay->buried());
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_pad->holding());
    settleHidden();
    QCOMPARE(stack(), QStringList{other()});
    QCOMPARE(otherCall(QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
    QTRY_COMPARE_WITH_TIMEOUT(holder(), QString(), busTimeoutMs);
}

void OverlayTest::holderBackRecreates()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->handle(), busTimeoutMs);
    m_lowered.clear();
    QCOMPARE(otherCall(QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(windowUp(), busTimeoutMs);
    QVERIFY(m_overlay->mapped());
    QVERIFY(m_pad->holding());
    QCOMPARE(m_lowered, QStringList{QStringLiteral("page")});
    settleHidden();
}

void OverlayTest::hideOnBuriedReleases()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->buried(), busTimeoutMs);
    QFile::remove(m_stateFile);
    m_overlay->hide();
    QVERIFY(!m_overlay->shown());
    QVERIFY(!m_overlay->mapped());
    QCOMPARE(m_overlay->opacity(), 0.0);
    QVERIFY(!m_player->positionPollEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(stack(), QStringList{other()}, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_stateFile), busTimeoutMs);
    QCOMPARE(otherCall(QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
    QTest::qWait(100);
    QVERIFY(!m_window->handle());
    QVERIFY(!m_overlay->shown());
}

void OverlayTest::showOnBuriedRaises()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->buried(), busTimeoutMs);
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowOverlay"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(windowUp(), busTimeoutMs);
    settleHidden();
    QCOMPARE(otherCall(QStringLiteral("Release")).type(), QDBusMessage::ReplyMessage);
}

void OverlayTest::arbiterRestartWhileBuried()
{
    showSettled();
    QCOMPARE(otherCall(QStringLiteral("Acquire")).type(), QDBusMessage::ReplyMessage);
    QTRY_VERIFY_WITH_TIMEOUT(m_overlay->buried(), busTimeoutMs);
    stopArbiter();
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->shown(), busTimeoutMs);
    QVERIFY(!m_window->handle());
    QVERIFY(startArbiter());
    QTest::qWait(200);
    QCOMPARE(stack(), QStringList());
    QVERIFY(!m_overlay->shown());
    QVERIFY(!m_window->handle());
    m_overlay->toggle();
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QVERIFY(windowUp());
    settleHidden();
}

void OverlayTest::releaseFailureRetries()
{
    showSettled();
    QCOMPARE(arbiter(QStringLiteral("RefuseRelease"), {true}).type(), QDBusMessage::ReplyMessage);
    m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!m_window->handle(), fadeTimeoutMs);
    QTest::qWait(300);
    QCOMPARE(held(), true);
    QCOMPARE(arbiter(QStringLiteral("RefuseRelease"), {false}).type(), QDBusMessage::ReplyMessage);
    QTRY_COMPARE_WITH_TIMEOUT(holder(), QString(), Intercept::ReleaseRetryMs + busTimeoutMs);
    QVERIFY(!m_overlay->shown());
}

void OverlayTest::hideSavesStack()
{
    settleHidden();
    QFile::remove(m_stateFile);
    m_state->setStack({QVariantMap{{QStringLiteral("name"), QStringLiteral("home")}, {QStringLiteral("id"), -1}},
                       QVariantMap{{QStringLiteral("name"), QStringLiteral("album")}, {QStringLiteral("id"), 12}},
                       QVariantMap{{QStringLiteral("name"), QString()}, {QStringLiteral("id"), 3}}});
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(m_overlay->opacity(), 1.0, fadeTimeoutMs);
    QVERIFY(!QFile::exists(m_stateFile));
    m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_stateFile), busTimeoutMs);
    QJsonObject object;
    QVERIFY(State::read(m_stateFile, object));
    QCOMPARE(object.value(QStringLiteral("handoff")).toBool(true), false);
    const QJsonArray stack = object.value(QStringLiteral("stack")).toArray();
    QCOMPARE(stack.size(), 2);
    QCOMPARE(stack.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("home"));
    QCOMPARE(stack.at(1).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("album"));
    QCOMPARE(stack.at(1).toObject().value(QStringLiteral("id")).toInt(), 12);
    QCOMPARE(State::savedStack(m_stateFile).size(), 2);
    QCOMPARE(m_state->stack().size(), 2);
}

void OverlayTest::closePlayer()
{
    settleHidden();
    m_overlay->show();
    QTRY_COMPARE_WITH_TIMEOUT(held(), true, busTimeoutMs);
    QVERIFY(Keep::write());
    QVERIFY(QFile::exists(m_keepFile));
    QVERIFY(m_state->save(true));
    QJsonObject before;
    QVERIFY(State::read(m_stateFile, before));
    QCOMPARE(before.value(QStringLiteral("handoff")).toBool(false), true);
    QFile::remove(m_closeLog);
    QSignalSpy closed(m_overlay.get(), &OverlayBridge::closed);

    m_overlay->closePlayer();
    QVERIFY(!m_overlay->shown());
    QTRY_COMPARE_WITH_TIMEOUT(held(), false, busTimeoutMs);
    QVERIFY(!QFile::exists(m_keepFile));
    QJsonObject after;
    QVERIFY(State::read(m_stateFile, after));
    QCOMPARE(after.value(QStringLiteral("handoff")).toBool(true), false);
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, OverlayBridge::CloseTimeoutMs + busTimeoutMs);
    QCOMPARE(readText(m_closeLog), QStringLiteral("microdap-bt --disconnect-sources\n"));
    m_overlay->closePlayer();
    QCoreApplication::processEvents();
    QCOMPARE(closed.count(), 1);
    QCOMPARE(readText(m_closeLog), QStringLiteral("microdap-bt --disconnect-sources\n"));
    m_overlay->hide();
    QTRY_VERIFY_WITH_TIMEOUT(!m_overlay->mapped(), fadeTimeoutMs);
}

void OverlayTest::closePlayerTimeout()
{
    QFile::remove(m_closeLog);
    qputenv("MICRODAP_BT_BIN", (m_dir.path() + QStringLiteral("/slow-bt")).toUtf8());
    OverlayBridge bridge(nullptr, nullptr);
    QSignalSpy closed(&bridge, &OverlayBridge::closed);
    QElapsedTimer timer;
    timer.start();
    bridge.closePlayer();
    QTRY_COMPARE_WITH_TIMEOUT(closed.count(), 1, OverlayBridge::CloseTimeoutMs + busTimeoutMs);
    const qint64 elapsed = timer.elapsed();
    qputenv("MICRODAP_BT_BIN", (m_dir.path() + QStringLiteral("/microdap-bt")).toUtf8());
    QVERIFY2(elapsed >= OverlayBridge::CloseTimeoutMs - 100, qPrintable(QString::number(elapsed)));
    QVERIFY2(elapsed < 3000, qPrintable(QString::number(elapsed)));
    QCOMPARE(readText(m_closeLog), QStringLiteral("slow-bt --disconnect-sources\n"));
}

void OverlayTest::hideWithDestroyedWindowNoOp()
{
    settleHidden();
    QVERIFY(!m_window->handle());
    QSignalSpy mappedSpy(m_overlay.get(), &OverlayBridge::mappedChanged);
    QSignalSpy shownSpy(m_overlay.get(), &OverlayBridge::shownChanged);

    m_overlay->hide();
    QCoreApplication::processEvents();
    QVERIFY(!m_overlay->shown());
    QVERIFY(!m_window->handle());
    QCOMPARE(mappedSpy.count(), 0);
    QCOMPARE(shownSpy.count(), 0);
    QCOMPARE(depth(), 1);

    m_engine.reset();
    QTRY_VERIFY_WITH_TIMEOUT(m_window.isNull(), fadeTimeoutMs);
    QVERIFY(!m_overlay->window());
    m_overlay->hide();
    QCoreApplication::processEvents();
    QVERIFY(!m_overlay->shown());
    QCOMPARE(mappedSpy.count(), 0);
}

QTEST_MAIN(OverlayTest)
#include "tst_overlay.moc"
