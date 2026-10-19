#include "AlsaVolume.h"
#include "FakeService.h"
#include "Keep.h"
#include "OutputFakes.h"
#include "OutputMode.h"
#include "OverlayBridge.h"
#include "Player.h"
#include "PowerBridge.h"
#include "Settings.h"
#include "SleepInhibitor.h"
#include "State.h"
#include "StatusModel.h"
#include "WirePlumberRestart.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

#include <gst/gst.h>

#include <memory>

using namespace microdap;

namespace {

constexpr int busTimeoutMs = 5000;

bool writeFile(const QString &path, const QString &text, bool executable = false)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    file.close();
    if (!executable)
        return true;
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return QString::fromUtf8(file.readAll());
}

QVariant savedValue(const QString &path, const QString &key)
{
    QJsonObject object;
    if (!State::read(path, object))
        return {};
    return object.value(key).toVariant();
}

QString switchLine(const QString &session = QStringLiteral("steam"))
{
    return QStringLiteral("systemctl start --no-block microos-session-switch@") + session + QStringLiteral(".service\n");
}

const QString fiioSink = QStringLiteral("alsa_output.usb-FIIO_SNOWSKY_TINY_B_000000000000-01.analog-stereo");

QStringList *capturedMessages = nullptr;
QtMessageHandler previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &text)
{
    if (capturedMessages)
        capturedMessages->append(text);
    if (previousHandler)
        previousHandler(type, context, text);
}

int countOf(const QStringList &log, const QString &prefix)
{
    int n = 0;
    for (const QString &line : log) {
        if (line.startsWith(prefix))
            ++n;
    }
    return n;
}

int indexOf(const QStringList &log, const QString &prefix)
{
    for (int i = 0; i < log.size(); ++i) {
        if (log.at(i).startsWith(prefix))
            return i;
    }
    return -1;
}

OutputMode::Bounds rigBounds()
{
    OutputMode::Bounds b;
    b.pcmFreeMs = 150;
    b.pcmPollMs = 5;
    b.acquireMs = 400;
    b.parkMs = 400;
    b.netFirstMs = 120;
    b.netSecondMs = 150;
    b.switchMs = 100;
    b.pcmWatchMs = 250;
    b.resolveMs = 150;
    b.readyMs = 150;
    b.quitMs = 50;
    b.sleepReleaseMs = 120;
    b.pacingMs = 10;
    b.storeDelayMs = 30;
    return b;
}

class OutputRig
{
public:
    explicit OutputRig(bool withDacEq = false, bool withRestart = false)
        : mixer(new FakeMixer(&log))
        , alsa(std::unique_ptr<MixerBackend>(mixer), rigBounds().pacingMs)
    {
        dac.card.cardIndex = 7;
        dac.card.alsaCard = 1;
        dac.card.reserveName = QStringLiteral("Audio1");
        dac.volumeDb = -20.0;
        OutputMode::Ports ports;
        ports.player = &player;
        ports.reservation = &reservation;
        ports.output = &output;
        ports.dac = &dac;
        ports.volume = &volume;
        ports.mixer = &alsa;
        if (withDacEq)
            ports.dacEq = &dacEq;
        if (withRestart)
            ports.wireplumber = &restart;
        mode = std::make_unique<OutputMode>(rigBounds(), ports);
        player.policy = mode.get();
        player.onStatus = [this] { mode->onStatusChanged(); };
        reservation.onAcquire = [this](quint64 seq) {
            QTimer::singleShot(0, &context, [this, seq] { mode->onAcquired(seq); });
        };
        reservation.onRelease = [this] { QTimer::singleShot(0, &context, [this] { mode->onReleased(); }); };
        output.onRefresh = [this](quint64 read) { mode->onSinksRead(fiioSink, read); };
    }

    bool enter()
    {
        mode->setLevel(QStringLiteral("exclusive"));
        mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
        return QTest::qWaitFor([this] { return volume.route == DacVolumePort::Route::Alsa; }, busTimeoutMs);
    }

    bool hold()
    {
        if (!enter())
            return false;
        player.play(1);
        return QTest::qWaitFor([this] {
            return mode->state() == OutputMode::State::ExHeld && player.chain == PlayerPort::OutputChain::Ex;
        }, busTimeoutMs);
    }

    QStringList log;
    FakeMixer *mixer;
    AlsaVolume alsa;
    FakePlayer player{&log};
    FakeReservation reservation{&log};
    FakeHeldOutput output{&log};
    FakeDacSink dac{&log};
    FakeDacVolume volume{&log};
    FakeDacEqPort dacEq{&log};
    WirePlumberRestart restart;
    std::unique_ptr<OutputMode> mode;
    QObject context;
};

}

class PowerTest : public QObject
{
    Q_OBJECT

public slots:
    void onScreenOffChanged(bool off);
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated);

private slots:
    void initTestCase();
    void cleanupTestCase();
    void registered();
    void secondInstanceNotRegistered();
    void screenOff();
    void powerMenu();
    void switchPrompt();
    void overlayNotSupported();
    void volume();
    void verbs();
    void sleepWatch();
    void keepMarker();
    void keepInBackground();
    void closeAndSwitch();
    void switchFails();
    void switchStalls();
    void statusSysfs();
    void clockFormat();
    void switchHeldOrder();
    void switchExitCodeRebuilds();
    void switchBeforeStartRebuildsNothing();
    void switchLevelOffStartsAtOnce();
    void switchDroppedAcquireRebuilds();
    void switchReadyFallback();
    void bpLevelAsyncDacExitCodeNoRebuild();
    void switchJoinedRestartAsyncDacNoRebuild();
    void switchDacOrder();
    void switchDacNeverAnswers();
    void switchDacFirstReleaseLater();
    void gameModeVerbByLevel();
    void sleepStopsParkTimer();
    void sleepReleasesInhibitorAfterWait();
    void backlightAtStart();
    void toggleScreen();
    void wifiOffWithScreen();
    void wifiOnFails();
    void holds();
    void holdsAgainForNewPowerd();
    void overlayHoldsNothing();
    void idleSuspend();
    void sleepTimer();
    void sessionsAndSwitchTo();
    void imageAndUpdate();

private:
    bool sawChange(const QString &name, const QVariant &value) const;
    bool writeStub(const QString &name, const QString &exitCode = QStringLiteral("0"));
    int logLines() const;
    QVariant savedState(const QString &key) const;
    bool startLogin1();
    QDBusMessage login1(const QString &method, const QList<QVariant> &arguments = {});
    int login1Count(const QString &method);
    int login1Calls(const QString &method);
    QString blPower() const;
    QStringList powerdHolds() const;

    QTemporaryDir m_dir;
    QProcess m_login1;
    FakeService m_powerd = FakeService::powerd(QStringLiteral(MICRODAP_FAKE_POWERD));
    bool m_login1Running = false;
    QString m_stubDir;
    QString m_emptyDir;
    QString m_log;
    QString m_blPower;
    QString m_keepFile;
    QString m_bootIdFile;
    QString m_stateFile;
    std::unique_ptr<Player> m_player;
    std::unique_ptr<State> m_state;
    std::unique_ptr<PowerBridge> m_bridge;
    std::unique_ptr<QDBusInterface> m_remote;
    QList<bool> m_screenOffSignals;
    QList<QVariantMap> m_changes;
};

void PowerTest::onScreenOffChanged(bool off)
{
    m_screenOffSignals.append(off);
}

void PowerTest::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                    const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface == PowerBridge::interfaceName())
        m_changes.append(changed);
}

bool PowerTest::sawChange(const QString &name, const QVariant &value) const
{
    for (const QVariantMap &change : m_changes) {
        if (change.contains(name) && change.value(name) == value)
            return true;
    }
    return false;
}

bool PowerTest::writeStub(const QString &name, const QString &exitCode)
{
    const QString script = QStringLiteral("#!/bin/sh\nprintf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"")
                           + m_log + QStringLiteral("\"\nexit ") + exitCode + QStringLiteral("\n");
    return writeFile(m_stubDir + QLatin1Char('/') + name, script, true);
}

int PowerTest::logLines() const
{
    return static_cast<int>(readFile(m_log).count(QLatin1Char('\n')));
}

QVariant PowerTest::savedState(const QString &key) const
{
    return savedValue(m_stateFile, key);
}

bool PowerTest::startLogin1()
{
    m_login1.setProcessChannelMode(QProcess::ForwardedErrorChannel);
    m_login1.start(QStringLiteral("python3"), {QStringLiteral(MICRODAP_FAKE_LOGIN1)});
    if (!m_login1.waitForStarted(busTimeoutMs))
        return false;
    QDBusConnectionInterface *iface = QDBusConnection::sessionBus().interface();
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < busTimeoutMs) {
        if (iface->isServiceRegistered(SleepInhibitor::service()))
            return true;
        if (m_login1.state() != QProcess::Running)
            return false;
        QTest::qWait(50);
    }
    return false;
}

