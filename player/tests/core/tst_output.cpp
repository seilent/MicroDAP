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

const char *usbName = "alsa_output.usb-Apple__Inc._USB-C_to_3.5mm_Headphone_Jack_Adapter_DWH345606BLJKLTA5-00.analog-stereo";

const char *sinksBuiltIn =
    "47\talsa_output.platform-sound.HiFi__Speaker__sink\tPipeWire\ts24le 2ch 48000Hz\tRUNNING\n"
    "48\talsa_output.platform-sound.HiFi__Headphones__sink\tPipeWire\ts24le 2ch 48000Hz\tSUSPENDED\n";

const char *sinksUsb =
    "47\talsa_output.platform-sound.HiFi__Speaker__sink\tPipeWire\ts24le 2ch 48000Hz\tRUNNING\n"
    "48\talsa_output.platform-sound.HiFi__Headphones__sink\tPipeWire\ts24le 2ch 48000Hz\tSUSPENDED\n"
    "51\tbluez_output.BC_27_7A_6F_A9_6F.1\tPipeWire\ts16le 2ch 48000Hz\tSUSPENDED\n"
    "52\talsa_input.usb-Apple__Inc._USB-C_to_3.5mm_Headphone_Jack_Adapter_DWH345606BLJKLTA5-00.mono-fallback\tPipeWire\ts16le 1ch 48000Hz\tSUSPENDED\n"
    "54\talsa_output.usb-Apple__Inc._USB-C_to_3.5mm_Headphone_Jack_Adapter_DWH345606BLJKLTA5-00.analog-stereo\tPipeWire\ts24le 2ch 48000Hz\tSUSPENDED\n";

const char *sinksUsbOnly =
    "54\talsa_output.usb-DAC.analog-stereo\tPipeWire\ts16le 2ch 48000Hz\tSUSPENDED\n";

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

QString setDefault(const QString &sink)
{
    return QStringLiteral("wpctl set-default ") + sink + QStringLiteral("\n");
}

}

class OutputTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void parsesSinks();
    void parsesJack();
    void resolves();
    void autoPicksUsb();
    void autoHeadphonesThenSpeaker();
    void usbSavedButAbsent();
    void speakerAndHeadphonesForced();
    void missingSink();
    void usbEdgeReappliesAndGatesHeadphoneWords();
    void jackEdgeReapplies();
    void headphoneWordsDirect();
    void polling();
    void speakerGainWords();
    void missingBinaries();

private:
    QTemporaryDir m_dir;
    QString m_bin;
    QString m_log;
    QString m_sinksFile;
    QString m_jackFile;
};

void OutputTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_bin = m_dir.path() + QStringLiteral("/bin");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    m_sinksFile = m_dir.path() + QStringLiteral("/sinks.txt");
    m_jackFile = m_dir.path() + QStringLiteral("/jack.txt");
    const QString logLine = QStringLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                            + QStringLiteral("\"\n");
    QVERIFY(writeFile(m_bin + QStringLiteral("/wpctl"), QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit 0\n"), true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/pactl"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("cat \"") + m_sinksFile + QStringLiteral("\"\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/systemctl"), QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit 0\n"), true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/amixer"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("cat \"") + m_jackFile + QStringLiteral("\"\n"),
                      true));
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    qputenv("CASDAP_WPCTL_BIN", (m_bin + QStringLiteral("/wpctl")).toUtf8());
    qputenv("CASDAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
    qputenv("CASDAP_SYSTEMCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
    QCOMPARE(OutputControl::wpctlPath(), m_bin + QStringLiteral("/wpctl"));
    QCOMPARE(OutputControl::pactlPath(), m_bin + QStringLiteral("/pactl"));
    QCOMPARE(OutputControl::amixerPath(), m_bin + QStringLiteral("/amixer"));
    QCOMPARE(OutputControl::systemctlPath(), m_bin + QStringLiteral("/systemctl"));
}

void OutputTest::init()
{
    QFile::remove(m_log);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
}

void OutputTest::parsesSinks()
{
    const QStringList builtIn = OutputControl::parseSinks(QString::fromUtf8(sinksBuiltIn));
    QCOMPARE(builtIn, QStringList({OutputControl::speakerSink(), OutputControl::headphonesSink()}));
    QCOMPARE(OutputControl::usbSinkIn(builtIn), QString());
    const QStringList usb = OutputControl::parseSinks(QString::fromUtf8(sinksUsb));
    QCOMPARE(usb.size(), 5);
    QCOMPARE(OutputControl::usbSinkIn(usb), QString::fromUtf8(usbName));
    QCOMPARE(OutputControl::usbSinkIn(OutputControl::parseSinks(QString::fromUtf8(sinksUsbOnly))), QStringLiteral("alsa_output.usb-DAC.analog-stereo"));
    QCOMPARE(OutputControl::parseSinks(QString()), QStringList());
    QCOMPARE(OutputControl::parseSinks(QStringLiteral("Connection failure: Connection refused\n")), QStringList());
    QCOMPARE(OutputControl::outputs(), QStringList({QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb")}));
    QCOMPARE(OutputControl::unitFor(QStringLiteral("hp-gain-low")), QStringLiteral("casdap-priv@hp-gain-low.service"));
}

void OutputTest::parsesJack()
{
    QVERIFY(OutputControl::parseJack(QString::fromUtf8(jackOn)));
    QVERIFY(!OutputControl::parseJack(QString::fromUtf8(jackOff)));
    QVERIFY(!OutputControl::parseJack(QString()));
    QVERIFY(!OutputControl::parseJack(QStringLiteral("amixer: Cannot find the given element\n")));
}

void OutputTest::resolves()
{
    QCOMPARE(OutputControl::resolve(QStringLiteral("auto"), true, true), QStringLiteral("usb"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("auto"), false, true), QStringLiteral("usb"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("auto"), true, false), QStringLiteral("headphones"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("auto"), false, false), QStringLiteral("speaker"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("usb"), true, true), QStringLiteral("usb"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("usb"), false, true), QStringLiteral("usb"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("usb"), true, false), QStringLiteral("headphones"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("usb"), false, false), QStringLiteral("speaker"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("speaker"), true, true), QStringLiteral("speaker"));
    QCOMPARE(OutputControl::resolve(QStringLiteral("headphones"), false, true), QStringLiteral("headphones"));
}

void OutputTest::autoPicksUsb()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    OutputControl control;
    QCOMPARE(control.output(), QStringLiteral("auto"));
    QCOMPARE(control.activeOutput(), QString());
    QVERIFY(!control.usbPresent());
    QVERIFY(!control.busy());
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy usbSpy(&control, &OutputControl::usbSinkChanged);
    QSignalSpy activeSpy(&control, &OutputControl::activeOutputChanged);

    control.apply(QStringLiteral("auto"));
    QVERIFY(control.busy());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("auto"));
    QVERIFY(applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QVERIFY(control.jackPresent());
    QVERIFY(control.usbPresent());
    QCOMPARE(control.usbSink(), QString::fromUtf8(usbName));
    QCOMPARE(usbSpy.count(), 1);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QCOMPARE(activeSpy.count(), 1);
    const QString log = readFile(m_log);
    QCOMPARE(log, QStringLiteral("amixer -c0 contents\npactl list sinks short\n") + setDefault(QString::fromUtf8(usbName)));
}

void OutputTest::autoHeadphonesThenSpeaker()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(applied.at(0).at(1).toBool());
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QVERIFY(!control.usbPresent());
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::headphonesSink())));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QVERIFY(applied.at(1).at(1).toBool());
    QVERIFY(!control.jackPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::speakerSink())));
    QVERIFY(!readFile(m_log).contains(QStringLiteral("systemctl")));
}

void OutputTest::usbSavedButAbsent()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy outputSpy(&control, &OutputControl::outputChanged);
    control.apply(QStringLiteral("usb"));
    QCOMPARE(outputSpy.count(), 1);
    QCOMPARE(control.output(), QStringLiteral("usb"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("usb"));
    QVERIFY(applied.at(0).at(1).toBool());
    QVERIFY(!control.usbPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::headphonesSink())));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    control.apply(QStringLiteral("usb"));
    QCOMPARE(outputSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::speakerSink())));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    control.apply(QStringLiteral("usb"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QVERIFY(control.usbPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QVERIFY(readFile(m_log).endsWith(setDefault(QString::fromUtf8(usbName))));
}

void OutputTest::speakerAndHeadphonesForced()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy outputSpy(&control, &OutputControl::outputChanged);

    control.apply(QStringLiteral("speaker"));
    QCOMPARE(outputSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("speaker"));
    QVERIFY(applied.at(0).at(1).toBool());
    QVERIFY(control.usbPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::speakerSink())));

    QFile::remove(m_log);
    control.apply(QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QVERIFY(applied.at(1).at(1).toBool());
    QVERIFY(!control.jackPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::headphonesSink())));

    QFile::remove(m_log);
    control.apply(QStringLiteral("headphones"));
    QCOMPARE(outputSpy.count(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QVERIFY(readFile(m_log).endsWith(setDefault(OutputControl::headphonesSink())));

    QTest::ignoreMessage(QtWarningMsg, "casdap output: unknown output \"bt\"");
    control.apply(QStringLiteral("bt"));
    QCOMPARE(control.output(), QStringLiteral("headphones"));
}

void OutputTest::missingSink()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsbOnly)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QTest::ignoreMessage(QtWarningMsg, "casdap output: sink not listed \"alsa_output.platform-sound.HiFi__Speaker__sink\"");
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(!applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QCOMPARE(control.output(), QStringLiteral("speaker"));
    QCOMPARE(control.activeOutput(), QString());
    QVERIFY(control.usbPresent());
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default")));

    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QVERIFY(applied.at(1).at(1).toBool());
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
}

