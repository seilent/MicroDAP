#include "RgbControl.h"
#include "Settings.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
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

}

class RgbTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void accentTableMatchesUi();
    void restoreThenAccentAtStart();
    void offAndArmada();
    void accentChangeReapplies();
    void unsupportedRunsNothing();
    void missingBinaryUnsupported();
    void failedWordReported();

private:
    void useSupported(bool supported);
    QStringList starts() const;

    QTemporaryDir m_dir;
    QString m_bin;
    QString m_log;
    QString m_rgbExit;
    QString m_systemctlExit;
};

void RgbTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_bin = m_dir.path() + QStringLiteral("/bin");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    m_rgbExit = m_dir.path() + QStringLiteral("/rgb.exit");
    m_systemctlExit = m_dir.path() + QStringLiteral("/systemctl.exit");
    const QString logLine = QStringLiteral("printf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                            + QStringLiteral("\"\n");
    QVERIFY(writeFile(m_bin + QStringLiteral("/armada-rgb"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit \"$(cat \"") + m_rgbExit
                          + QStringLiteral("\")\"\n"),
                      true));
    QVERIFY(writeFile(m_bin + QStringLiteral("/systemctl"),
                      QStringLiteral("#!/bin/sh\n") + logLine + QStringLiteral("exit \"$(cat \"") + m_systemctlExit
                          + QStringLiteral("\")\"\n"),
                      true));
    qputenv("CASDAP_RGB_BIN", (m_bin + QStringLiteral("/armada-rgb")).toUtf8());
    qputenv("CASDAP_SYSTEMCTL_BIN", (m_bin + QStringLiteral("/systemctl")).toUtf8());
    QCOMPARE(RgbControl::rgbPath(), m_bin + QStringLiteral("/armada-rgb"));
    QCOMPARE(RgbControl::systemctlPath(), m_bin + QStringLiteral("/systemctl"));
}

void RgbTest::init()
{
    QFile::remove(m_log);
    useSupported(true);
    QVERIFY(writeFile(m_systemctlExit, QStringLiteral("0\n")));
    qputenv("CASDAP_RGB_BIN", (m_bin + QStringLiteral("/armada-rgb")).toUtf8());
}