QDBusMessage PowerTest::login1(const QString &method, const QList<QVariant> &arguments)
{
    QDBusMessage call = QDBusMessage::createMethodCall(SleepInhibitor::service(),
                                                       QStringLiteral("/net/seilent/MicroDap/FakeLogin1"),
                                                       QStringLiteral("net.seilent.MicroDap.FakeLogin1"), method);
    call.setArguments(arguments);
    return QDBusConnection::sessionBus().call(call, QDBus::Block, busTimeoutMs);
}

int PowerTest::login1Count(const QString &method)
{
    const QDBusMessage reply = login1(method);
    if (reply.type() == QDBusMessage::ErrorMessage)
        return -1;
    return reply.arguments().value(0).toInt();
}

int PowerTest::login1Calls(const QString &method)
{
    const QDBusMessage reply = login1(QStringLiteral("CallCount"), {method});
    if (reply.type() == QDBusMessage::ErrorMessage)
        return -1;
    return reply.arguments().value(0).toInt();
}

QString PowerTest::blPower() const
{
    return readFile(m_blPower);
}

QStringList PowerTest::powerdHolds() const
{
    return m_powerd.value(QStringLiteral("Holds")).toStringList();
}

void PowerTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus, run under dbus-run-session");

    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("MICRODAP_AUDIO_SINK"))
        qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));

    QVERIFY(m_dir.isValid());
    m_stubDir = m_dir.path() + QStringLiteral("/bin");
    m_emptyDir = m_dir.path() + QStringLiteral("/empty");
    QVERIFY(QDir().mkpath(m_emptyDir));
    m_log = m_dir.path() + QStringLiteral("/verbs.log");
    m_blPower = qEnvironmentVariable("MICRODAP_SYS_ROOT") + QStringLiteral("/class/backlight/panel0/bl_power");
    QVERIFY(writeFile(m_blPower, QStringLiteral("1")));
    QVERIFY(QDir().mkpath(qEnvironmentVariable("MICRODAP_SYS_ROOT") + QStringLiteral("/class/backlight/empty")));
    m_keepFile = m_dir.path() + QStringLiteral("/config/microdap/keep");
    m_bootIdFile = m_dir.path() + QStringLiteral("/boot_id");
    m_stateFile = m_dir.path() + QStringLiteral("/config/microdap/state.json");
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    QVERIFY(writeStub(QStringLiteral("nmcli")));
    QVERIFY(writeFile(m_bootIdFile, QStringLiteral("  0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10 \n")));
    qputenv("MICRODAP_VERB_DIR", m_stubDir.toUtf8());
    qputenv("MICRODAP_NMCLI_BIN", (m_stubDir + QStringLiteral("/nmcli")).toUtf8());
    qputenv("MICRODAP_KEEP_FILE", m_keepFile.toUtf8());
    qputenv("MICRODAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());
    qputenv("MICRODAP_SWITCH_TIMEOUT_MS", QByteArrayLiteral("400"));
    const QString asound = m_dir.path() + QStringLiteral("/asound/card1");
    QVERIFY(writeFile(asound + QStringLiteral("/id"), QStringLiteral("B\n")));
    QVERIFY(writeFile(asound + QStringLiteral("/pcm0p/sub0/status"), QStringLiteral("closed\n")));
    qputenv("MICRODAP_ASOUND_DIR", (m_dir.path() + QStringLiteral("/asound")).toUtf8());
    qputenv("PATH", m_stubDir.toUtf8() + ':' + qgetenv("PATH"));
    m_login1Running = startLogin1();
    if (m_login1Running)
        qputenv("MICRODAP_LOGIN1_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QVERIFY(m_powerd.start());
    qputenv("MICRODAP_POWERD_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));

    m_bridge = std::make_unique<PowerBridge>(nullptr, bus);
    QVERIFY(m_bridge->isRegistered());
    QCOMPARE(m_bridge->state(), nullptr);
    QCOMPARE(m_bridge->sleep(), nullptr);
    m_player = std::make_unique<Player>();
    m_state = std::make_unique<State>(m_player.get(), m_stateFile);
    m_bridge->setState(m_state.get());
    QCOMPARE(m_bridge->state(), m_state.get());

    QVERIFY(bus.connect(PowerBridge::serviceName(), PowerBridge::objectPath(),
                        PowerBridge::interfaceName(), QStringLiteral("ScreenOffChanged"), this,
                        SLOT(onScreenOffChanged(bool))));
    QVERIFY(bus.connect(PowerBridge::serviceName(), PowerBridge::objectPath(),
                        QStringLiteral("org.freedesktop.DBus.Properties"),
                        QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList))));

    m_remote = std::make_unique<QDBusInterface>(PowerBridge::serviceName(),
                                                PowerBridge::objectPath(),
                                                PowerBridge::interfaceName(), bus);
    QVERIFY2(m_remote->isValid(), qPrintable(m_remote->lastError().message()));
}

void PowerTest::cleanupTestCase()
{
    m_bridge.reset();
    m_powerd.stop();
    if (m_login1.state() == QProcess::Running) {
        login1(QStringLiteral("Quit"));
        if (!m_login1.waitForFinished(busTimeoutMs)) {
            m_login1.terminate();
            m_login1.waitForFinished(busTimeoutMs);
        }
    }
}

void PowerTest::registered()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusReply<bool> owned = bus.interface()->isServiceRegistered(PowerBridge::serviceName());
    QVERIFY2(owned.isValid(), qPrintable(owned.error().message()));
    QVERIFY(owned.value());

    QDBusInterface introspectable(PowerBridge::serviceName(), PowerBridge::objectPath(),
                                  QStringLiteral("org.freedesktop.DBus.Introspectable"), bus);
    QDBusReply<QString> xml = introspectable.call(QStringLiteral("Introspect"));
    QVERIFY2(xml.isValid(), qPrintable(xml.error().message()));
    QVERIFY(xml.value().contains(QStringLiteral("net.seilent.MicroDap.Player")));
    QVERIFY(xml.value().contains(QStringLiteral("SetScreenOff")));
    QVERIFY(xml.value().contains(QStringLiteral("ToggleScreen")));
    QVERIFY(xml.value().contains(QStringLiteral("ShowPowerMenu")));
    QVERIFY(xml.value().contains(QStringLiteral("ScreenOff")));
    QVERIFY(xml.value().contains(QStringLiteral("PlaybackStatus")));
    QVERIFY(xml.value().contains(QStringLiteral("ScreenOffChanged")));
    QVERIFY(xml.value().contains(QStringLiteral("ShowOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("HideOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("ToggleOverlay")));
    QVERIFY(xml.value().contains(QStringLiteral("ShowSwitchPrompt")));
    QVERIFY(xml.value().contains(QStringLiteral("OverlayShown")));

    QCOMPARE(m_remote->property("PlaybackStatus").toString(), QStringLiteral("Stopped"));
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QVERIFY(!m_bridge->overlayMode());
    QCOMPARE(m_bridge->overlay(), nullptr);
}

void PowerTest::secondInstanceNotRegistered()
{
    QDBusConnection second = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                           QStringLiteral("microdap-test-second"));
    QVERIFY(second.isConnected());
    {
        PowerBridge other(nullptr, second);
        QVERIFY(!other.isRegistered());
        QVERIFY(m_bridge->isRegistered());
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("microdap-test-second"));

    QDBusReply<QString> owner = QDBusConnection::sessionBus().interface()->serviceOwner(PowerBridge::serviceName());
    QVERIFY2(owner.isValid(), qPrintable(owner.error().message()));
    QCOMPARE(owner.value(), QDBusConnection::sessionBus().baseService());
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
}

