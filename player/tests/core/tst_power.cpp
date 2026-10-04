#include "PowerBridge.h"
#include "Settings.h"
#include "StatusModel.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QtTest>

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
    void registered();
    void screenOff();
    void powerMenu();
    void volume();
    void verbs();
    void drift();
    void settingsPersist();
    void statusSysfs();
    void clockFormat();

private:
    bool sawChange(const QString &name, const QVariant &value) const;
    bool writeStub(const QString &name);
    int logLines() const;

    QTemporaryDir m_dir;
    QString m_stubDir;
    QString m_emptyDir;
    QString m_log;
    QString m_driftFile;
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

bool PowerTest::writeStub(const QString &name)
{
    const QString script = QStringLiteral("#!/bin/sh\nprintf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"")
                           + m_log + QStringLiteral("\"\n");
    return writeFile(m_stubDir + QLatin1Char('/') + name, script, true);
}

int PowerTest::logLines() const
{
    return static_cast<int>(readFile(m_log).count(QLatin1Char('\n')));
}

void PowerTest::initTestCase()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus, run under dbus-run-session");

    QVERIFY(m_dir.isValid());
    m_stubDir = m_dir.path() + QStringLiteral("/bin");
    m_emptyDir = m_dir.path() + QStringLiteral("/empty");
    QVERIFY(QDir().mkpath(m_emptyDir));
    m_log = m_dir.path() + QStringLiteral("/verbs.log");
    m_driftFile = m_dir.path() + QStringLiteral("/drift");
    QVERIFY(writeStub(QStringLiteral("casdap-power")));
    QVERIFY(writeStub(QStringLiteral("systemctl")));
    QVERIFY(writeFile(m_driftFile, QStringLiteral("/usr/libexec/armada/session-control\n\n")));
    qputenv("CASDAP_DRIFT_FILE", m_driftFile.toUtf8());
    qputenv("CASDAP_VERB_DIR", m_stubDir.toUtf8());

    m_bridge = std::make_unique<PowerBridge>(nullptr, bus);
    QVERIFY(m_bridge->isRegistered());

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

    QCOMPARE(m_remote->property("PlaybackStatus").toString(), QStringLiteral("Stopped"));
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

    qputenv("CASDAP_VERB_DIR", m_emptyDir.toUtf8());
    QVERIFY(m_bridge->runVerb(QStringLiteral("screen-off")));
    QVERIFY(m_bridge->runVerb(QStringLiteral("screen-off")));
    QCOMPARE(logLines(), 4);
    qputenv("CASDAP_VERB_DIR", m_stubDir.toUtf8());
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
        QCOMPARE(settings.output(), QStringLiteral("speaker"));
        QCOMPARE(settings.roots(), Settings::defaultRoots());
        QCOMPARE(settings.accent(), QStringLiteral("#FA2A55"));
        QCOMPARE(settings.scale(), 1.0);
        QVERIFY(!settings.pocketControls());
        QCOMPARE(settings.saverSeconds(), 30);

        QSignalSpy scaleSpy(&settings, &Settings::scaleChanged);
        settings.setScale(1.1);
        QCOMPARE(scaleSpy.count(), 1);
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
        QCOMPARE(settings.output(), QStringLiteral("headphones"));

        settings.setAccent(QStringLiteral("#1ba1e2"));
        QCOMPARE(settings.accent(), QStringLiteral("#1BA1E2"));
        settings.setAccent(QStringLiteral("#123456"));
        QCOMPARE(settings.accent(), QStringLiteral("#1BA1E2"));

        settings.setPocketControls(true);
        settings.setSaverSeconds(5);
        QCOMPARE(settings.saverSeconds(), Settings::minSaverSeconds);
        settings.setSaverSeconds(45);
        settings.setRoots({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")});
    }
    QVERIFY(QFile::exists(path));

    Settings again(path);
    QCOMPARE(again.output(), QStringLiteral("headphones"));
    QCOMPARE(again.roots(), QStringList({QStringLiteral("/tmp/a"), QStringLiteral("/tmp/b")}));
    QCOMPARE(again.accent(), QStringLiteral("#1BA1E2"));
    QCOMPARE(again.scale(), 1.1);
    QVERIFY(again.pocketControls());
    QCOMPARE(again.saverSeconds(), 45);
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

QTEST_GUILESS_MAIN(PowerTest)
#include "tst_power.moc"
