#include "AlsaVolume.h"
#include "CodecFragment.h"
#include "Keep.h"
#include "OutputFakes.h"
#include "OutputMode.h"
#include "Player.h"
#include "PowerBridge.h"
#include "Settings.h"
#include "SleepInhibitor.h"
#include "State.h"
#include "StatusModel.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
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

using namespace casdap;

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
    explicit OutputRig(bool withDacEq = false, bool withCodec = false)
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
        if (withCodec)
            ports.codec = &codec;
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
    CodecFragment codec;
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
    void powerBinPath();
    void sleepWatch();
    void keepMarker();
    void keepInBackground();
    void closeAndSwitch();
    void switchFails();
    void switchStalls();
    void drift();
    void settingsPersist();
    void settingsOrientation();
    void settingsAutoRotate();
    void settingsTouch();
    void settingsOutputLevel();
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

private:
    bool sawChange(const QString &name, const QVariant &value) const;
    bool writeStub(const QString &name, const QString &exitCode = QStringLiteral("0"));
    int logLines() const;
    QVariant savedState(const QString &key) const;
    bool startLogin1();
    QDBusMessage login1(const QString &method, const QList<QVariant> &arguments = {});
    int login1Count(const QString &method);

    QTemporaryDir m_dir;
    QProcess m_login1;
    bool m_login1Running = false;
    QString m_stubDir;
    QString m_emptyDir;
    QString m_log;
    QString m_driftFile;
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
    m_login1.start(QStringLiteral("python3"), {QStringLiteral(CASDAP_FAKE_LOGIN1)});
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
                                                       QStringLiteral("/org/casdap/FakeLogin1"),
                                                       QStringLiteral("org.casdap.FakeLogin1"), method);
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

void PowerTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus, run under dbus-run-session");

    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=false"));

    QVERIFY(m_dir.isValid());
    m_stubDir = m_dir.path() + QStringLiteral("/bin");
    m_emptyDir = m_dir.path() + QStringLiteral("/empty");
    QVERIFY(QDir().mkpath(m_emptyDir));
    m_log = m_dir.path() + QStringLiteral("/verbs.log");
    m_driftFile = m_dir.path() + QStringLiteral("/drift");
    m_keepFile = m_dir.path() + QStringLiteral("/config/casdap/keep");
    m_bootIdFile = m_dir.path() + QStringLiteral("/boot_id");
    m_stateFile = m_dir.path() + QStringLiteral("/config/casdap/state.json");
    QVERIFY(writeStub(QStringLiteral("casdap-power")));
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    QVERIFY(writeFile(m_driftFile, QStringLiteral("/usr/libexec/armada/session-control\n\n")));
    QVERIFY(writeFile(m_bootIdFile, QStringLiteral("  0f6f1a2e-5d1c-4a1b-9c3e-7b2d8e4f6a10 \n")));
    qputenv("CASDAP_DRIFT_FILE", m_driftFile.toUtf8());
    qputenv("CASDAP_VERB_DIR", m_stubDir.toUtf8());
    qputenv("CASDAP_POWER_BIN", (m_stubDir + QStringLiteral("/casdap-power")).toUtf8());
    qputenv("CASDAP_KEEP_FILE", m_keepFile.toUtf8());
    qputenv("CASDAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());
    qputenv("CASDAP_SWITCH_TIMEOUT_MS", QByteArrayLiteral("400"));
    const QString asound = m_dir.path() + QStringLiteral("/asound/card1");
    QVERIFY(writeFile(asound + QStringLiteral("/id"), QStringLiteral("B\n")));
    QVERIFY(writeFile(asound + QStringLiteral("/pcm0p/sub0/status"), QStringLiteral("closed\n")));
    qputenv("CASDAP_ASOUND_DIR", (m_dir.path() + QStringLiteral("/asound")).toUtf8());
    qputenv("PATH", m_stubDir.toUtf8() + ':' + qgetenv("PATH"));
    m_login1Running = startLogin1();
    if (m_login1Running)
        qputenv("CASDAP_LOGIN1_BUS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));

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
    QVERIFY(xml.value().contains(QStringLiteral("org.casdap.Player")));
    QVERIFY(xml.value().contains(QStringLiteral("SetScreenOff")));
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
                                                           QStringLiteral("casdap-test-second"));
    QVERIFY(second.isConnected());
    {
        PowerBridge other(nullptr, second);
        QVERIFY(!other.isRegistered());
        QVERIFY(m_bridge->isRegistered());
    }
    QDBusConnection::disconnectFromBus(QStringLiteral("casdap-test-second"));

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
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));

    QTest::ignoreMessage(QtInfoMsg, "casdap power: keep in background");
    QTest::ignoreMessage(QtInfoMsg, "casdap power: switch already running");
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
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));

    QTest::ignoreMessage(QtInfoMsg, "casdap power: close player");
    QTest::ignoreMessage(QtInfoMsg, "casdap power: switch already running");
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

    qputenv("CASDAP_VERB_DIR", m_emptyDir.toUtf8());
    m_bridge->keepInBackground();
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 3, busTimeoutMs);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    qputenv("CASDAP_VERB_DIR", m_stubDir.toUtf8());
    QVERIFY(writeStub(QStringLiteral("systemctl")));

    const int lines = logLines();
    QSignalSpy switching(m_bridge.get(), &PowerBridge::switchingChanged);
    qputenv("CASDAP_BOOT_ID_FILE", (m_dir.path() + QStringLiteral("/missing")).toUtf8());
    m_bridge->keepInBackground();
    QCOMPARE(failed.count(), 4);
    QCOMPARE(switching.count(), 0);
    QVERIFY(!m_bridge->switching());
    QVERIFY(!QFile::exists(m_keepFile));
    QCOMPARE(savedState(QStringLiteral("handoff")).toBool(), false);
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), lines);
    qputenv("CASDAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());

    QTest::ignoreMessage(QtWarningMsg, "casdap power: session bus not connected");
    PowerBridge detached(nullptr, QDBusConnection(QStringLiteral("casdap-no-session-bus")));
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
    QCOMPARE(PowerBridge::notSupportedError(), QStringLiteral("org.casdap.Error.NotSupported"));
    for (const char *member : {"ShowOverlay", "HideOverlay", "ToggleOverlay"}) {
        const QByteArray called = QByteArrayLiteral("casdap power: ") + member;
        const QByteArray rejected = called + QByteArrayLiteral(" needs overlay mode");
        QTest::ignoreMessage(QtInfoMsg, called.constData());
        QTest::ignoreMessage(QtInfoMsg, rejected.constData());
        QDBusReply<void> reply = m_remote->call(QString::fromLatin1(member));
        QVERIFY2(!reply.isValid(), member);
        QCOMPARE(reply.error().name(), PowerBridge::notSupportedError());
    }
    QCOMPARE(m_remote->property("OverlayShown").toBool(), false);
    QVERIFY(!m_bridge->overlayShown());
    QTest::ignoreMessage(QtInfoMsg, "casdap power: ShowOverlay needs overlay mode");
    m_bridge->showOverlay();
    QTest::ignoreMessage(QtInfoMsg, "casdap power: ToggleOverlay needs overlay mode");
    m_bridge->toggleOverlay();
    QVERIFY(!m_bridge->overlayShown());
    QCOMPARE(m_remote->property("ScreenOff").toBool(), false);
}

void PowerTest::volume()
{
    QDBusConnection emitter = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                            QStringLiteral("casdap-test-hotkeys"));
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

    QDBusConnection::disconnectFromBus(QStringLiteral("casdap-test-hotkeys"));
}

void PowerTest::verbs()
{
    QCOMPARE(PowerBridge::verbs().size(), 4);
    QFile::remove(m_log);
    qputenv("CASDAP_VERB_DIR", m_stubDir.toUtf8());

    for (const QString &verb : PowerBridge::verbs())
        QVERIFY2(m_bridge->runVerb(verb), qPrintable(verb));

    QTRY_VERIFY_WITH_TIMEOUT(logLines() >= 4, busTimeoutMs);
    const QString log = readFile(m_log);
    QVERIFY2(log.contains(QStringLiteral("casdap-power screen-off\n")), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("systemctl start casdap-switch-back.service\n")),
             qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("casdap-power suspend\n")), qPrintable(log));
    QVERIFY2(log.contains(QStringLiteral("casdap-power poweroff\n")), qPrintable(log));

    QVERIFY(!m_bridge->runVerb(QStringLiteral("rm")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("rm -rf /")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("SCREEN-OFF")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("screen-off; rm")));
    QVERIFY(!m_bridge->runVerb(QString()));
    QCoreApplication::processEvents();
    QCOMPARE(logLines(), 4);

    qputenv("CASDAP_POWER_BIN", (m_emptyDir + QStringLiteral("/casdap-power")).toUtf8());
    QVERIFY(!m_bridge->runVerb(QStringLiteral("screen-off")));
    QVERIFY(!m_bridge->runVerb(QStringLiteral("screen-off")));
    QCOMPARE(logLines(), 4);
    qputenv("CASDAP_POWER_BIN", (m_stubDir + QStringLiteral("/casdap-power")).toUtf8());
    QVERIFY(m_bridge->runVerb(QStringLiteral("screen-off")));
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 5, busTimeoutMs);
}