void OutputTest::usbEdgeReappliesAndGatesHeadphoneWords()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy usbSpy(&control, &OutputControl::usbSinkChanged);

    control.setHeadphone(QStringLiteral("low"), QStringLiteral("ulp"));
    QCOMPARE(control.hpGain(), QStringLiteral("low"));
    QCOMPARE(control.hpAmp(), QStringLiteral("ulp"));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
    QCOMPARE(applied.count(), 0);

    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("systemctl")));

    QFile::remove(m_log);
    control.setHeadphone(QStringLiteral("high"), QStringLiteral("hifi"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QVERIFY(!QFile::exists(m_log));

    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(usbSpy.count(), 2);
    QVERIFY(!control.usbPresent());
    QCOMPARE(control.output(), QStringLiteral("auto"));
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("auto"));
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("hp-gain-high"));
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("hp-amp-hifi"));
    QVERIFY(applied.at(2).at(1).toBool());
    QVERIFY(applied.at(3).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    const QString log = readFile(m_log);
    QVERIFY2(log.contains(setDefault(OutputControl::headphonesSink())
                          + QStringLiteral("systemctl start casdap-priv@hp-gain-high.service\nsystemctl start casdap-priv@hp-amp-hifi.service\n")),
             qPrintable(log));

    QFile::remove(m_log);
    control.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(readFile(m_log).contains(QStringLiteral("pactl list sinks short\n")), waitMs);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 4);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default")));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 5);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("systemctl")));
}

void OutputTest::jackEdgeReapplies()
{
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.setHeadphone(QStringLiteral("low"), QStringLiteral("lohifi"));
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QVERIFY(!readFile(m_log).contains(QStringLiteral("systemctl")));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    control.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("auto"));
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("hp-gain-low"));
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("hp-amp-lohifi"));
    QVERIFY(readFile(m_log).contains(setDefault(OutputControl::headphonesSink())));

    QFile::remove(m_log);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("systemctl")));
}

void OutputTest::headphoneWordsDirect()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));

    QFile::remove(m_log);
    control.setHeadphone(QStringLiteral("normal"), QStringLiteral("lohifi"));
    QVERIFY(control.busy());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("hp-gain-normal"));
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("hp-amp-lohifi"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-priv@hp-gain-normal.service\nsystemctl start casdap-priv@hp-amp-lohifi.service\n"));

    QFile::remove(m_log);
    control.apply(QStringLiteral("headphones"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 6, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("headphones"));
    QCOMPARE(applied.at(4).at(0).toString(), QStringLiteral("hp-gain-normal"));
    QCOMPARE(applied.at(5).at(0).toString(), QStringLiteral("hp-amp-lohifi"));

    QFile::remove(m_log);
    QTest::ignoreMessage(QtWarningMsg, "casdap output: unknown headphone setting \"max\" \"lohifi\"");
    control.setHeadphone(QStringLiteral("max"), QStringLiteral("lohifi"));
    QCOMPARE(control.hpGain(), QStringLiteral("normal"));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
}

void OutputTest::polling()
{
    OutputControl control;
    QSignalSpy jack(&control, &OutputControl::jackPresentChanged);
    QSignalSpy applied(&control, &OutputControl::applied);
    QVERIFY(!control.jackPresent());
    control.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);
    QCOMPARE(jack.count(), 1);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 0);

    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOff)));
    control.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(!control.jackPresent(), waitMs);
    QCOMPARE(jack.count(), 2);
    QCOMPARE(applied.count(), 0);

    control.setActive(false);
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    QTest::qWait(2500);
    QVERIFY(!control.jackPresent());
    QCOMPARE(jack.count(), 2);

    control.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);

    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QFile::remove(m_log);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QVERIFY(readFile(m_log).contains(setDefault(QString::fromUtf8(usbName))));
    control.setActive(false);
}

void OutputTest::speakerGainWords()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy busy(&control, &OutputControl::busyChanged);
    QCOMPARE(control.spkGain(), 0);
    control.setSpkGain(90);
    QCOMPARE(control.spkGain(), 90);
    QVERIFY(control.busy());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("spk-gain-90"));
    QVERIFY(applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-priv@spk-gain-90.service\n"));

    QFile::remove(m_log);
    control.setSpkGain(90);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl start casdap-priv@spk-gain-90.service\n"));

    QFile::remove(m_log);
    QTest::ignoreMessage(QtWarningMsg, "casdap output: unknown speaker gain 91");
    control.setSpkGain(91);
    QCOMPARE(control.spkGain(), 90);
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
    QCOMPARE(busy.count(), 4);
}

void OutputTest::missingBinaries()
{
    qputenv("CASDAP_WPCTL_BIN", (m_dir.path() + QStringLiteral("/missing/wpctl")).toUtf8());
    qputenv("CASDAP_PACTL_BIN", (m_dir.path() + QStringLiteral("/missing/pactl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_dir.path() + QStringLiteral("/missing/amixer")).toUtf8());
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(!applied.at(0).at(1).toBool());
    QVERIFY(!control.busy());
    QCOMPARE(control.activeOutput(), QString());
    control.refresh();
    QTest::qWait(200);
    QVERIFY(!control.jackPresent());
    QVERIFY(!control.usbPresent());
    qputenv("CASDAP_WPCTL_BIN", (m_bin + QStringLiteral("/wpctl")).toUtf8());
    qputenv("CASDAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("CASDAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
}

QTEST_GUILESS_MAIN(OutputTest)

#include "tst_output.moc"