void PowerTest::screenOff()
{
    m_screenOffSignals.clear();
    m_changes.clear();
    QSignalSpy spy(m_bridge.get(), &PowerBridge::screenOffChanged);

    QDBusReply<void> reply = m_remote->call(QStringLiteral("SetScreenOff"), true);
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->screenOff(), busTimeoutMs);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_remote->property("ScreenOff").toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(m_screenOffSignals.contains(true), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("ScreenOff"), true), busTimeoutMs);

    reply = m_remote->call(QStringLiteral("SetScreenOff"), true);
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCoreApplication::processEvents();
    QCOMPARE(spy.count(), 1);

    reply = m_remote->call(QStringLiteral("SetScreenOff"), false);
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->screenOff(), busTimeoutMs);
    QCOMPARE(spy.count(), 2);
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
    QTRY_VERIFY_WITH_TIMEOUT(m_screenOffSignals.contains(false), busTimeoutMs);

    m_bridge->setScreenOff(true);
    QCOMPARE(m_remote->property("ScreenOff").toBool(), true);
    m_bridge->setScreenOff(false);
    QCOMPARE(blPower(), QStringLiteral("0"));
}

void PowerTest::powerMenu()
{
    QSignalSpy spy(m_bridge.get(), &PowerBridge::showPowerMenu);
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowPowerMenu"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, busTimeoutMs);
}

void PowerTest::switchPrompt()
{
    QSignalSpy spy(m_bridge.get(), &PowerBridge::showSwitchPrompt);
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ShowSwitchPrompt"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, busTimeoutMs);
    reply = m_remote->call(QStringLiteral("ShowSwitchPrompt"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
}

void PowerTest::keepMarker()
{
    QCOMPARE(Keep::markerPath(), m_keepFile);
    QCOMPARE(Keep::bootId(), QStringLiteral("0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10"));
    QVERIFY(!QFile::exists(m_keepFile));
    QVERIFY(Keep::clear());
    QVERIFY(Keep::write());
    QCOMPARE(readFile(m_keepFile), QStringLiteral("0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10\n"));
    QCOMPARE(Keep::marker(), Keep::bootId());
    QVERIFY(Keep::clear());
    QVERIFY(!QFile::exists(m_keepFile));

    const QString statePath = m_dir.path() + QStringLiteral("/state/state.json");
    Player player;
    State state(&player, statePath);
    QVERIFY(Keep::write());
    QVERIFY(state.saveExit());
    QVERIFY(QFile::exists(m_keepFile));
    QCOMPARE(savedValue(statePath, QStringLiteral("handoff")).toBool(), true);
    QVERIFY(Keep::clear());
    QVERIFY(state.saveExit());
    QVERIFY(!QFile::exists(m_keepFile));
    QVERIFY(QFile::exists(statePath));
}

void PowerTest::keepInBackground()
{
    QFile::remove(m_log);
    QFile::remove(m_stateFile);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QSignalSpy switching(m_bridge.get(), &PowerBridge::switchingChanged);

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QCOMPARE(switching.count(), 1);
    QCOMPARE(readFile(m_keepFile), QStringLiteral("0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10\n"));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(readFile(m_log), switchLine());

    QTest::ignoreMessage(QtInfoMsg, "microdap power: keep in background");
    QTest::ignoreMessage(QtInfoMsg, "microdap power: switch already running");
    m_bridge->keepInBackground();
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 1);
    QVERIFY(m_bridge->switching());
    QCOMPARE(failed.count(), 0);
    QVERIFY(QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), true);

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, PowerBridge::switchTimeoutMs() + busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QCOMPARE(switching.count(), 2);
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
}

void PowerTest::closeAndSwitch()
{
    QFile::remove(m_log);
    QVERIFY(Keep::write());
    QVERIFY(m_state->save(true));
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    m_bridge->closeAndSwitch();
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(readFile(m_log), switchLine());

    QTest::ignoreMessage(QtInfoMsg, "microdap power: close player, switch to \"steam\"");
    QTest::ignoreMessage(QtInfoMsg, "microdap power: switch already running");
    m_bridge->closeAndSwitch();
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 1);

    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, PowerBridge::switchTimeoutMs() + busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
}

void PowerTest::switchFails()
{
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    m_bridge->keepInBackground();
    QVERIFY(QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
    QCOMPARE(logLines(), 1);

    QVERIFY(Keep::write());
    m_bridge->keepInBackground();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, busTimeoutMs);
    QVERIFY(!QFile::exists(m_keepFile));

    qputenv("MICRODAP_VERB_DIR", m_emptyDir.toUtf8());
    m_bridge->keepInBackground();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 3, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    qputenv("MICRODAP_VERB_DIR", m_stubDir.toUtf8());
    QVERIFY(writeStub(QStringLiteral("systemctl")));

    const int lines = logLines();
    QSignalSpy switching(m_bridge.get(), &PowerBridge::switchingChanged);
    qputenv("MICRODAP_BOOT_ID_FILE", (m_dir.path() + QStringLiteral("/missing")).toUtf8());
    m_bridge->keepInBackground();
    QCOMPARE(failed.count(), 4);
    QCOMPARE(switching.count(), 0);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), lines);
    qputenv("MICRODAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());

    QTest::ignoreMessage(QtWarningMsg, "microdap power: session bus not connected");
    PowerBridge detached(nullptr, QDBusConnection(QStringLiteral("microdap-no-session-bus")));
    QCOMPARE(detached.state(), nullptr);
    QSignalSpy detachedFailed(&detached, &PowerBridge::switchFailed);
    detached.keepInBackground();
    QCOMPARE(detachedFailed.count(), 1);
    QVERIFY(!detached.switching());
    QVERIFY(!QFile::exists(m_keepFile));
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), lines);
}

void PowerTest::switchStalls()
{
    QFile::remove(m_log);
    QCOMPARE(PowerBridge::switchTimeoutMs(), 400);
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QElapsedTimer timer;
    timer.start();

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(failed.count(), 0);
    QVERIFY(QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= 400);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);

    m_bridge->keepInBackground();
    QVERIFY(QFile::exists(m_keepFile));
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, busTimeoutMs);
    QVERIFY(!QFile::exists(m_keepFile));
}

void PowerTest::overlayNotSupported()
{
    QCOMPARE(PowerBridge::notSupportedError(), QStringLiteral("net.seilent.MicroDap.Error.NotSupported"));
    for (const char *member : {"ShowOverlay", "HideOverlay", "ToggleOverlay"}) {
        const QByteArray called = QByteArrayLiteral("microdap power: ") + member;
        const QByteArray rejected = called + QByteArrayLiteral(" needs overlay mode");
        QTest::ignoreMessage(QtInfoMsg, called.constData());
        QTest::ignoreMessage(QtInfoMsg, rejected.constData());
        QDBusReply<void> reply = m_remote->call(QString::fromLatin1(member));
        QVERIFY2(!reply.isValid(), member);
        QCOMPARE(reply.error().name(), PowerBridge::notSupportedError());
    }
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QVERIFY(!m_bridge->overlayShown());
    QTest::ignoreMessage(QtInfoMsg, "microdap power: ShowOverlay needs overlay mode");
    m_bridge->showOverlay();
    QTest::ignoreMessage(QtInfoMsg, "microdap power: ToggleOverlay needs overlay mode");
    m_bridge->toggleOverlay();
    QVERIFY(!m_bridge->overlayShown());
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
}

void PowerTest::volume()
{
    QDBusConnection emitter = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                            QStringLiteral("microdap-test-hotkeys"));
    QVERIFY(emitter.isConnected());
    QSignalSpy spy(m_bridge.get(), &PowerBridge::volumeChanged);

    QDBusMessage signal = QDBusMessage::createSignal(PowerBridge::hotkeysPath(),
                                                     PowerBridge::hotkeysInterface(),
                                                     QStringLiteral("VolumeChanged"));
    signal << 0.35;
    QVERIFY(emitter.send(signal));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, busTimeoutMs);
    QCOMPARE(m_bridge->volume(), 35);
    QCOMPARE(spy.at(0).at(0).toInt(), 35);

    signal = QDBusMessage::createSignal(PowerBridge::hotkeysPath(), PowerBridge::hotkeysInterface(),
                                        QStringLiteral("VolumeChanged"));
    signal << 0.35;
    QVERIFY(emitter.send(signal));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, busTimeoutMs);
    QCOMPARE(m_bridge->volume(), 35);

    signal = QDBusMessage::createSignal(PowerBridge::hotkeysPath(), PowerBridge::hotkeysInterface(),
                                        QStringLiteral("VolumeChanged"));
    signal << 1.5;
    QVERIFY(emitter.send(signal));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 3, busTimeoutMs);
    QCOMPARE(m_bridge->volume(), 100);

    signal = QDBusMessage::createSignal(PowerBridge::hotkeysPath(), PowerBridge::hotkeysInterface(),
                                        QStringLiteral("VolumeChanged"));
    signal << -0.2;
    QVERIFY(emitter.send(signal));
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 4, busTimeoutMs);
    QCOMPARE(m_bridge->volume(), 0);

    QDBusConnection::disconnectFromBus(QStringLiteral("microdap-test-hotkeys"));
}

