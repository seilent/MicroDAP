#include "Eq.h"
#include "OutputControl.h"
#include "Settings.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace microdap;

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

}

class OutputTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void parsesSinks();
    void parsesJack();
    void outputOf();
    void applyReadsDefault();
    void defaultFollowed();
    void unreadableDefault();
    void polling();
    void missingBinaries();
    void heldKeepsUsb();
    void returningKeepsUsb();
    void watchedCardGone();
    void watchedCardPolled();
    void rereadDuringRead();
    void eqLoadsResolvedOutput();
    void eqFailedResolveLoadsSinkDefault();
    void eqMissingPactlLoadsSpeaker();
    void eqHangingSinkLoadsSpeaker();
    void jackEventLosesRoute();
    void usbSinkLosesRoute();

private:
    int count(const QString &line) const;
    bool setDefaultSink(const QString &sink);

    QTemporaryDir m_dir;
    QString m_bin;
    QString m_log;
    QString m_sinksFile;
    QString m_jackFile;
    QString m_cardIdFile;
    QString m_gateFile;
    QString m_defaultFile;
    QString m_defaultGateFile;
};

void OutputTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_bin = m_dir.path() + QStringLiteral("/bin");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    m_sinksFile = m_dir.path() + QStringLiteral("/sinks.txt");
    m_jackFile = m_dir.path() + QStringLiteral("/jack.txt");
    m_cardIdFile = m_dir.path() + QStringLiteral("/asound/card1/id");
    m_gateFile = m_dir.path() + QStringLiteral("/gate");
    m_defaultFile = m_dir.path() + QStringLiteral("/default.txt");
    m_defaultGateFile = m_dir.path() + QStringLiteral("/default-gate");
    const QString logLine = QStringLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                            + QStringLiteral("\"\n");
    QVERIFY(writeFile(m_bin + QStringLiteral("/pactl"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("[ \"$1\" = list ] && cat \"") + m_sinksFile
                          + QStringLiteral("\"\n[ \"$1\" = get-default-sink ] && cat \"") + m_defaultFile
                          + QStringLiteral("\"\nexit 0\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/slow/pactl"),
                      QStringLiteral("#!/bin/sh\nif [ \"$1\" = list ]; then\nout=$(cat \"") + m_sinksFile
                          + QStringLiteral("\")\n") + logLine
                          + QStringLiteral("while [ -f \"") + m_gateFile
                          + QStringLiteral("\" ]; do sleep 0.05; done\nprintf '%s\\n' \"$out\"\nexit 0\nfi\n") + logLine
                          + QStringLiteral("if [ \"$1\" = get-default-sink ]; then\nwhile [ -f \"") + m_defaultGateFile
                          + QStringLiteral("\" ]; do sleep 0.05; done\ncat \"") + m_defaultFile
                          + QStringLiteral("\"\nfi\nexit 0\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/amixer"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("cat \"") + m_jackFile + QStringLiteral("\"\n"),
                      true));
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("MICRODAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
    qputenv("MICRODAP_ASOUND_DIR", (m_dir.path() + QStringLiteral("/asound")).toUtf8());
    qputenv("MICRODAP_DEV_INPUT", QFile::encodeName(m_dir.path() + QStringLiteral("/dev")));
    qputenv("MICRODAP_SYS_INPUT", QFile::encodeName(m_dir.path() + QStringLiteral("/sys")));
    QCOMPARE(OutputControl::pactlPath(), m_bin + QStringLiteral("/pactl"));
    QCOMPARE(OutputControl::amixerPath(), m_bin + QStringLiteral("/amixer"));
}

void OutputTest::init()
{
    QFile::remove(m_log);
    QFile::remove(m_gateFile);
    QFile::remove(m_defaultGateFile);
    QVERIFY(setDefaultSink(OutputControl::speakerSink()));
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(writeFile(m_jackFile, QString::fromUtf8(jackOn)));
    QVERIFY(writeFile(m_cardIdFile, QStringLiteral("B\n")));
    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
}

int OutputTest::count(const QString &line) const
{
    return readFile(m_log).count(line);
}

bool OutputTest::setDefaultSink(const QString &sink)
{
    return writeFile(m_defaultFile, sink.isEmpty() ? QString() : sink + QStringLiteral("\n"));
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
    QCOMPARE(OutputControl::outputs(), QStringList({QStringLiteral("auto"), QStringLiteral("speaker"), QStringLiteral("headphones"), QStringLiteral("usb"), QStringLiteral("bt")}));
}

void OutputTest::parsesJack()
{
    QVERIFY(OutputControl::parseJack(QString::fromUtf8(jackOn)));
    QVERIFY(!OutputControl::parseJack(QString::fromUtf8(jackOff)));
    QVERIFY(!OutputControl::parseJack(QString()));
    QVERIFY(!OutputControl::parseJack(QStringLiteral("amixer: Cannot find the given element\n")));
}

void OutputTest::outputOf()
{
    QCOMPARE(OutputControl::outputOf(OutputControl::speakerSink()), QStringLiteral("speaker"));
    QCOMPARE(OutputControl::outputOf(OutputControl::headphonesSink()), QStringLiteral("headphones"));
    QCOMPARE(OutputControl::outputOf(QString::fromUtf8(usbName)), QStringLiteral("usb"));
    QCOMPARE(OutputControl::outputOf(QStringLiteral("bluez_output.BC_27_7A_6F_A9_6F.1")), QStringLiteral("bt"));
    QCOMPARE(OutputControl::outputOf(QStringLiteral("alsa_output.platform-sound.HiFi__HDMI1__sink")), QStringLiteral("speaker"));
    QCOMPARE(OutputControl::outputOf(QString()), QString());
}

void OutputTest::applyReadsDefault()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    OutputControl control;
    QCOMPARE(control.output(), QStringLiteral("auto"));
    QCOMPARE(control.activeOutput(), QString());
    QVERIFY(!control.usbPresent());
    QVERIFY(!control.busy());
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy first(&control, &OutputControl::firstApplied);
    QSignalSpy usbSpy(&control, &OutputControl::usbSinkChanged);
    QSignalSpy activeSpy(&control, &OutputControl::activeOutputChanged);

    control.apply(QStringLiteral("usb"));
    QVERIFY(control.busy());
    QCOMPARE(control.output(), QStringLiteral("usb"));
    QTest::qWait(200);
    QCOMPARE(readFile(m_log), QString());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("usb"));
    QVERIFY(applied.at(0).at(1).toBool());
    QCOMPARE(first.count(), 1);
    QVERIFY(first.at(0).at(0).toBool());
    QVERIFY(!control.busy());
    QVERIFY(control.jackPresent());
    QVERIFY(control.usbPresent());
    QCOMPARE(control.usbSink(), QString::fromUtf8(usbName));
    QCOMPARE(control.defaultSink(), QString::fromUtf8(usbName));
    QCOMPARE(usbSpy.count(), 1);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(readFile(m_log),
             QStringLiteral("amixer -c0 contents\npactl list sinks short\npactl get-default-sink\n"));

    QTest::ignoreMessage(QtWarningMsg, "microdap output: unknown output \"tape\"");
    control.apply(QStringLiteral("tape"));
    QVERIFY(!control.busy());
    QCOMPARE(control.output(), QStringLiteral("usb"));
    QVERIFY(setDefaultSink(QStringLiteral("bluez_output.BC_27_7A_6F_A9_6F.1")));
    control.apply(QStringLiteral("bt"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("bt"));
    QCOMPARE(first.count(), 1);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default-sink")));
}

void OutputTest::defaultFollowed()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));
    QSignalSpy activeSpy(&control, &OutputControl::activeOutputChanged);

    QVERIFY(setDefaultSink(OutputControl::headphonesSink()));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("headphones"), waitMs);
    QCOMPARE(activeSpy.count(), 1);
    QCOMPARE(applied.count(), 1);

    QVERIFY(setDefaultSink(QString()));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("pactl get-default-sink\n")), 3, waitMs);
    QTest::qWait(200);
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default-sink")));
}