void PowerTest::powerBinPath()
{
    const QByteArray saved = qgetenv("CASDAP_POWER_BIN");
    qunsetenv("CASDAP_POWER_BIN");
    QCOMPARE(PowerBridge::powerBin(), QStringLiteral("/usr/libexec/casdap/casdap-power"));
    QCOMPARE(PowerBridge::commandFor(QStringLiteral("screen-off")),
             QStringList({QStringLiteral("/usr/libexec/casdap/casdap-power"), QStringLiteral("screen-off")}));
    QCOMPARE(PowerBridge::commandFor(QStringLiteral("suspend")).constFirst(),
             QStringLiteral("/usr/libexec/casdap/casdap-power"));
    QCOMPARE(PowerBridge::commandFor(QStringLiteral("poweroff")).constFirst(),
             QStringLiteral("/usr/libexec/casdap/casdap-power"));
    QCOMPARE(PowerBridge::wakeCommand(),
             QStringList({QStringLiteral("/usr/libexec/casdap/casdap-power"), QStringLiteral("screen-on")}));
    QCOMPARE(PowerBridge::commandFor(QStringLiteral("game-mode")).constFirst(), QStringLiteral("systemctl"));
    qputenv("CASDAP_POWER_BIN", saved);
    QCOMPARE(PowerBridge::powerBin(), QString::fromUtf8(saved));
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
             QStringList({QStringLiteral("sleep"), QStringLiteral("casdap"), QStringLiteral("save state"),
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
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QCOMPARE(readFile(m_log), QStringLiteral("casdap-power screen-on\n"));
    QCOMPARE(m_player->status(), Player::Stopped);
}

void PowerTest::drift()
{
    QCOMPARE(m_bridge->driftPaths(),
             QStringList{QStringLiteral("/usr/libexec/armada/session-control")});

    QSignalSpy spy(m_bridge.get(), &PowerBridge::driftPathsChanged);
    QVERIFY(writeFile(m_driftFile,
                      QStringLiteral("/usr/libexec/armada/session-control\n"
                                     "/usr/share/inputplumber/profiles/default.yaml\n")));
    m_bridge->reloadDrift();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_bridge->driftPaths().size(), 2);
    m_bridge->reloadDrift();
    QCOMPARE(spy.count(), 1);

    QVERIFY(QFile::remove(m_driftFile));
    m_bridge->reloadDrift();
    QCOMPARE(spy.count(), 2);
    QVERIFY(m_bridge->driftPaths().isEmpty());
}

void PowerTest::settingsPersist()
{
    QVERIFY(Settings::defaultPath().endsWith(QStringLiteral("/casdap/settings.ini")));
    const QString path = m_dir.path() + QStringLiteral("/settings.ini");
    {
        Settings settings(path);
        QCOMPARE(settings.path(), path);
        QCOMPARE(settings.output(), QStringLiteral("auto"));
        QCOMPARE(settings.btCodec(), QStringLiteral("auto"));
        QCOMPARE(Settings::outputs(), QStringList({QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb")}));
        QCOMPARE(Settings::codecs(), QStringList({QStringLiteral("auto"), QStringLiteral("aptx"), QStringLiteral("aac"), QStringLiteral("ldac"), QStringLiteral("sbc")}));
        QCOMPARE(settings.roots(), Settings::defaultRoots());
        QCOMPARE(settings.accentHue(), 350);
        QVERIFY(!settings.accentFromArt());
        QCOMPARE(settings.scale(), 1.0);
        QCOMPARE(settings.saverSeconds(), 30);
        QCOMPARE(settings.metaObject()->indexOfProperty("keepPhoneInGameMode"), -1);

        QSignalSpy scaleSpy(&settings, &Settings::scaleChanged);
        settings.setScale(1.1);
        QCOMPARE(scaleSpy.count(), 1);
        QVERIFY(!QFile::exists(path));
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(path), Settings::syncDelayMs + busTimeoutMs);
        settings.setScale(1.1);
        QCOMPARE(scaleSpy.count(), 1);
        settings.setScale(9.0);
        QCOMPARE(settings.scale(), Settings::maxScale);
        settings.setScale(0.1);
        QCOMPARE(settings.scale(), Settings::minScale);
        settings.setScale(1.14);
        QCOMPARE(settings.scale(), 1.1);

        settings.setOutput(QStringLiteral("headphones"));
        settings.setOutput(QStringLiteral("tape"));
        settings.setOutput(QStringLiteral("bt"));
        QCOMPARE(settings.output(), QStringLiteral("headphones"));

        QSignalSpy codecSpy(&settings, &Settings::btCodecChanged);
        settings.setBtCodec(QStringLiteral("AAC"));
        QCOMPARE(codecSpy.count(), 1);
        QCOMPARE(settings.btCodec(), QStringLiteral("aac"));
        settings.setBtCodec(QStringLiteral("aac"));
        settings.setBtCodec(QStringLiteral("mp3"));
        QCOMPARE(codecSpy.count(), 1);
        QCOMPARE(settings.btCodec(), QStringLiteral("aac"));

        QSignalSpy hueSpy(&settings, &Settings::accentHueChanged);
        settings.setAccentHue(198);
        QCOMPARE(settings.accentHue(), 200);
        settings.setAccentHue(200);
        QCOMPARE(hueSpy.count(), 1);
        settings.setAccentHue(-10);
        QCOMPARE(settings.accentHue(), 350);
        settings.setAccentHue(360);
        QCOMPARE(settings.accentHue(), 0);
        settings.setAccentHue(204);
        QCOMPARE(settings.accentHue(), 200);
        settings.setAccentFromArt(true);

        settings.setSaverSeconds(5);
        QCOMPARE(settings.saverSeconds(), Settings::minSaverSeconds);
        settings.setSaverSeconds(45);
        settings.setRoots({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")});
    }
    QVERIFY(QFile::exists(path));
    QVERIFY(!readFile(path).contains(QStringLiteral("keepPhoneInGameMode")));
    QVERIFY(readFile(path).contains(QStringLiteral("btCodec=aac")));

    Settings again(path);
    QCOMPARE(again.output(), QStringLiteral("headphones"));
    QCOMPARE(again.btCodec(), QStringLiteral("aac"));
    QCOMPARE(again.roots(), QStringList({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")}));
    QCOMPARE(again.accentHue(), 200);
    QVERIFY(again.accentFromArt());
    QCOMPARE(again.scale(), 1.1);
    QCOMPARE(again.saverSeconds(), 45);
}

void PowerTest::settingsOrientation()
{
    const QString path = m_dir.path() + QStringLiteral("/orientation.ini");
    QCOMPARE(Settings::orientations(),
             QStringList({QStringLiteral("portrait"), QStringLiteral("portrait-flipped"),
                          QStringLiteral("landscape"), QStringLiteral("landscape-flipped")}));
    for (const QString &value : Settings::orientations()) {
        {
            Settings settings(path);
            QSignalSpy spy(&settings, &Settings::orientationChanged);
            settings.setOrientation(value);
            settings.setOrientation(QStringLiteral("sideways"));
            QCOMPARE(settings.orientation(), value);
            QCOMPARE(spy.count(), value == QStringLiteral("portrait") ? 0 : 1);
            settings.setOrientation(value);
            QCOMPARE(spy.count(), value == QStringLiteral("portrait") ? 0 : 1);
        }
        Settings again(path);
        QCOMPARE(again.orientation(), value);
        again.setOrientation(QStringLiteral("portrait"));
    }
    QVERIFY(writeFile(path, QStringLiteral("[General]\norientation=sideways\n")));
    Settings bogus(path);
    QCOMPARE(bogus.orientation(), QStringLiteral("portrait"));
}

void PowerTest::settingsAutoRotate()
{
    const QString path = m_dir.path() + QStringLiteral("/autorotate.ini");
    {
        Settings settings(path);
        QVERIFY(!settings.autoRotate());
        QSignalSpy spy(&settings, &Settings::autoRotateChanged);
        settings.setAutoRotate(true);
        settings.setAutoRotate(true);
        QVERIFY(settings.autoRotate());
        QCOMPARE(spy.count(), 1);
    }
    {
        Settings again(path);
        QVERIFY(again.autoRotate());
        again.setAutoRotate(false);
    }
    Settings off(path);
    QVERIFY(!off.autoRotate());
}

void PowerTest::settingsTouch()
{
    const QString path = m_dir.path() + QStringLiteral("/touch.ini");
    {
        Settings settings(path);
        QVERIFY(settings.touch());
        QSignalSpy spy(&settings, &Settings::touchChanged);
        settings.setTouch(false);
        settings.setTouch(false);
        QVERIFY(!settings.touch());
        QCOMPARE(spy.count(), 1);
    }
    {
        Settings again(path);
        QVERIFY(!again.touch());
        again.setTouch(true);
    }
    Settings on(path);
    QVERIFY(on.touch());
}

void PowerTest::settingsOutputLevel()
{
    const QString path = m_dir.path() + QStringLiteral("/level.ini");
    QCOMPARE(Settings::outputLevels(),
             QStringList({QStringLiteral("off"), QStringLiteral("bitperfect"), QStringLiteral("exclusive")}));
    QCOMPARE(Settings::sampleRates(),
             QStringList({QStringLiteral("auto"), QStringLiteral("44100"), QStringLiteral("48000"),
                          QStringLiteral("88200"), QStringLiteral("96000"), QStringLiteral("176400"),
                          QStringLiteral("192000"), QStringLiteral("352800"), QStringLiteral("384000")}));
    {
        Settings settings(path);
        QCOMPARE(settings.outputLevel(), QStringLiteral("off"));
        QCOMPARE(settings.sampleRate(), QStringLiteral("auto"));
        QSignalSpy levelSpy(&settings, &Settings::outputLevelChanged);
        QSignalSpy rateSpy(&settings, &Settings::sampleRateChanged);
        settings.setOutputLevel(QStringLiteral("off"));
        settings.setSampleRate(QStringLiteral("auto"));
        QCOMPARE(levelSpy.count(), 0);
        QCOMPARE(rateSpy.count(), 0);
        settings.setOutputLevel(QStringLiteral("EXCLUSIVE"));
        settings.setOutputLevel(QStringLiteral("exclusive"));
        settings.setOutputLevel(QStringLiteral("on"));
        QCOMPARE(settings.outputLevel(), QStringLiteral("exclusive"));
        QCOMPARE(levelSpy.count(), 1);
        settings.setSampleRate(QStringLiteral("352800"));
        settings.setSampleRate(QStringLiteral("352800"));
        settings.setSampleRate(QStringLiteral("22050"));
        QCOMPARE(settings.sampleRate(), QStringLiteral("352800"));
        QCOMPARE(rateSpy.count(), 1);
        QVERIFY(!readFile(path).contains(QStringLiteral("outputLevel")));
        QTRY_VERIFY_WITH_TIMEOUT(readFile(path).contains(QStringLiteral("outputLevel=exclusive")),
                                 Settings::syncDelayMs + busTimeoutMs);
        QVERIFY(readFile(path).contains(QStringLiteral("sampleRate=352800")));
    }
    {
        Settings again(path);
        QCOMPARE(again.outputLevel(), QStringLiteral("exclusive"));
        QCOMPARE(again.sampleRate(), QStringLiteral("352800"));
        again.setOutputLevel(QStringLiteral("bitperfect"));
        again.setSampleRate(QStringLiteral("AUTO"));
    }
    {
        Settings back(path);
        QCOMPARE(back.outputLevel(), QStringLiteral("bitperfect"));
        QCOMPARE(back.sampleRate(), QStringLiteral("auto"));
    }
    QVERIFY(writeFile(path, QStringLiteral("[General]\noutputLevel=on\nsampleRate=22050\n")));
    Settings bogus(path);
    QCOMPARE(bogus.outputLevel(), QStringLiteral("off"));
    QCOMPARE(bogus.sampleRate(), QStringLiteral("auto"));
    QVERIFY(writeFile(path, QStringLiteral("[General]\noutputLevel=Exclusive\nsampleRate=96000\n")));
    Settings cased(path);
    QCOMPARE(cased.outputLevel(), QStringLiteral("exclusive"));
    QCOMPARE(cased.sampleRate(), QStringLiteral("96000"));
}

void PowerTest::statusSysfs()
{
    const QString root = m_dir.path() + QStringLiteral("/power_supply");
    QVERIFY(writeFile(root + QStringLiteral("/ADP1/type"), QStringLiteral("Mains\n")));
    QVERIFY(writeFile(root + QStringLiteral("/ADP1/online"), QStringLiteral("1\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/type"), QStringLiteral("Battery\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/capacity"), QStringLiteral("73\n")));
    QVERIFY(writeFile(root + QStringLiteral("/BAT0/status"), QStringLiteral("Charging\n")));

    StatusModel status(QDBusConnection(QStringLiteral("casdap-no-system-bus")), root);
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

    StatusModel none(QDBusConnection(QStringLiteral("casdap-no-system-bus")), m_emptyDir);
    QVERIFY(!none.batteryPresent());
    QCOMPARE(none.battery(), -1);
    QVERIFY(!none.charging());
}

void PowerTest::clockFormat()
{
    QCOMPARE(StatusModel::formatClock(QTime(7, 13)), QStringLiteral("7:13"));
    QCOMPARE(StatusModel::formatClock(QTime(0, 5)), QStringLiteral("0:05"));
    QCOMPARE(StatusModel::formatClock(QTime(19, 30, 59)), QStringLiteral("19:30"));
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

    QTest::ignoreMessage(QtInfoMsg, "casdap power: keep in background");
    QTest::ignoreMessage(QtInfoMsg, "casdap power: switch already running");
    m_bridge->keepInBackground();
    QCOMPARE(countOf(rig.log, QStringLiteral("reserve release")), 1);
    QTest::qWait(150);
    QCOMPARE(logLines(), 0);

    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    const qint64 started = timer.elapsed();
    QVERIFY(started >= 300);
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));
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
    qputenv("CASDAP_BOOT_ID_FILE", (m_dir.path() + QStringLiteral("/missing")).toUtf8());
    auto detach = qScopeGuard([this] {
        m_bridge->setOutputMode(nullptr, nullptr);
        qputenv("CASDAP_BOOT_ID_FILE", m_bootIdFile.toUtf8());
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
    QCOMPARE(countOf(messages, QStringLiteral("casdap power: switch ")), 1);
    QCOMPARE(countOf(messages, QStringLiteral("casdap power: switch waits")), 0);
    QCOMPARE(countOf(messages, QStringLiteral("casdap output:")), 0);
    QVERIFY(rig.reservation.calls.isEmpty());
    QVERIFY(!rig.player.isParked);
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("rebuild")), 0);
    QCOMPARE(countOf(rig.player.calls, QStringLiteral("park")), 0);
    QCOMPARE(countOf(messages, QStringLiteral("casdap output:")), 0);
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
                         qPrintable(QStringLiteral("casdap power: no output release within %1 ms, switching").arg(bound)));
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
    const QByteArray previous = qgetenv("CASDAP_SYSTEMCTL_BIN");
    qputenv("CASDAP_SYSTEMCTL_BIN", restartStub.toUtf8());
    QVERIFY(writeStub(QStringLiteral("systemctl"), QStringLiteral("1")));
    auto restore = qScopeGuard([this, previous] {
        qputenv("CASDAP_SYSTEMCTL_BIN", previous);
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
        QTRY_VERIFY_WITH_TIMEOUT(rig.codec.restarting(), busTimeoutMs);
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
        QTRY_VERIFY_WITH_TIMEOUT(!rig.codec.restarting(), busTimeoutMs);
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
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));
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
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));
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
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-switch-back.service\n"));
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
    QVERIFY(!m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 1, busTimeoutMs);

    rig.mode->setLevel(QStringLiteral("bitperfect"));
    QVERIFY(m_bridge->runVerb(QStringLiteral("game-mode")));
    QVERIFY(m_bridge->switching());
    QTRY_COMPARE_WITH_TIMEOUT(logLines(), 2, busTimeoutMs);
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, busTimeoutMs);
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

QTEST_GUILESS_MAIN(PowerTest)
#include "tst_power.moc"