void PowerTest::verbs()
{
    QCOMPARE(PowerBridge::verbs().size(), 4);
    QFile::remove(m_log);
    qputenv("MICRODAP_VERB_DIR", m_stubDir.toUtf8());
    const int suspends = login1Calls(QStringLiteral("Suspend"));
    const int poweroffs = login1Calls(QStringLiteral("PowerOff"));

    QVERIFY(m_bridge->runVerb(QStringLiteral("screen-off")));
    QVERIFY(m_bridge->screenOff());
    QCOMPARE(blPower(), QStringLiteral("4"));
    m_bridge->runScreenOn();
    QCOMPARE(blPower(), QStringLiteral("0"));
    QVERIFY(m_bridge->runVerb(QStringLiteral("suspend")));
    QVERIFY(m_bridge->runVerb(QStringLiteral("poweroff")));
    if (m_login1Running) {
        QTRY_COMPARE_WITH_TIMEOUT(login1Calls(QStringLiteral("Suspend")), suspends + 1, busTimeoutMs);
        QTRY_COMPARE_WITH_TIMEOUT(login1Calls(QStringLiteral("PowerOff")), poweroffs + 1, busTimeoutMs);
    }
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QVERIFY(m_bridge->runVerb(QStringLiteral("game-mode")));
    QVERIFY(m_bridge->switching());
    QVERIFY(!m_bridge->runVerb(QStringLiteral("game-mode")));
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(readFile(m_log), switchLine());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);

    QVERIFY(!m_bridge->runVerb(QStringLiteral("rm")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("rm -rf /")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("SCREEN-OFF")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("screen-off; rm")));
    QVERIFY(!m_bridge->runVerb(QString()));
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 1);
    QVERIFY(!m_bridge->screenOff());
}

void PowerTest::sleepWatch()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    QFile::remove(m_log);
    QFile::remove(m_stateFile);
    QCOMPARE(login1Count(QStringLiteral("InhibitCount")), 0);

    m_bridge->enableSleepWatch();
    QVERIFY(m_bridge->sleep() != nullptr);
    QVERIFY(m_bridge->sleep()->subscribed());
    QVERIFY(m_bridge->sleep()->held());
    QCOMPARE(m_bridge->sleep()->takeCount(), 1);
    QCOMPARE(login1Count(QStringLiteral("InhibitCount")), 1);
    QTRY_COMPARE_WITH_TIMEOUT(login1Count(QStringLiteral("OpenFds")), 1, busTimeoutMs);
    const QDBusMessage last = login1(QStringLiteral("LastInhibit"));
    QCOMPARE(last.arguments().value(0).toStringList(),
             QStringList({QStringLiteral("sleep"), QStringLiteral("microdap"), QStringLiteral("save state"),
                          QStringLiteral("delay")}));
    m_bridge->enableSleepWatch();
    QCOMPARE(m_bridge->sleep()->takeCount(), 1);

    QElapsedTimer timer;
    timer.start();
    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->sleep()->held(), busTimeoutMs);
    QVERIFY(timer.elapsed() < 5000);
    QVERIFY(QFile::exists(m_stateFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
    QCOMPARE(savedState(QStringLiteral("status")).toString(), QStringLiteral("stopped"));
    QTRY_COMPARE_WITH_TIMEOUT(login1Count(QStringLiteral("OpenFds")), 0, busTimeoutMs);
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 0);

    QSignalSpy wake(m_bridge.get(), &PowerBridge::wakeFinished);
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->sleep()->held(), busTimeoutMs);
    QCOMPARE(m_bridge->sleep()->takeCount(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(login1Count(QStringLiteral("InhibitCount")), 2, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(wake.count(), 1, busTimeoutMs);
    QCOMPARE(wake.at(0).at(0).toBool(), true);
    QVERIFY(!m_bridge->waking());
    QCOMPARE(blPower(), QStringLiteral("0"));
    QVERIFY(!m_bridge->screenOff());
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 0);
    QCOMPARE(m_player->status(), Player::Stopped);
}

void PowerTest::statusSysfs()
{
    const QString root = m_dir.path() + QStringLiteral("/power_supply");
    QVERIFY(writeFile(root + QStringLiteral("/ADP1/type"), QStringLiteral("Mains\n")));
    QVERIFY(writeFile(root + QStringLiteral("/ADP1/online"), QStringLiteral("1\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/type"), QStringLiteral("Battery\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/capacity"), QStringLiteral("73\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/status"), QStringLiteral("Charging\n")));

    StatusModel status(QDBusConnection(QStringLiteral("microdap-no-system-bus")), root);
    QVERIFY(!status.usesUPower());
    QVERIFY(status.batteryPresent());
    QCOMPARE(status.battery(), 73);
    QVERIFY(status.charging());
    QVERIFY(status.active());
    QVERIFY(!status.clock().isEmpty());

    QSignalSpy batterySpy(&status, &StatusModel::batteryChanged);
    QSignalSpy chargingSpy(&status, &StatusModel::chargingChanged);
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/capacity"), QStringLiteral("9\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/status"), QStringLiteral("Discharging\n")));
    status.refresh();
    QCOMPARE(status.battery(), 9);
    QVERIFY(!status.charging());
    QCOMPARE(batterySpy.count(), 1);
    QCOMPARE(chargingSpy.count(), 1);

    QVERIFY(writeFile(root + QStringLiteral("/BAT0/status"), QStringLiteral("Full\n")));
    QTRY_VERIFY_WITH_TIMEOUT(status.charging(), 3000);

    status.setActive(false);
    QVERIFY(!status.active());
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/capacity"), QStringLiteral("50\n")));
    QTest::qWait(1200);
    QCOMPARE(status.battery(), 9);
    status.setActive(true);
    QCOMPARE(status.battery(), 50);

    StatusModel none(QDBusConnection(QStringLiteral("microdap-no-system-bus")), m_emptyDir);
    QVERIFY(!none.batteryPresent());
    QCOMPARE(none.battery(), -1);
    QVERIFY(!none.charging());
}

void PowerTest::clockFormat()
{
    QCOMPARE(StatusModel::formatClock(QTime(7, 13)), QStringLiteral("7:13"));
    QCOMPARE(StatusModel::formatClock(QTime(0, 5)), QStringLiteral("0:05"));
    QCOMPARE(StatusModel::formatClock(QTime(19, 30, 59)), QStringLiteral("19:30"));
    QCOMPARE(StatusModel::formatClock(QTime(19, 30), QStringLiteral("12h")), QStringLiteral("7:30"));
    QCOMPARE(StatusModel::formatClock(QTime(0, 5), QStringLiteral("12h")), QStringLiteral("12:05"));
    QCOMPARE(StatusModel::formatClock(QTime(12, 0), QStringLiteral("12h")), QStringLiteral("12:00"));
    QCOMPARE(StatusModel::formatClock(QTime(7, 13), QStringLiteral("off")), QString());
}

void PowerTest::switchHeldOrder()
{
    OutputRig rig;
    QVERIFY(rig.hold());
    QFile::remove(m_log);
    QFile::remove(m_stateFile);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    rig.reservation.onRelease = [this, &rig] {
        rig.log.append(QStringLiteral("release seen, handoff %1, service lines %2")
                           .arg(savedState(QStringLiteral("handoff")).toBool() ? 1 : 0)
                           .arg(logLines()));
        QTimer::singleShot(300, &rig.context, [&rig] { rig.mode->onReleased(); });
    };
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QElapsedTimer timer;
    timer.start();

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QVERIFY(rig.mode->switchStoppedStream());
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 1);
    QCOMPARE(rig.log.at(indexOf(rig.log, QStringLiteral("release seen"))),
             QStringLiteral("release seen, handoff 1, service lines 0"));
    QVERIFY(indexOf(rig.log, QStringLiteral("player park")) < indexOf(rig.log, QStringLiteral("reserve release")));

    QTest::ignoreMessage(QtInfoMsg, "microdap power: keep in background");
    QTest::ignoreMessage(QtInfoMsg, "microdap power: switch already running");
    m_bridge->keepInBackground();
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 1);
    QTest::qWait(150);
    QCOMPARE(logLines(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    const qint64 started = timer.elapsed();
    QVERIFY(started >= 300);
    QCOMPARE(readFile(m_log), switchLine());
    QCOMPARE(failed.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= 300 + PowerBridge::switchTimeoutMs());
    QVERIFY(!m_bridge->switching());
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 0);
    QVERIFY(rig.player.isParked);
    QCOMPARE(rig.mode->reason(), OutputMode::Reason::Switch);
}