void OutputTest::unreadableDefault()
{
    QVERIFY(setDefaultSink(QString()));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    QSignalSpy first(&control, &OutputControl::firstApplied);
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(!applied.at(0).at(1).toBool());
    QCOMPARE(first.count(), 1);
    QVERIFY(!first.at(0).at(0).toBool());
    QCOMPARE(control.activeOutput(), QString());
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
    QCOMPARE(control.activeOutput(), QString());

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

    QVERIFY(setDefaultSink(OutputControl::headphonesSink()));
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("usb"), waitMs);
    QCOMPARE(applied.count(), 1);
    QVERIFY(control.usbPresent());
    control.setActive(false);
}

void OutputTest::missingBinaries()
{
    qputenv("MICRODAP_PACTL_BIN", (m_dir.path() + QStringLiteral("/missing/pactl")).toUtf8());
    qputenv("MICRODAP_AMIXER_BIN", (m_dir.path() + QStringLiteral("/missing/amixer")).toUtf8());
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
    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/pactl")).toUtf8());
    qputenv("MICRODAP_AMIXER_BIN", (m_bin + QStringLiteral("/amixer")).toUtf8());
}

void OutputTest::heldKeepsUsb()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QCOMPARE(control.held(), HeldOutputPort::HeldState::None);
    QSignalSpy usbSpy(&control, &OutputControl::usbSinkChanged);
    QSignalSpy activeSpy(&control, &OutputControl::activeOutputChanged);

    control.setHeld(HeldOutputPort::HeldState::Held);
    QCOMPARE(control.held(), HeldOutputPort::HeldState::Held);
    QCOMPARE(usbSpy.count(), 0);
    QFile::remove(m_log);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(setDefaultSink(OutputControl::speakerSink()));
    control.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(readFile(m_log).contains(QStringLiteral("pactl get-default-sink\n")), waitMs);
    QTest::qWait(300);
    QVERIFY(control.usbPresent());
    QCOMPARE(control.usbSink(), QString::fromUtf8(usbName));
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QCOMPARE(usbSpy.count(), 0);
    QCOMPARE(activeSpy.count(), 0);

    control.setHeld(HeldOutputPort::HeldState::None);
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("speaker"), waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.usbPresent(), waitMs);
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default")));

    OutputControl other;
    QSignalSpy otherApplied(&other, &OutputControl::applied);
    other.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(otherApplied.count(), 1, waitMs);
    QCOMPARE(other.activeOutput(), QStringLiteral("speaker"));
    other.setHeld(HeldOutputPort::HeldState::Held);
    QCOMPARE(other.activeOutput(), QStringLiteral("usb"));
    QVERIFY(other.usbPresent());
}

