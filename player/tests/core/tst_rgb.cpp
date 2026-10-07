#include "RgbControl.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;

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

QString unit(const QString &word)
{
    return QStringLiteral("casdap-priv@") + word + QStringLiteral(".service");
}

}

class RgbTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void words();
    void artColorWords();
    void artWordHeldWhileOff();
    void restoreThenHueAtStart();
    void offAndArmada();
    void hueDebounced();
    void unchangedHueSkipped();
    void waitingHueReplaced();
    void screenOffHolds();
    void saverSleepsAndWakes();
    void saverArmadaAndOff();
    void saverScreenOff();
    void unsupportedRunsNothing();
    void missingBinaryUnsupported();
    void failedWordReported();

private:
    void useSupported(bool supported);
    void startAccent(RgbControl &control, QSignalSpy &applied, int hue);
    QStringList starts() const;

    QTemporaryDir m_dir;
    QString m_bin;
    QString m_log;
    QString m_rgbExit;
    QString m_systemctlExit;
    QString m_systemctlDelay;
};

void RgbTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_bin = m_dir.path() + QStringLiteral("/bin");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    m_rgbExit = m_dir.path() + QStringLiteral("/rgb.exit");
    m_systemctlExit = m_dir.path() + QStringLiteral("/systemctl.exit");
    m_systemctlDelay = m_dir.path() + QStringLiteral("/systemctl.delay");
    const QString logLine = QStringLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                            + QStringLiteral("\"\n");
    QVERIFY(writeFile(m_bin + QStringLiteral("/armada-rgb"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit \"$(cat \"") + m_rgbExit
                          + QStringLiteral("\")\"\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/systemctl"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("[ -f \"") + m_systemctlDelay
                          + QStringLiteral("\" ] && sleep \"$(cat \"") + m_systemctlDelay
                          + QStringLiteral("\")\"\nexit \"$(cat \"") + m_systemctlExit + QStringLiteral("\")\"\n"),
                      true));
    qputenv("CASDAP_RGB_BIN", (m_bin + QStringLiteral("/armada-rgb")).toUtf8());
    qputenv("CASDAP_SYSTEMCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
    QCOMPARE(RgbControl::rgbPath(), m_bin + QStringLiteral("/armada-rgb"));
    QCOMPARE(RgbControl::systemctlPath(), m_bin + QStringLiteral("/systemctl"));
}

void RgbTest::init()
{
    QFile::remove(m_log);
    QFile::remove(m_systemctlDelay);
    useSupported(true);
    QVERIFY(writeFile(m_systemctlExit, QStringLiteral("0\n")));
    qputenv("CASDAP_RGB_BIN", (m_bin + QStringLiteral("/armada-rgb")).toUtf8());
}

void RgbTest::useSupported(bool supported)
{
    QVERIFY(writeFile(m_rgbExit, supported ? QStringLiteral("0\n") : QStringLiteral("1\n")));
}

void RgbTest::startAccent(RgbControl &control, QSignalSpy &applied, int hue)
{
    control.start(QStringLiteral("accent"), hue);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QFile::remove(m_log);
}

QStringList RgbTest::starts() const
{
    QStringList out;
    for (const QString &line : readFile(m_log).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        if (line.startsWith(QLatin1StringView("systemctl start ")))
            out.append(line.mid(16));
    }
    return out;
}

void RgbTest::words()
{
    QCOMPARE(RgbControl::hueWord(0), QStringLiteral("rgb-hue-0"));
    QCOMPARE(RgbControl::hueWord(350), QStringLiteral("rgb-hue-350"));
    QCOMPARE(RgbControl::hueWord(198), QStringLiteral("rgb-hue-200"));
    QCOMPARE(RgbControl::hueWord(360), QStringLiteral("rgb-hue-0"));
    QCOMPARE(RgbControl::hueWord(-10), QStringLiteral("rgb-hue-350"));
    QCOMPARE(RgbControl::unitFor(QStringLiteral("rgb-off")), QStringLiteral("casdap-priv@rgb-off.service"));
    QCOMPARE(RgbControl::colorWord(0, 1), QStringLiteral("rgb-color-0-1"));
    QCOMPARE(RgbControl::colorWord(200, 4), QStringLiteral("rgb-color-200-4"));
    QCOMPARE(RgbControl::colorWord(355, 2), QStringLiteral("rgb-color-0-2"));
    QCOMPARE(RgbControl::colorWord(344, 3), QStringLiteral("rgb-color-340-3"));
    QCOMPARE(RgbControl::colorWord(200, 0), QStringLiteral("rgb-hue-200"));
    QCOMPARE(RgbControl::colorWord(200, 5), QStringLiteral("rgb-hue-200"));
    QSet<QString> all;
    for (int hue = 0; hue < 360; hue += 10) {
        for (int level = 1; level <= RgbControl::levels; ++level)
            all.insert(RgbControl::colorWord(hue, level));
    }
    QCOMPARE(all.size(), 144);
}

void RgbTest::artColorWords()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy levelSpy(&control, &RgbControl::levelChanged);
    control.start(QStringLiteral("accent"), 200, 2);
    QCOMPARE(control.level(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("rgb-color-200-2"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(control.sentWord(), QStringLiteral("rgb-color-200-2"));
    QCOMPARE(control.sentHue(), 200);
    QFile::remove(m_log);

    control.setColor(200, 2);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 2);

    control.setColor(200, 1);
    control.setColor(30, 3);
    control.setColor(200, 4);
    QTest::qWait(RgbControl::hueDelayMs / 2);
    QCOMPARE(applied.count(), 2);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-color-200-4"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);

    control.setHue(200);
    QCOMPARE(control.level(), 0);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 3);

    control.setColor(200, 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-color-200-1"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    control.setHue(200);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QCOMPARE(applied.at(4).at(0).toString(), QStringLiteral("rgb-hue-200"));
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-color-200-4")), unit(QStringLiteral("rgb-color-200-1")),
                                    unit(QStringLiteral("rgb-hue-200"))}));
    QVERIFY(levelSpy.count() >= 5);
}