void PowerTest::switchExitCodeRebuilds()
{
    OutputRig rig;
    QVERIFY(rig.hold());
    rig.player.seek(2500);
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] {
        m_bridge->setOutputMode(nullptr, nullptr);
        writeStub(QStringLiteral("systemctl"));
    });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    m_bridge->closeAndSwitch();
    QVERIFY(m_bridge->switching());
    QVERIFY(rig.mode->switchStoppedStream());
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 1);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(logLines(), 1);
    QVERIFY(!m_bridge->switching());
    QVERIFY(indexOf(rig.log, QStringLiteral("player park 2500")) >= 0);
    QVERIFY(indexOf(rig.log, QStringLiteral("player park 2500"))
            < indexOf(rig.log, QStringLiteral("player rebuild 2500 playing")));
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 1);
    QVERIFY(QTest::qWaitFor([&rig] {
        return rig.mode->state() == OutputMode::State::ExHeld && rig.player.chain == PlayerPort::OutputChain::Ex;
    }, busTimeoutMs));
    QCOMPARE(rig.player.positionMs(), qint64(2500));
}

void PowerTest::switchBeforeStartRebuildsNothing()
{
    OutputRig rig;
    QVERIFY(rig.hold());
    QFile::remove(m_log);
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    qputenv("MICRODAP_BOOT_ID_FILE", (m_dir.path() + QStringLiteral("/missing")).toUtf8());
    auto detach = qScopeGuard([this] {
        m_bridge->setOutputMode(nullptr, nullptr);
        qputenv("MICRODAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());
    });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    const int calls = rig.player.calls.size();

    m_bridge->keepInBackground();
    QCOMPARE(failed.count(), 1);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!rig.mode->switchStoppedStream());
    QTest::qWait(200);
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 0);
    QCOMPARE(rig.player.calls.size(), calls);
    QCOMPARE(rig.mode->state(), OutputMode::State::ExHeld);
    QCOMPARE(logLines(), 0);
}

void PowerTest::switchLevelOffStartsAtOnce()
{
    OutputRig rig;
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    rig.player.play(1);
    QCOMPARE(rig.player.chain, PlayerPort::OutputChain::Pw);
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    QStringList messages;
    capturedMessages = &messages;
    previousHandler = qInstallMessageHandler(captureMessage);
    auto detach = qScopeGuard([this] {
        qInstallMessageHandler(previousHandler);
        capturedMessages = nullptr;
        m_bridge->setOutputMode(nullptr, nullptr);
    });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    m_bridge->closeAndSwitch();
    QVERIFY(m_bridge->switching());
    QVERIFY(!rig.mode->switchStoppedStream());
    QCOMPARE(countOf(messages, QStringLiteral("microdap power: switch ")), 1);
    QCOMPARE(countOf(messages, QStringLiteral("microdap power: switch waits")), 0);
    QCOMPARE(countOf(messages, QStringLiteral("microdap output:")), 0);
    QVERIFY(rig.reservation.calls.isEmpty());
    QVERIFY(!rig.player.isParked);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 0);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("park")), 0);
    QCOMPARE(countOf(messages, QStringLiteral("microdap output:")), 0);
    QCOMPARE(rig.player.status, PlayerPort::PlayState::Playing);
}

void PowerTest::switchDroppedAcquireRebuilds()
{
    OutputRig rig;
    QVERIFY(rig.enter());
    rig.reservation.onAcquire = nullptr;
    rig.player.play(1);
    rig.player.seek(1500);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.reservation.calls, QStringLiteral("acquire")), 1, busTimeoutMs);
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] {
        m_bridge->setOutputMode(nullptr, nullptr);
        writeStub(QStringLiteral("systemctl"));
    });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    m_bridge->closeAndSwitch();
    QVERIFY(rig.player.isParked);
    QVERIFY(!rig.mode->switchStoppedStream());
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild 1500 playing")), 1);
}

void PowerTest::switchReadyFallback()
{
    OutputRig rig;
    QVERIFY(rig.hold());
    rig.reservation.onRelease = nullptr;
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    const int bound = rig.mode->switchBoundMs() + PowerBridge::switchReadyMarginMs;
    QElapsedTimer timer;
    timer.start();

    QTest::ignoreMessage(QtWarningMsg,
                         qPrintable(QStringLiteral("microdap power: no output release within %1 ms, switching").arg(bound)));
    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QVERIFY(rig.mode->switchStoppedStream());
    QTest::qWait(bound - 300);
    QCOMPARE(logLines(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= bound);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 0);
}

void PowerTest::bpLevelAsyncDacExitCodeNoRebuild()
{
    OutputRig rig(true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Delayed;
    rig.dacEq.delayMs = 150;
    rig.mode->setLevel(QStringLiteral("bitperfect"));
    rig.mode->onActiveOutputChanged(QStringLiteral("usb"), fiioSink);
    QVERIFY(QTest::qWaitFor([&rig] { return rig.volume.route == DacVolumePort::Route::Alsa; }, busTimeoutMs));
    rig.player.play(1);
    rig.player.seek(1200);
    QCOMPARE(rig.player.chain, PlayerPort::OutputChain::Bp);
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] {
        m_bridge->setOutputMode(nullptr, nullptr);
        writeStub(QStringLiteral("systemctl"));
    });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    const int calls = rig.player.calls.size();

    m_bridge->closeAndSwitch();
    QVERIFY(m_bridge->switching());
    QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("release")});
    QVERIFY(!rig.mode->switchStoppedStream());
    QCOMPARE(logLines(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QCOMPARE(rig.player.calls.size(), calls);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 0);
    QVERIFY(!rig.player.isParked);
    QCOMPARE(rig.player.chain, PlayerPort::OutputChain::Bp);
    QCOMPARE(rig.player.status, PlayerPort::PlayState::Playing);
}

void PowerTest::switchJoinedRestartAsyncDacNoRebuild()
{
    const QString restartStub = m_dir.path() + QStringLiteral("/slow-systemctl");
    QVERIFY(writeFile(restartStub, QStringLiteral("#!/bin/sh\nsleep 1\nexit 0\n"), true));
    const QByteArray previous = qgetenv("MICRODAP_SYSTEMCTL_BIN");
    qputenv("MICRODAP_SYSTEMCTL_BIN", restartStub.toUtf8());
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    auto restore = qScopeGuard([this, previous] {
        qputenv("MICRODAP_SYSTEMCTL_BIN", previous);
        writeStub(QStringLiteral("systemctl"));
    });
    QList<int> rebuilds;
    for (const bool withDac : {false, true}) {
        OutputRig rig(withDac, true);
        rig.dacEq.answer = FakeDacEqPort::Answer::Delayed;
        rig.dacEq.delayMs = 150;
        QVERIFY(rig.hold());
        rig.output.onRefresh = nullptr;
        rig.player.seek(900);
        rig.player.pause();
        QTRY_VERIFY_WITH_TIMEOUT(rig.restart.restarting(), busTimeoutMs);
        QVERIFY(rig.mode->releasing());
        QVERIFY(rig.player.isParked);
        QFile::remove(m_log);
        m_bridge->setOutputMode(rig.mode.get(), &rig.player);
        auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
        QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

        m_bridge->closeAndSwitch();
        QVERIFY(!rig.mode->releasing());
        QVERIFY(!rig.mode->switchStoppedStream());
        QCOMPARE(m_bridge->switching(), true);
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
        QCOMPARE(logLines(), 1);
        rebuilds.append(int(countOf(rig.player.calls, QStringLiteral("rebuild"))));
        QTRY_VERIFY_WITH_TIMEOUT(!rig.restart.restarting(), busTimeoutMs);
    }
    QCOMPARE(rebuilds, (QList<int>{0, 0}));
}