void RgbTest::useSupported(bool supported)
{
    QVERIFY(writeFile(m_rgbExit, supported ? QStringLiteral("0\n") : QStringLiteral("1\n")));
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

void RgbTest::accentTableMatchesUi()
{
    QFile ui(QStringLiteral(CASDAP_UI_QML));
    QVERIFY2(ui.open(QIODevice::ReadOnly), qPrintable(ui.fileName()));
    const QString text = QString::fromUtf8(ui.readAll());
    const QRegularExpression re(QStringLiteral("accentOptions:\\s*\\[([^\\]]*)\\]"));
    const QRegularExpressionMatch match = re.match(text);
    QVERIFY(match.hasMatch());
    QStringList hexes;
    const QRegularExpression item(QStringLiteral("\"(#[0-9A-Fa-f]{6})\""));
    auto it = item.globalMatch(match.captured(1));
    while (it.hasNext())
        hexes.append(it.next().captured(1).toUpper());
    QCOMPARE(hexes, RgbControl::accentHexes());
    QCOMPARE(hexes, Settings::accents());
    QCOMPARE(RgbControl::accentNames().size(), hexes.size());
    QCOMPARE(RgbControl::accentName(QStringLiteral("#FA2A55")), QStringLiteral("pink"));
    QCOMPARE(RgbControl::accentName(QStringLiteral("#f09609")), QStringLiteral("orange"));
    QCOMPARE(RgbControl::accentName(QStringLiteral("#1BA1E2")), QStringLiteral("cyan"));
    QCOMPARE(RgbControl::accentName(QStringLiteral("#339933")), QStringLiteral("lime"));
    QCOMPARE(RgbControl::accentName(QStringLiteral("#A200FF")), QStringLiteral("purple"));
    QCOMPARE(RgbControl::accentName(QStringLiteral("#123456")), QString());
    QCOMPARE(RgbControl::unitFor(QStringLiteral("rgb-off")), QStringLiteral("casdap-priv@rgb-off.service"));
}

void RgbTest::restoreThenAccentAtStart()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy supportedSpy(&control, &RgbControl::supportedChanged);
    QVERIFY(!control.supported());
    QVERIFY(!control.probed());
    QCOMPARE(control.mode(), QStringLiteral("armada"));
    control.start(QStringLiteral("accent"), QStringLiteral("#FA2A55"));
    QVERIFY(control.busy());
    QCOMPARE(control.mode(), QStringLiteral("accent"));
    QCOMPARE(control.accent(), QStringLiteral("#FA2A55"));
    QTRY_VERIFY_WITH_TIMEOUT(control.probed(), waitMs);
    QVERIFY(control.supported());
    QCOMPARE(supportedSpy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("rgb-restore"));
    QVERIFY(applied.at(0).at(1).toBool());
    QCOMPARE(applied.at(1).at(0).toString(), QStringLiteral("rgb-accent-pink"));
    QVERIFY(applied.at(1).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    const QString log = readFile(m_log);
    QVERIFY2(log.startsWith(QStringLiteral("armada-rgb supported\n")), qPrintable(log));
    QCOMPARE(starts(), QStringList({QStringLiteral("casdap-priv@rgb-restore.service"),
                                    QStringLiteral("casdap-priv@rgb-accent-pink.service")}));
}

void RgbTest::offAndArmada()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("off"), QStringLiteral("#1BA1E2"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);
    QCOMPARE(starts(), QStringList({QStringLiteral("casdap-priv@rgb-restore.service"),
                                    QStringLiteral("casdap-priv@rgb-off.service")}));

    QFile::remove(m_log);
    control.setMode(QStringLiteral("armada"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-restore"));
    QCOMPARE(starts(), QStringList({QStringLiteral("casdap-priv@rgb-restore.service")}));

    QFile::remove(m_log);
    control.setAccent(QStringLiteral("#A200FF"));
    control.setMode(QStringLiteral("armada"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));

    control.setMode(QStringLiteral("accent"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 4, waitMs);
    QCOMPARE(applied.at(3).at(0).toString(), QStringLiteral("rgb-accent-purple"));

    QFile::remove(m_log);
    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: unknown mode \"disco\"");
    control.setMode(QStringLiteral("disco"));
    QCOMPARE(control.mode(), QStringLiteral("accent"));
    QTest::qWait(200);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::accentChangeReapplies()
{
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("accent"), QStringLiteral("#FA2A55"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 2, waitMs);

    QFile::remove(m_log);
    control.setAccent(QStringLiteral("#1ba1e2"));
    QCOMPARE(control.accent(), QStringLiteral("#1BA1E2"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 3, waitMs);
    QCOMPARE(applied.at(2).at(0).toString(), QStringLiteral("rgb-accent-cyan"));
    QCOMPARE(starts(), QStringList({QStringLiteral("casdap-priv@rgb-accent-cyan.service")}));

    QFile::remove(m_log);
    control.setAccent(QStringLiteral("#1BA1E2"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));

    QTest::ignoreMessage(QtWarningMsg, "casdap rgb: no accent word for \"#123456\"");
    control.setAccent(QStringLiteral("#123456"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 3);
    QVERIFY(!QFile::exists(m_log));
}

void RgbTest::unsupportedRunsNothing()
{
    useSupported(false);
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    QSignalSpy supportedSpy(&control, &RgbControl::supportedChanged);
    control.start(QStringLiteral("accent"), QStringLiteral("#FA2A55"));
    QTRY_VERIFY_WITH_TIMEOUT(control.probed(), waitMs);
    QVERIFY(!control.supported());
    QCOMPARE(supportedSpy.count(), 0);
    QVERIFY(!control.busy());
    control.setMode(QStringLiteral("off"));
    control.setAccent(QStringLiteral("#1BA1E2"));
    QTest::qWait(300);
    QCOMPARE(applied.count(), 0);
    QCOMPARE(readFile(m_log), QStringLiteral("armada-rgb supported\n"));
}

void RgbTest::missingBinaryUnsupported()
{
    qputenv("CASDAP_RGB_BIN", (m_dir.path() + QStringLiteral("/missing/armada-rgb")).toUtf8());
    RgbControl control;
    QSignalSpy applied(&control, &RgbControl::applied);
    control.start(QStringLiteral("off"), QStringLiteral("#FA2A55"));
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
    control.start(QStringLiteral("disco"), QStringLiteral("#FA2A55"));
    QCOMPARE(control.mode(), QStringLiteral("armada"));
    QTRY_COMPARE_WITH_TIMEOUT(applied.count(), 1, waitMs);
    QCOMPARE(applied.at(0).at(0).toString(), QStringLiteral("rgb-restore"));
    QVERIFY(!applied.at(0).at(1).toBool());
    QTRY_VERIFY_WITH_TIMEOUT(!control.busy(), waitMs);
    QTest::qWait(300);
    QCOMPARE(applied.count(), 1);
    QCOMPARE(starts(), QStringList({QStringLiteral("casdap-priv@rgb-restore.service")}));
}

QTEST_GUILESS_MAIN(RgbTest)

#include "tst_rgb.moc"