void OutputTest::returningKeepsUsb()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));

    control.setHeld(HeldOutputPort::HeldState::Returning);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(setDefaultSink(OutputControl::speakerSink()));
    QFile::remove(m_log);
    control.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(readFile(m_log).contains(QStringLiteral("pactl get-default-sink\n")), waitMs);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QVERIFY(control.usbPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));

    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    control.setHeld(HeldOutputPort::HeldState::None);
    QTRY_VERIFY_WITH_TIMEOUT(count(QStringLiteral("pactl get-default-sink\n")) >= 2, waitMs);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QVERIFY(control.usbPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QVERIFY(!readFile(m_log).contains(QStringLiteral("set-default")));
    control.setActive(false);
}

void OutputTest::watchedCardGone()
{
    OutputControl control;
    QSignalSpy gone(&control, &OutputControl::cardGone);
    QFile::remove(m_cardIdFile);
    control.refresh();
    QCOMPARE(gone.count(), 0);

    QVERIFY(writeFile(m_cardIdFile, QStringLiteral("B\n")));
    control.watchCard(1, QStringLiteral("B"));
    control.refresh();
    QCOMPARE(gone.count(), 0);

    QFile::remove(m_cardIdFile);
    control.refresh();
    QCOMPARE(gone.count(), 1);
    QCOMPARE(gone.at(0).at(0).toInt(), 1);
    control.refresh();
    QCOMPARE(gone.count(), 1);

    QVERIFY(writeFile(m_cardIdFile, QStringLiteral("C\n")));
    control.watchCard(1, QStringLiteral("B"));
    control.refresh();
    QCOMPARE(gone.count(), 2);
    QCOMPARE(gone.at(1).at(0).toInt(), 1);

    control.watchCard(1, QStringLiteral("C"));
    control.refresh();
    QCOMPARE(gone.count(), 2);

    control.unwatchCard();
    QFile::remove(m_cardIdFile);
    control.refresh();
    QCOMPARE(gone.count(), 2);

    control.watchCard(2, QStringLiteral("B"));
    control.refresh();
    QCOMPARE(gone.count(), 3);
    QCOMPARE(gone.at(2).at(0).toInt(), 2);
}

void OutputTest::watchedCardPolled()
{
    OutputControl control;
    QSignalSpy gone(&control, &OutputControl::cardGone);
    control.watchCard(1, QStringLiteral("B"));
    control.setActive(true);
    QCOMPARE(gone.count(), 0);
    QFile::remove(m_cardIdFile);
    QTRY_COMPARE_WITH_TIMEOUT(gone.count(), 1, waitMs);
    QCOMPARE(gone.at(0).at(0).toInt(), 1);
    QTest::qWait(2500);
    QCOMPARE(gone.count(), 1);
    control.setActive(false);
}