void PowerTest::switchDacOrder()
{
    OutputRig rig(true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Never;
    QVERIFY(rig.hold());
    QFile::remove(m_log);
    QFile::remove(m_stateFile);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    rig.reservation.onRelease = [this, &rig] {
        rig.log.append(QStringLiteral("release seen, handoff %1, service lines %2")
                           .arg(savedState(QStringLiteral("handoff")).toBool() ? 1 : 0)
                           .arg(logLines()));
        QTimer::singleShot(100, &rig.context, [&rig] { rig.mode->onReleased(); });
    };
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QElapsedTimer timer;
    timer.start();

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), true);
    QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("release")});
    QCOMPARE(rig.log.at(indexOf(rig.log, QStringLiteral("release seen"))),
             QStringLiteral("release seen, handoff 1, service lines 0"));
    QVERIFY(indexOf(rig.log, QStringLiteral("daceq release")) >= 0);
    QTest::qWait(300);
    QCOMPARE(logLines(), 0);
    QCOMPARE(rig.dacEq.pending.size(), 1);

    rig.dacEq.pending.takeFirst()();
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= 300);
    QVERIFY(timer.elapsed() < rig.mode->switchBoundMs());
    QCOMPARE(readFile(m_log), switchLine());
    QCOMPARE(failed.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
}

void PowerTest::switchDacNeverAnswers()
{
    OutputRig rig(true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Never;
    QVERIFY(rig.hold());
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    const int dacBound = rigBounds().dacReleaseMs;
    QElapsedTimer timer;
    timer.start();

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("release")});
    QTest::qWait(dacBound - 150);
    QCOMPARE(logLines(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= dacBound);
    QVERIFY(timer.elapsed() < rig.mode->switchBoundMs() + PowerBridge::switchReadyMarginMs);
    QCOMPARE(readFile(m_log), switchLine());
    QCOMPARE(rig.dacEq.pending.size(), 1);
    rig.dacEq.pending.takeFirst()();
    QTest::qWait(50);
    QCOMPARE(logLines(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
}

void PowerTest::switchDacFirstReleaseLater()
{
    OutputRig rig(true);
    rig.dacEq.answer = FakeDacEqPort::Answer::Now;
    QVERIFY(rig.hold());
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    rig.reservation.onRelease = [&rig] {
        QTimer::singleShot(300, &rig.context, [&rig] { rig.mode->onReleased(); });
    };
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QElapsedTimer timer;
    timer.start();

    m_bridge->keepInBackground();
    QVERIFY(m_bridge->switching());
    QCOMPARE(rig.dacEq.calls, QStringList{QStringLiteral("release")});
    QVERIFY(rig.dacEq.pending.isEmpty());
    QTest::qWait(200);
    QCOMPARE(logLines(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QVERIFY(timer.elapsed() >= 300);
    QCOMPARE(readFile(m_log), switchLine());
    QCOMPARE(failed.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
}

void PowerTest::gameModeVerbByLevel()
{
    OutputRig rig;
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);

    QVERIFY(m_bridge->runVerb(QStringLiteral("game-mode")));
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);

    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QVERIFY(m_bridge->runVerb(QStringLiteral("game-mode")));
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 2, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
}

void PowerTest::sleepStopsParkTimer()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    m_bridge->enableSleepWatch();
    QVERIFY(m_bridge->sleep() != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->sleep()->held(), busTimeoutMs);
    OutputRig rig;
    QVERIFY(rig.hold());
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    auto detach = qScopeGuard([this] { m_bridge->setOutputMode(nullptr, nullptr); });
    rig.player.pause();
    QVERIFY(!rig.player.isParked);

    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->sleep()->held(), busTimeoutMs);
    QVERIFY(rig.player.isParked);
    QVERIFY(!rig.alsa.isOpen());
    QTest::qWait(rigBounds().parkMs + 300);
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 0);
    QCOMPARE(rig.mode->state(), OutputMode::State::ExHeld);

    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->sleep()->held(), busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(countOf(rig.log, QStringLiteral("reserve release")), 1, busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), busTimeoutMs);
}

void PowerTest::sleepReleasesInhibitorAfterWait()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    m_bridge->enableSleepWatch();
    QVERIFY(m_bridge->sleep() != nullptr);
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->sleep()->held(), busTimeoutMs);
    OutputRig rig;
    QVERIFY(rig.hold());
    rig.reservation.onRelease = nullptr;
    rig.reservation.releaseCompletes = false;
    m_bridge->setOutputMode(rig.mode.get(), &rig.player);
    const QMetaObject::Connection held = connect(m_bridge->sleep(), &SleepInhibitor::heldChanged, this,
                                                 [this, &rig] {
        if (!m_bridge->sleep()->held())
            rig.log.append(QStringLiteral("inhibitor released"));
    });
    auto detach = qScopeGuard([this, held] {
        disconnect(held);
        m_bridge->setOutputMode(nullptr, nullptr);
    });
    rig.player.stop();
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 1);
    QVERIFY(rig.mode->releasing());

    login1(QStringLiteral("EmitPrepareForSleep"), {true});
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->sleep()->held(), busTimeoutMs);
    const int waited = indexOf(rig.log, QStringLiteral("reserve wait released 120"));
    QVERIFY(waited >= 0);
    QVERIFY(waited < indexOf(rig.log, QStringLiteral("inhibitor released")));

    rig.mode->onReleased();
    login1(QStringLiteral("EmitPrepareForSleep"), {false});
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->sleep()->held(), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(!rig.mode->releasing(), busTimeoutMs);
}

void PowerTest::backlightAtStart()
{
    QVERIFY(writeFile(m_blPower, QStringLiteral("1")));
    const QString marker = PowerBridge::wifiMarker();
    QVERIFY(writeFile(marker, QString()));
    QFile::remove(m_log);
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-start"));
    {
        PowerBridge bridge(nullptr, other);
        bridge.runScreenOn();
        QCOMPARE(blPower(), QStringLiteral("0"));
        QVERIFY(bridge.transition());
        QTRY_VERIFY_WITH_TIMEOUT(!bridge.transition(), busTimeoutMs);
        QCOMPARE(readFile(m_log), QStringLiteral("nmcli radio wifi on\n"));
        QVERIFY(!QFile::exists(marker));
        QFile::remove(m_log);
        bridge.runScreenOn();
        QCoreApplication::processEvents();
        QCOMPARE(logLines(), 0);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-start"));
    QCOMPARE(PowerBridge::backlightDir(), qEnvironmentVariable("MICRODAP_SYS_ROOT") + QStringLiteral("/class/backlight"));
    QCOMPARE(PowerBridge::wifiMarker(), QDir::homePath() + QStringLiteral("/.local/state/microdap/wifi-off"));
}

void PowerTest::toggleScreen()
{
    QTest::qWait(PowerBridge::toggleDebounceMs + 100);
    QVERIFY(!m_bridge->screenOff());
    QDBusReply<void> reply = m_remote->call(QStringLiteral("ToggleScreen"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(m_bridge->screenOff(), busTimeoutMs);
    QCOMPARE(blPower(), QStringLiteral("4"));

    QTest::ignoreMessage(QtInfoMsg, "microdap power: ToggleScreen");
    QTest::ignoreMessage(QtInfoMsg, "microdap power: toggle within 2000 ms of the last transition, ignored");
    reply = m_remote->call(QStringLiteral("ToggleScreen"));
    QVERIFY(reply.isValid());
    QTest::qWait(100);
    QVERIFY(m_bridge->screenOff());
    QCOMPARE(blPower(), QStringLiteral("4"));

    QElapsedTimer timer;
    timer.start();
    QTest::qWait(PowerBridge::toggleDebounceMs + 100);
    m_bridge->toggleScreen();
    QVERIFY(!m_bridge->screenOff());
    QCOMPARE(blPower(), QStringLiteral("0"));
    m_bridge->toggleScreen();
    QVERIFY(!m_bridge->screenOff());
    QVERIFY(timer.elapsed() >= PowerBridge::toggleDebounceMs);
}

void PowerTest::wifiOffWithScreen()
{
    QTest::qWait(PowerBridge::toggleDebounceMs + 100);
    Settings settings(nullptr);
    settings.setWifiOffScreenOff(true);
    m_bridge->setSettings(&settings);
    auto detach = qScopeGuard([this] { m_bridge->setSettings(nullptr); });
    const QString marker = PowerBridge::wifiMarker();
    QFile::remove(marker);
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("nmcli")));

    m_bridge->toggleScreen();
    QVERIFY(m_bridge->screenOff());
    QVERIFY(QFile::exists(marker));
    QVERIFY(m_bridge->transition());
    QTest::ignoreMessage(QtInfoMsg, "microdap power: toggle during a transition, ignored");
    m_bridge->toggleScreen();
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->transition(), busTimeoutMs);
    QCOMPARE(readFile(m_log), QStringLiteral("nmcli radio wifi off\n"));
    QVERIFY(QFile::exists(marker));

    m_bridge->runScreenOff();
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 1);

    m_bridge->runScreenOn();
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->transition(), busTimeoutMs);
    QCOMPARE(readFile(m_log), QStringLiteral("nmcli radio wifi off\nnmcli radio wifi on\n"));
    QVERIFY(!QFile::exists(marker));

    settings.setWifiOffScreenOff(false);
    m_bridge->runScreenOff();
    QVERIFY(!QFile::exists(marker));
    QVERIFY(!m_bridge->transition());
    m_bridge->runScreenOn();
    QCOMPARE(logLines(), 2);
}