void RgbTest::artWordHeldWhileOff()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 350);

    control.setScreenOff(true);
    control.setColor(120, 3);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 2);
    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-color-120-3"));

    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    control.setSaver(true);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    control.setColor(60, 1);
    control.setSaver(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 6, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-sleep"));
    QCOMPARE(applied.at(4).at(0).toString(), QStringLiteral("rgb-wake"));
    QCOMPARE(applied.at(5).at(0).toString(), QStringLiteral("rgb-color-60-1"));

    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QVERIFY(writeFile(m_systemctlExit, QStringLiteral("1\n")));
    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: \"rgb-color-60-2\" failed");
    control.setColor(60, 2);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 7, waitMs);
    QVERIFY(!applied.at(6).at(1).toBool());
    QCOMPARE(control.sentWord(), QString());
    QCOMPARE(control.sentHue(), -1);
}

void RgbTest::restoreThenHueAtStart()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy supportedSpy(&control, &RgbControl::supportedChanged);
    QVERIFY(!control.supported());
    QVERIFY(!control.probed());
    QCOMPARE(control.mode(), QStringLiteral("armada"));
    control.start(QStringLiteral("accent"), 350);
    QVERIFY(control.busy());
    QCOMPARE(control.mode(), QStringLiteral("accent"));
    QCOMPARE(control.hue(), 350);
    QTRY_VERIFY_WITH_TIMEOUT(control.probed(), waitMs);
    QVERIFY(control.supported());
    QCOMPARE(supportedSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("rgb-restore"));
    QVERIFY(applied.at(0).at(1).toBool());
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("rgb-hue-350"));
    QVERIFY(applied.at(1).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(control.sentHue(), 350);
    const QString log = readFile(m_log);
    QVERIFY2(log.startsWith(QStringLiteral("armada-rgb supported\n")), qPrintable(log));
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-restore")), unit(QStringLiteral("rgb-hue-350"))}));
}

void RgbTest::offAndArmada()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("off"), 200);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-restore")), unit(QStringLiteral("rgb-off"))}));

    QFile::remove(m_log);
    control.setMode(QStringLiteral("armada"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-restore"));
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-restore"))}));

    QFile::remove(m_log);
    control.setHue(280);
    control.setMode(QStringLiteral("armada"));
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));

    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-hue-280"));

    QFile::remove(m_log);
    control.setMode(QStringLiteral("off"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QCOMPARE(control.sentHue(), -1);
    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 6, waitMs);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-off")), unit(QStringLiteral("rgb-hue-280"))}));

    QFile::remove(m_log);
    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: unknown mode \"disco\"");
    control.setMode(QStringLiteral("disco"));
    QCOMPARE(control.mode(), QStringLiteral("accent"));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::hueDebounced()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 350);

    for (int hue = 0; hue <= 90; hue += 10)
        control.setHue(hue);
    QCOMPARE(control.hue(), 90);
    QTest::qWait(RgbControl::hueDelayMs / 2);
    QCOMPARE(applied.count(), 2);
    QVERIFY(!QFile::exists(m_log));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-hue-90"));
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 3);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-hue-90"))}));
}

void RgbTest::unchangedHueSkipped()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 200);

    control.setHue(200);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 2);

    control.setHue(210);
    control.setHue(200);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 2);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::waitingHueReplaced()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 350);

    QVERIFY(writeFile(m_systemctlDelay, QStringLiteral("2\n")));
    control.setMode(QStringLiteral("off"));
    QTRY_VERIFY_WITH_TIMEOUT(starts().size() == 1, waitMs);
    control.setMode(QStringLiteral("accent"));
    control.setHue(10);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 2);
    QVERIFY(control.busy());
    QFile::remove(m_systemctlDelay);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-off"));
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-hue-10"));
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-off")), unit(QStringLiteral("rgb-hue-10"))}));
}