void OutputTest::rereadDuringRead()
{
    OutputControl control;
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("speaker"));

    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/slow/pactl")).toUtf8());
    QVERIFY(writeFile(m_gateFile, QString()));
    QFile::remove(m_log);
    control.refresh();
    QTRY_VERIFY_WITH_TIMEOUT(readFile(m_log).contains(QStringLiteral("pactl list sinks short\n")), waitMs);
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    control.refresh();
    control.refresh();
    control.refresh();
    QVERIFY(QFile::remove(m_gateFile));
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("usb"), waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("pactl get-default-sink\n")), 2, waitMs);
    QTest::qWait(1500);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(count(QStringLiteral("pactl list sinks short\n")), 2);
    QCOMPARE(count(QStringLiteral("amixer -c0 contents\n")), 2);
}

void OutputTest::eqLoadsResolvedOutput()
{
    qputenv("MICRODAP_EQ_DIR", (m_dir.path() + QStringLiteral("/eq-resolved")).toUtf8());
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    Settings settings(nullptr);
    EqControl eq(&settings);
    OutputControl control;
    QStringList loads;
    connect(&eq, &EqControl::activeOutputChanged, this, [&eq, &loads] { loads.append(eq.activeOutput()); });

    QElapsedTimer timer;
    timer.start();
    eq.awaitOutput(&control);
    control.apply(QStringLiteral("auto"));
    QVERIFY(timer.elapsed() < 500);
    QCOMPARE(eq.activeOutput(), QString());

    QTRY_COMPARE_WITH_TIMEOUT(loads, QStringList({QStringLiteral("usb")}), waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));
    QCOMPARE(count(QStringLiteral("pactl get-default-sink\n")), 1);
}

void OutputTest::eqFailedResolveLoadsSinkDefault()
{
    qputenv("MICRODAP_EQ_DIR", (m_dir.path() + QStringLiteral("/eq-failed")).toUtf8());
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsbOnly)));
    QVERIFY(setDefaultSink(QString()));
    Settings settings(nullptr);
    EqControl eq(&settings);
    OutputControl control;
    QSignalSpy first(&control, &OutputControl::firstApplied);
    QStringList loads;
    connect(&eq, &EqControl::activeOutputChanged, this, [&eq, &loads] { loads.append(eq.activeOutput()); });

    eq.awaitOutput(&control, 60000);
    control.apply(QStringLiteral("speaker"));
    QTRY_COMPARE_WITH_TIMEOUT(first.count(), 1, waitMs);
    QVERIFY(!first.at(0).at(0).toBool());
    QTRY_COMPARE_WITH_TIMEOUT(loads, QStringList({QStringLiteral("speaker")}), waitMs);
    QCOMPARE(count(QStringLiteral("pactl get-default-sink\n")), 2);

    QVERIFY(setDefaultSink(QStringLiteral("alsa_output.usb-DAC.analog-stereo")));
    control.apply(QStringLiteral("usb"));
    QTRY_COMPARE_WITH_TIMEOUT(loads, QStringList({QStringLiteral("speaker"), QStringLiteral("usb")}), waitMs);
    QCOMPARE(first.count(), 1);
}

void OutputTest::eqMissingPactlLoadsSpeaker()
{
    qputenv("MICRODAP_EQ_DIR", (m_dir.path() + QStringLiteral("/eq-missing")).toUtf8());
    qputenv("MICRODAP_PACTL_BIN", (m_dir.path() + QStringLiteral("/missing/pactl")).toUtf8());
    Settings settings(nullptr);
    EqControl eq(&settings);
    OutputControl control;
    QStringList loads;
    connect(&eq, &EqControl::activeOutputChanged, this, [&eq, &loads] { loads.append(eq.activeOutput()); });

    eq.awaitOutput(&control, 200);
    QTRY_COMPARE_WITH_TIMEOUT(loads, QStringList({QStringLiteral("speaker")}), waitMs);
    QTest::qWait(200);
    QCOMPARE(loads, QStringList({QStringLiteral("speaker")}));
}