void PowerTest::wifiOnFails()
{
    const QString marker = PowerBridge::wifiMarker();
    QVERIFY(writeFile(marker, QString()));
    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("nmcli"), QStringLiteral("1")));
    auto restore = qScopeGuard([this] { writeStub(QStringLiteral("nmcli")); });
    QTest::ignoreMessage(QtWarningMsg, "microdap power: nmcli radio wifi on exit 1");
    m_bridge->runScreenOn();
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->transition(), busTimeoutMs);
    QVERIFY(QFile::exists(marker));
    QVERIFY(writeStub(QStringLiteral("nmcli")));
    m_bridge->runScreenOn();
    QTRY_VERIFY_WITH_TIMEOUT(!m_bridge->transition(), busTimeoutMs);
    QVERIFY(!QFile::exists(marker));
}

void PowerTest::holds()
{
    m_powerd.call(QStringLiteral("Clear"));
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-holds"));
    {
        PowerBridge bridge(nullptr, other);
        QCOMPARE(bridge.holdMode(), QString());
        QCOMPARE(powerdHolds(), QStringList());
        bridge.enableHolds();
        QCOMPARE(bridge.holdMode(), QStringLiteral("dap"));
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap")}, busTimeoutMs);
        bridge.runScreenOff();
        QCOMPARE(bridge.holdMode(), QStringLiteral("dap-off"));
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap-off")}, busTimeoutMs);
        bridge.runScreenOn();
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap")}, busTimeoutMs);
        bridge.enableHolds();
        QTest::qWait(100);
        QCOMPARE(m_powerd.value(QStringLiteral("Calls")).toStringList().filter(QStringLiteral("Hold dap")).size(), 3);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-holds"));
}

void PowerTest::holdsAgainForNewPowerd()
{
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-again"));
    {
        PowerBridge bridge(nullptr, other);
        bridge.enableHolds();
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap")}, busTimeoutMs);
        m_powerd.stop();
        bridge.runScreenOff();
        QVERIFY(m_powerd.start());
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap-off")}, busTimeoutMs);
        bridge.runScreenOn();
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-again"));
    QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList(), busTimeoutMs);
}

void PowerTest::overlayHoldsNothing()
{
    m_powerd.call(QStringLiteral("Clear"));
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-overlay"));
    {
        PowerBridge bridge(nullptr, other);
        bridge.enableHolds();
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList{QStringLiteral("dap")}, busTimeoutMs);
        Player player;
        OverlayBridge overlay(nullptr, &player);
        bridge.setOverlay(&overlay);
        QCOMPARE(bridge.holdMode(), QString());
        QTRY_COMPARE_WITH_TIMEOUT(powerdHolds(), QStringList(), busTimeoutMs);
        QVERIFY(m_powerd.value(QStringLiteral("Calls")).toStringList().contains(QStringLiteral("Release dap")));
        bridge.enableHolds();
        QTest::ignoreMessage(QtInfoMsg, "microdap power: ToggleScreen ignored in overlay mode");
        bridge.toggleScreen();
        bridge.runScreenOff();
        QVERIFY(!bridge.screenOff());
        QTest::qWait(100);
        QCOMPARE(powerdHolds(), QStringList());
        bridge.setOverlay(nullptr);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-overlay"));
}

void PowerTest::idleSuspend()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    qputenv("MICRODAP_IDLE_CHECK_MS", QByteArrayLiteral("50"));
    qputenv("MICRODAP_IDLE_MINUTE_MS", QByteArrayLiteral("40"));
    auto restore = qScopeGuard([] {
        qunsetenv("MICRODAP_IDLE_CHECK_MS");
        qunsetenv("MICRODAP_IDLE_MINUTE_MS");
    });
    Settings settings(nullptr);
    settings.setSuspendMinutes(5);
    QCOMPARE(settings.suspendMinutes(), 5);
    Player player;
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-idle"));
    {
        PowerBridge bridge(&player, other);
        bridge.setSettings(&settings);
        const int before = login1Calls(QStringLiteral("Suspend"));
        bridge.runScreenOn();
        QTest::qWait(400);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before);

        QElapsedTimer timer;
        timer.start();
        bridge.runScreenOff();
        QTRY_COMPARE_WITH_TIMEOUT(login1Calls(QStringLiteral("Suspend")), before + 1, busTimeoutMs);
        QVERIFY(timer.elapsed() >= 200);

        player.setTransportActive(true);
        QTest::qWait(500);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before + 1);
        player.setTransportActive(false);
        QTRY_COMPARE_WITH_TIMEOUT(login1Calls(QStringLiteral("Suspend")), before + 2, busTimeoutMs);

        settings.setSuspendMinutes(0);
        QTest::qWait(500);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before + 2);
        bridge.runScreenOn();
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-idle"));
}