void RgbTest::screenOffHolds()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 350);

    control.setScreenOff(true);
    QVERIFY(control.screenOff());
    control.setHue(120);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 2);
    QVERIFY(!QFile::exists(m_log));

    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-hue-120"));
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-hue-120"))}));

    QFile::remove(m_log);
    control.setScreenOff(true);
    control.setScreenOff(false);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::saverSleepsAndWakes()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy saverSpy(&control, &RgbControl::saverChanged);
    startAccent(control, applied, 350);

    control.setSaver(true);
    QVERIFY(control.saver());
    QCOMPARE(saverSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-sleep"));
    QVERIFY(applied.at(2).at(1).toBool());
    QCOMPARE(control.sentHue(), 350);

    control.setHue(120);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 3);

    control.setSaver(false);
    QVERIFY(!control.saver());
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-wake"));
    QCOMPARE(applied.at(4).at(0).toString(), QStringLiteral("rgb-hue-120"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(control.sentHue(), 120);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-sleep")), unit(QStringLiteral("rgb-wake")),
                                    unit(QStringLiteral("rgb-hue-120"))}));

    QFile::remove(m_log);
    control.setSaver(true);
    control.setSaver(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 7, waitMs);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 7);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-sleep")), unit(QStringLiteral("rgb-wake"))}));
}

void RgbTest::saverArmadaAndOff()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("armada"), 350);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QFile::remove(m_log);

    control.setSaver(true);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("rgb-sleep"));
    control.setHue(120);
    control.setSaver(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-wake"));
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 3);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-sleep")), unit(QStringLiteral("rgb-wake"))}));

    control.setMode(QStringLiteral("off"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-off"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QFile::remove(m_log);
    control.setSaver(true);
    QTest::qWait(300);
    control.setSaver(false);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 4);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::saverScreenOff()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    startAccent(control, applied, 350);

    control.setSaver(true);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QFile::remove(m_log);

    control.setScreenOff(true);
    control.setHue(120);
    control.setSaver(false);
    QTest::qWait(RgbControl::hueDelayMs + 300);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));

    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-hue-120"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-hue-120"))}));

    QFile::remove(m_log);
    control.setSaver(true);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 5, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    control.setScreenOff(true);
    control.setScreenOff(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 6, waitMs);
    QCOMPARE(applied.at(5).at(0).toString(), QStringLiteral("rgb-sleep"));
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    control.setSaver(false);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 7, waitMs);
    QCOMPARE(applied.at(6).at(0).toString(), QStringLiteral("rgb-wake"));
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 7);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-sleep")), unit(QStringLiteral("rgb-sleep")),
                                    unit(QStringLiteral("rgb-wake"))}));
}

void RgbTest::unsupportedRunsNothing()
{
    useSupported(false);
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy supportedSpy(&control, &RgbControl::supportedChanged);
    control.start(QStringLiteral("accent"), 350);
    QTRY_VERIFY_WITH_TIMEOUT(control.probed(), waitMs);
    QVERIFY(!control.supported());
    QCOMPARE(supportedSpy.count(), 0);
    QVERIFY(!control.busy());
    control.setMode(QStringLiteral("off"));
    control.setMode(QStringLiteral("accent"));
    control.setHue(200);
    control.setScreenOff(true);
    control.setScreenOff(false);
    control.setSaver(true);
    control.setSaver(false);
    QTest::qWait(RgbControl::hueDelayMs + 200);
    QCOMPARE(applied.count(), 0);
    QCOMPARE(readFile(m_log), QStringLiteral("armada-rgb supported\n"));
}

void RgbTest::missingBinaryUnsupported()
{
    qputenv("CASDAP_RGB_BIN", (m_dir.path() + QStringLiteral("/missing/armada-rgb")).toUtf8());
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("off"), 350);
    QTRY_VERIFY_WITH_TIMEOUT(control.probed(), waitMs);
    QVERIFY(!control.supported());
    QVERIFY(!control.busy());
    QTest::qWait(300);
    QCOMPARE(applied.count(), 0);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::failedWordReported()
{
    QVERIFY(writeFile(m_systemctlExit, QStringLiteral("1\n")));
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: unknown mode \"disco\"");
    control.start(QStringLiteral("disco"), 350);
    QCOMPARE(control.mode(), QStringLiteral("armada"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("rgb-restore"));
    QVERIFY(!applied.at(0).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(starts(), QStringList({unit(QStringLiteral("rgb-restore"))}));

    QFile::remove(m_log);
    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: \"rgb-hue-350\" failed");
    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QVERIFY(!applied.at(1).at(1).toBool());
    QCOMPARE(control.sentHue(), -1);
}

QTEST_GUILESS_MAIN(RgbTest)

#include "tst_rgb.moc"
