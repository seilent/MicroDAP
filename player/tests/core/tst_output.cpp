#include "OutputControl.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;

const char *statusText =
    "PipeWire 'pipewire-0' [1.6.9, armada@armada, cookie:1]\n"
    " └─ Clients:\n"
    "        33. WirePlumber                         [1.6.9, armada@armada, pid:1234]\n"
    "\n"
    "Audio\n"
    " ├─ Devices:\n"
    " │      42. alsa_card.platform-sound            [alsa]\n"
    " │\n"
    " ├─ Sinks:\n"
    " │  *   47. alsa_output.platform-sound.HiFi__Speaker__sink    [vol: 0.20]\n"
    " │      48. alsa_output.platform-sound.HiFi__Headphones__sink [vol: 1.00]\n"
    " │\n"
    " ├─ Sources:\n"
    " │      49. alsa_input.platform-sound.HiFi__Mic__source       [vol: 1.00]\n";

const char *jackOn =
    "numid=19,iface=MIXER,name='HPHL Volume'\n"
    "  ; type=INTEGER,access=rw---R--,values=1,min=0,max=24,step=0\n"
    "  : values=20\n"
    "numid=126,iface=CARD,name='Mic Jack'\n"
    "  ; type=BOOLEAN,access=r-------,values=1\n"
    "  : values=off\n"
    "numid=127,iface=CARD,name='Headphone Jack'\n"
    "  ; type=BOOLEAN,access=r-------,values=1\n"
    "  : values=on\n";

const char *jackOff =
    "numid=126,iface=CARD,name='Mic Jack'\n"
    "  ; type=BOOLEAN,access=r-------,values=1\n"
    "  : values=on\n"
    "numid=127,iface=CARD,name='Headphone Jack'\n"
    "  ; type=BOOLEAN,access=r-------,values=1\n"
    "  : values=off\n";

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

class OutputTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void parsesStatus();
    void parsesJack();
    void speakerAndHeadphones();
    void autoClearsPin();
    void missingSink();
    void jackPolling();
    void missingBinaries();

private:
    QTemporaryDir m_dir;
    QString m_bin;
    QString m_log;
    QString m_statusFile;
    QString m_jackFile;
};

void OutputTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_bin = m_dir.path() + QStringLiteral("/bin");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    m_statusFile = m_dir.path() + QStringLiteral("/status.txt");
    m_jackFile = m_dir.path() + QStringLiteral("/jack.txt");
    const QString logLine = QStringLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                            + QStringLiteral("\"\n");
    QVERIFY(writeFile(m_bin + QStringLiteral("/wpctl"),
                      QStringLiteral("#!/bin/sh\n") + logLine
                          + QStringLiteral("[ \"$1\" = status ] && cat \"") + m_statusFile + QStringLiteral("\"\nexit 0\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/pw-metadata"), QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit 0\n"), true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/amixer"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("cat \"") + m_jackFile + QStringLiteral("\"\n"),
                      true));
    QVERIFY(writeFile(m_statusFile, QString::fromUtf8(statusText)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    qputenv("CASDAP_WPCTL_BIN", (m_bin + QStringLiteral("/wpctl")).toUtf8());
    qputenv("CASDAP_PW_METADATA_BIN", (m_bin + QStringLiteral("/pw-metadata")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
    QCOMPARE(OutputControl::wpctlPath(), m_bin + QStringLiteral("/wpctl"));
    QCOMPARE(OutputControl::pwMetadataPath(), m_bin + QStringLiteral("/pw-metadata"));
    QCOMPARE(OutputControl::amixerPath(), m_bin + QStringLiteral("/amixer"));
}

void OutputTest::init()
{
    QFile::remove(m_log);
    QVERIFY(writeFile(m_statusFile, QString::fromUtf8(statusText)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
}

void OutputTest::parsesStatus()
{
    const QString status = QString::fromUtf8(statusText);
    QCOMPARE(OutputControl::sinkId(status, OutputControl::speakerSink()), 47);
    QCOMPARE(OutputControl::sinkId(status, OutputControl::headphonesSink()), 48);
    QCOMPARE(OutputControl::sinkId(status, QStringLiteral("alsa_output.platform-sound.HiFi__Speaker")), -1);
    QCOMPARE(OutputControl::sinkId(status, QStringLiteral("alsa_output.usb")), -1);
    QCOMPARE(OutputControl::sinkId(QString(), OutputControl::speakerSink()), -1);
    QCOMPARE(OutputControl::sinkFor(QStringLiteral("speaker")), OutputControl::speakerSink());
    QCOMPARE(OutputControl::sinkFor(QStringLiteral("headphones")), OutputControl::headphonesSink());
    QCOMPARE(OutputControl::sinkFor(QStringLiteral("auto")), QString());
}

void OutputTest::parsesJack()
{
    QVERIFY(OutputControl::parseJack(QString::fromUtf8(jackOn)));
    QVERIFY(!OutputControl::parseJack(QString::fromUtf8(jackOff)));
    QVERIFY(!OutputControl::parseJack(QString()));
    QVERIFY(!OutputControl::parseJack(QStringLiteral("amixer: Cannot find the given element\n")));
}

void OutputTest::speakerAndHeadphones()
{
    OutputControl control;
    QCOMPARE(control.output(), QStringLiteral("auto"));
    QVERIFY(!control.busy());
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy outputSpy(&control, &OutputControl::outputChanged);

    control.apply(QStringLiteral("speaker"));
    QVERIFY(control.busy());
    QCOMPARE(outputSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("speaker"));
    QVERIFY(applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QTRY_VERIFY_WITH_TIMEOUT(readFile(m_log).contains(QStringLiteral("amixer -c0 contents\n")), waitMs);
    QString log = readFile(m_log);
    QVERIFY2(log.startsWith(QStringLiteral("wpctl status --name\nwpctl set-default 47\n")), qPrintable(log));
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);

    QFile::remove(m_log);
    control.apply(QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QVERIFY(applied.at(1).at(1).toBool());
    log = readFile(m_log);
    QVERIFY2(log.startsWith(QStringLiteral("wpctl status --name\nwpctl set-default 48\n")), qPrintable(log));
    QCOMPARE(control.output(), QStringLiteral("headphones"));

    QFile::remove(m_log);
    control.apply(QStringLiteral("headphones"));
    QCOMPARE(outputSpy.count(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QVERIFY(readFile(m_log).contains(QStringLiteral("wpctl set-default 48\n")));
}

void OutputTest::autoClearsPin()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(applied.at(0).at(1).toBool());
    const QString log = readFile(m_log);
    QVERIFY2(log.startsWith(QStringLiteral("pw-metadata -d 0 default.configured.audio.sink\n")), qPrintable(log));
    QVERIFY(!log.contains(QStringLiteral("wpctl")));
}

void OutputTest::missingSink()
{
    QVERIFY(writeFile(m_statusFile, QStringLiteral("Audio\n ├─ Sinks:\n │  *   50. alsa_output.usb-DAC.analog-stereo [vol: 1.00]\n")));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QTest::ignoreMessage(QtWarningMsg, "casdap output: sink not listed \"alsa_output.platform-sound.HiFi__Speaker__sink\"");
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(!applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QCOMPARE(control.output(), QStringLiteral("speaker"));
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default")));
}

void OutputTest::jackPolling()
{
    OutputControl control;
    QSignalSpy jack(&control, &OutputControl::jackPresentChanged);
    QVERIFY(!control.jackPresent());
    control.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);
    QCOMPARE(jack.count(), 1);

    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    control.refreshJack();
    QTRY_VERIFY_WITH_TIMEOUT(!control.jackPresent(), waitMs);
    QCOMPARE(jack.count(), 2);

    control.setActive(false);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    QTest::qWait(2500);
    QVERIFY(!control.jackPresent());
    QCOMPARE(jack.count(), 2);

    control.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);
}

void OutputTest::missingBinaries()
{
    qputenv("CASDAP_WPCTL_BIN", (m_dir.path() + QStringLiteral("/missing/wpctl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_dir.path() + QStringLiteral("/missing/amixer")).toUtf8());
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(!applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    control.refreshJack();
    QTest::qWait(200);
    QVERIFY(!control.jackPresent());
    qputenv("CASDAP_WPCTL_BIN", (m_bin + QStringLiteral("/wpctl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
}

QTEST_GUILESS_MAIN(OutputTest)

#include "tst_output.moc"