void PowerTest::sleepTimer()
{
    if (!m_login1Running)
        QSKIP("fake login1 did not start (python3 with dbus and gi needed)");
    QDirIterator it(QStringLiteral(MICRODAP_FIXTURE_MUSIC), {QStringLiteral("*.flac")}, QDir::Files,
                    QDirIterator::Subdirectories);
    QVERIFY(it.hasNext());
    QueueEntry entry;
    entry.path = it.next();
    entry.title = QStringLiteral("fixture");
    const QByteArray sink = qgetenv("MICRODAP_AUDIO_SINK");
    qputenv("MICRODAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));
    qputenv("MICRODAP_IDLE_MINUTE_MS", QByteArrayLiteral("150"));
    auto restore = qScopeGuard([sink] {
        qputenv("MICRODAP_AUDIO_SINK", sink);
        qunsetenv("MICRODAP_IDLE_MINUTE_MS");
    });
    Player player;
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-sleep"));
    {
        PowerBridge bridge(&player, other);
        QSignalSpy changed(&bridge, &PowerBridge::sleepTimerChanged);
        const int before = login1Calls(QStringLiteral("Suspend"));
        bridge.startSleepTimer(0);
        QCOMPARE(changed.count(), 0);
        QCOMPARE(bridge.sleepMinutesLeft(), 0);

        bridge.startSleepTimer(3);
        QCOMPARE(bridge.sleepMinutesLeft(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.sleepMinutesLeft(), 2, busTimeoutMs);
        player.play({entry}, 0);
        QCOMPARE(player.status(), Player::Playing);
        QElapsedTimer timer;
        timer.start();
        bridge.startSleepTimer(3);
        QCOMPARE(bridge.sleepMinutesLeft(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.sleepMinutesLeft(), 0, busTimeoutMs);
        QVERIFY(timer.elapsed() >= 400);
        QCOMPARE(player.status(), Player::Paused);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before);
        QTRY_COMPARE_WITH_TIMEOUT(login1Calls(QStringLiteral("Suspend")), before + 1, busTimeoutMs);
        QVERIFY(timer.elapsed() >= 400 + PowerBridge::sleepEndDelayMs);

        bridge.startSleepTimer(1);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.sleepMinutesLeft(), 0, busTimeoutMs);
        bridge.noteInput();
        QTest::qWait(PowerBridge::sleepEndDelayMs + 300);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before + 1);

        bridge.startSleepTimer(1);
        QTRY_COMPARE_WITH_TIMEOUT(bridge.sleepMinutesLeft(), 0, busTimeoutMs);
        bridge.startSleepTimer(2);
        QCOMPARE(bridge.sleepMinutesLeft(), 2);
        bridge.startSleepTimer(0);
        QCOMPARE(bridge.sleepMinutesLeft(), 0);
        QTest::qWait(300 + PowerBridge::sleepEndDelayMs + 300);
        QCOMPARE(login1Calls(QStringLiteral("Suspend")), before + 1);
        bridge.noteInput();
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-sleep"));
}

void PowerTest::sessionsAndSwitchTo()
{
    const QString dir = m_dir.path() + QStringLiteral("/sessions.d");
    QVERIFY(writeFile(dir + QStringLiteral("/steam.conf"), QStringLiteral("LABEL=Steam\nDEFAULT=yes\n")));
    QVERIFY(writeFile(dir + QStringLiteral("/microdap.conf"), QStringLiteral("LABEL=MicroDAP\nDEFAULT=yes\n")));
    QVERIFY(writeFile(dir + QStringLiteral("/microemu.conf"), QStringLiteral("LABEL=MicroEMU\nSTOP=unit\nDEFAULT=yes\n")));
    QVERIFY(writeFile(dir + QStringLiteral("/recovery.conf"), QStringLiteral("LABEL=Recovery\n")));
    QVERIFY(writeFile(dir + QStringLiteral("/Bad Name.conf"), QStringLiteral("DEFAULT=yes\n")));
    const QVariantList sessions = PowerBridge::readSessions(dir, QStringLiteral("microdap"));
    QCOMPARE(sessions.size(), 2);
    QCOMPARE(sessions.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("microemu"));
    QCOMPARE(sessions.at(0).toMap().value(QStringLiteral("label")).toString(), QStringLiteral("MicroEMU"));
    QCOMPARE(sessions.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("steam"));
    QVERIFY(PowerBridge::readSessions(m_emptyDir, QStringLiteral("microdap")).isEmpty());
    qputenv("MICRODAP_SESSIONS_DIR", dir.toUtf8());
    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-sessions"));
    {
        PowerBridge bridge(nullptr, other);
        QCOMPARE(bridge.sessions(), sessions);
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-sessions"));
    qunsetenv("MICRODAP_SESSIONS_DIR");

    QFile::remove(m_log);
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    QVERIFY(Keep::write());
    QSignalSpy failed(m_bridge.get(), &PowerBridge::switchFailed);
    QVERIFY(!m_bridge->switchTo(QStringLiteral("microdap")));
    QVERIFY(!m_bridge->switchTo(QStringLiteral("../steam")));
    QVERIFY(!m_bridge->switchTo(QString()));
    QVERIFY(QFile::exists(m_keepFile));
    QVERIFY(m_bridge->switchTo(QStringLiteral("microemu")));
    QVERIFY(!QFile::exists(m_keepFile));
    QVERIFY(!m_bridge->switchTo(QStringLiteral("steam")));
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(readFile(m_log), switchLine(QStringLiteral("microemu")));
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(PowerBridge::switchCommand(QStringLiteral("steam")),
             QStringList({QStringLiteral("systemctl"), QStringLiteral("start"), QStringLiteral("--no-block"),
                          QStringLiteral("microos-session-switch@steam.service")}));
}

void PowerTest::imageAndUpdate()
{
    const QString release = m_dir.path() + QStringLiteral("/os-release");
    QVERIFY(writeFile(release, QStringLiteral("NAME=\"MicroOS\"\nIMAGE_VERSION=\"testing-38784f1\"\nID=microos\n")));
    const QString update = m_dir.path() + QStringLiteral("/update-available");
    const QString showDir = m_dir.path() + QStringLiteral("/show");
    const QString show = m_dir.path() + QStringLiteral("/show.txt");
    QVERIFY(writeFile(showDir + QStringLiteral("/systemctl"),
                      QStringLiteral("#!/bin/sh\ncat \"") + show + QStringLiteral("\"\n"), true));
    qputenv("MICRODAP_OS_RELEASE", release.toUtf8());
    qputenv("MICRODAP_UPDATE_FILE", update.toUtf8());
    qputenv("MICRODAP_VERB_DIR", showDir.toUtf8());
    auto restore = qScopeGuard([this] {
        qunsetenv("MICRODAP_OS_RELEASE");
        qunsetenv("MICRODAP_UPDATE_FILE");
        qputenv("MICRODAP_VERB_DIR", m_stubDir.toUtf8());
    });
    QCOMPARE(PowerBridge::checkStatus(QStringLiteral("ExecMainStatus=0\nExecMainStartTimestampMonotonic=0\n")),
             QStringLiteral("unchecked"));
    QCOMPARE(PowerBridge::checkStatus(QStringLiteral("ExecMainStatus=7\nExecMainStartTimestampMonotonic=912\n")),
             QStringLiteral("none"));
    QCOMPARE(PowerBridge::checkStatus(QStringLiteral("ExecMainStatus=4\nExecMainStartTimestampMonotonic=912\n")),
             QStringLiteral("offline"));
    QCOMPARE(PowerBridge::checkStatus(QStringLiteral("ExecMainStatus=1\nExecMainStartTimestampMonotonic=912\n")),
             QStringLiteral("unchecked"));
    QCOMPARE(PowerBridge::checkStatus(QString()), QStringLiteral("unchecked"));

    QDBusConnection other = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-update"));
    {
        PowerBridge bridge(nullptr, other);
        QCOMPARE(bridge.imageVersion(), QStringLiteral("testing-38784f1"));
        QCOMPARE(bridge.updateStatus(), QStringLiteral("unchecked"));
        QSignalSpy changed(&bridge, &PowerBridge::updateChanged);

        QVERIFY(writeFile(update, QStringLiteral("state=fetch\nimage=ghcr.io/x\nversion=testing-aaaa111\nchecked=2026-10-16T01:00:00Z\n")));
        bridge.reloadUpdate();
        QCOMPARE(bridge.updateStatus(), QStringLiteral("available"));
        QCOMPARE(bridge.updateVersion(), QStringLiteral("testing-aaaa111"));
        QCOMPARE(bridge.updateState(), QStringLiteral("fetch"));
        QCOMPARE(bridge.updateChecked(), QStringLiteral("2026-10-16T01:00:00Z"));
        QCOMPARE(changed.count(), 1);

        QVERIFY(writeFile(update, QStringLiteral("state=withdrawn\nversion=testing-aaaa111\n")));
        QVERIFY(writeFile(show, QStringLiteral("ExecMainStatus=7\nExecMainStartTimestampMonotonic=4242\n")));
        bridge.reloadUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("none"), busTimeoutMs);
        QCOMPARE(bridge.updateVersion(), QString());

        QVERIFY(QFile::remove(update));
        QVERIFY(writeFile(show, QStringLiteral("ExecMainStatus=4\nExecMainStartTimestampMonotonic=4242\n")));
        bridge.reloadUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("offline"), busTimeoutMs);

        qputenv("MICRODAP_VERB_DIR", m_emptyDir.toUtf8());
        bridge.reloadUpdate();
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("unchecked"), busTimeoutMs);
        QCOMPARE(bridge.updateState(), QString());

        const QString temp = update + QStringLiteral(".tmp");
        QVERIFY(writeFile(temp, QStringLiteral("state=staged\nversion=testing-bbbb222\nchecked=2026-10-16T02:00:00Z\n")));
        QVERIFY(QFile::rename(temp, update));
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("available"), busTimeoutMs);
        QCOMPARE(bridge.updateState(), QStringLiteral("staged"));
        QCOMPARE(bridge.updateVersion(), QStringLiteral("testing-bbbb222"));
        QCOMPARE(bridge.updateChecked(), QStringLiteral("2026-10-16T02:00:00Z"));
        QVERIFY(QFile::remove(update));
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("none"), busTimeoutMs);
        QCOMPARE(bridge.updateState(), QString());
    }
    const QString later = m_dir.path() + QStringLiteral("/run-later/microos/update-available");
    qputenv("MICRODAP_UPDATE_FILE", later.toUtf8());
    QDBusConnection watch = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-watch"));
    {
        PowerBridge bridge(nullptr, watch);
        QCOMPARE(bridge.updateStatus(), QStringLiteral("unchecked"));
        QVERIFY(writeFile(later, QStringLiteral("state=switch\nversion=testing-cccc333\n")));
        QTRY_COMPARE_WITH_TIMEOUT(bridge.updateStatus(), QStringLiteral("available"), busTimeoutMs);
        QCOMPARE(bridge.updateState(), QStringLiteral("switch"));
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-watch"));
    QDBusConnection::disconnectFromBus(QStringLiteral("power-update"));
    qputenv("MICRODAP_OS_RELEASE", (m_dir.path() + QStringLiteral("/none")).toUtf8());
    QDBusConnection empty = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("power-noimage"));
    {
        PowerBridge bridge(nullptr, empty);
        QCOMPARE(bridge.imageVersion(), QString());
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("power-noimage"));
}

QTEST_GUILESS_MAIN(PowerTest)
#include "tst_power.moc"