void OutputTest::eqHangingSinkLoadsSpeaker()
{
    qputenv("MICRODAP_EQ_DIR", (m_dir.path() + QStringLiteral("/eq-hanging")).toUtf8());
    qputenv("MICRODAP_PACTL_BIN", (m_bin + QStringLiteral("/slow/pactl")).toUtf8());
    QVERIFY(writeFile(m_defaultFile, OutputControl::headphonesSink() + QStringLiteral("\n")));
    QVERIFY(writeFile(m_defaultGateFile, QString()));
    Settings settings(nullptr);
    EqControl eq(&settings);
    OutputControl control;
    QStringList loads;
    connect(&eq, &EqControl::activeOutputChanged, this, [&eq, &loads] { loads.append(eq.activeOutput()); });

    QTest::ignoreMessage(QtWarningMsg, "microdap eq: get-default-sink timed out");
    QElapsedTimer timer;
    timer.start();
    eq.awaitOutput(&control, 200);
    QTRY_COMPARE_WITH_TIMEOUT(count(QStringLiteral("pactl get-default-sink\n")), 1, waitMs);
    QTRY_COMPARE_WITH_TIMEOUT(loads, QStringList({QStringLiteral("speaker")}), waitMs);
    QVERIFY(timer.elapsed() >= 200 + EqControl::sinkTimeoutMs);

    QVERIFY(QFile::remove(m_defaultGateFile));
    QTest::qWait(300);
    QCOMPARE(loads, QStringList({QStringLiteral("speaker")}));
    QCOMPARE(count(QStringLiteral("pactl get-default-sink\n")), 1);
}

void OutputTest::jackEventLosesRoute()
{
    const QString sys = m_dir.path() + QStringLiteral("/sys/event7/device");
    QVERIFY(QDir().mkpath(sys));
    QVERIFY(writeFile(sys + QStringLiteral("/name"), OutputControl::jackDeviceName() + QStringLiteral("\n")));
    QVERIFY(QDir().mkpath(m_dir.path() + QStringLiteral("/dev")));
    const QString fifo = m_dir.path() + QStringLiteral("/dev/event7");
    QCOMPARE(::mkfifo(QFile::encodeName(fifo).constData(), 0600), 0);
    const int writer = ::open(QFile::encodeName(fifo).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    QVERIFY(writer >= 0);
    const auto cleanup = qScopeGuard([writer, fifo, sys] {
        ::close(writer);
        QFile::remove(fifo);
        QDir(QFileInfo(sys).absolutePath()).removeRecursively();
    });
    const auto send = [writer](int value) {
        input_event events[2]{};
        events[0].type = EV_SW;
        events[0].code = SW_HEADPHONE_INSERT;
        events[0].value = value;
        events[1].type = EV_SYN;
        events[1].code = SYN_REPORT;
        return ::write(writer, events, sizeof(events)) == ssize_t(sizeof(events));
    };

    QVERIFY(setDefaultSink(OutputControl::headphonesSink()));
    OutputControl control;
    QVERIFY(control.jackEvents());
    QSignalSpy lost(&control, &OutputControl::routeLost);
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QVERIFY(control.jackPresent());
    QCOMPARE(control.activeOutput(), QStringLiteral("headphones"));

    QFile::remove(m_log);
    QElapsedTimer clock;
    clock.start();
    QVERIFY(send(0));
    QTRY_VERIFY_WITH_TIMEOUT(!control.jackPresent(), waitMs);
    QVERIFY(clock.elapsed() < 1000);
    QCOMPARE(lost.count(), 1);
    QCOMPARE(lost.at(0).at(0).toString(), QStringLiteral("headphones"));
    QCOMPARE(count(QStringLiteral("amixer")), 0);

    QVERIFY(send(1));
    QTRY_VERIFY_WITH_TIMEOUT(control.jackPresent(), waitMs);
    QVERIFY(setDefaultSink(OutputControl::speakerSink()));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("speaker"), waitMs);
    QVERIFY(send(0));
    QTRY_VERIFY_WITH_TIMEOUT(!control.jackPresent(), waitMs);
    QCOMPARE(lost.count(), 1);
}

void OutputTest::usbSinkLosesRoute()
{
    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksUsb)));
    QVERIFY(setDefaultSink(QString::fromUtf8(usbName)));
    OutputControl control;
    QSignalSpy lost(&control, &OutputControl::routeLost);
    QSignalSpy applied(&control, &OutputControl::applied);
    control.apply(QStringLiteral("auto"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(control.activeOutput(), QStringLiteral("usb"));

    QVERIFY(writeFile(m_sinksFile, QString::fromUtf8(sinksBuiltIn)));
    QVERIFY(setDefaultSink(OutputControl::speakerSink()));
    control.refresh();
    QTRY_COMPARE_WITH_TIMEOUT(control.activeOutput(), QStringLiteral("speaker"), waitMs);
    QCOMPARE(lost.count(), 1);
    QCOMPARE(lost.at(0).at(0).toString(), QStringLiteral("usb"));
    control.refresh();
    QTest::qWait(300);
    QCOMPARE(lost.count(), 1);
}

QTEST_GUILESS_MAIN(OutputTest)

#include "tst_output.moc"
