#include "CodecFragment.h"
#include "FakeBluezModel.h"

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

class CodecTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void fragmentText();
    void writesAndRestarts();
    void unchangedSkipsRestart();
    void autoRemoves();
    void transportActiveDefers();
    void restartFailure();
    void rejectsUnknown();

private:
    bool writeStub(const QString &exitCode = QStringLiteral("0"));

    QTemporaryDir m_dir;
    QString m_confDir;
    QString m_stub;
    QString m_log;
};

bool CodecTest::writeStub(const QString &exitCode)
{
    return writeFile(m_stub,
                     QStringLiteral("#!/bin/sh\nprintf '%s %s\\n' \"$(basename \"$0\")\" \"$*\" >> \"") + m_log
                         + QStringLiteral("\"\nexit ") + exitCode + QStringLiteral("\n"),
                     true);
}

void CodecTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_confDir = m_dir.path() + QStringLiteral("/config/wireplumber/wireplumber.conf.d");
    m_stub = m_dir.path() + QStringLiteral("/bin/systemctl");
    m_log = m_dir.path() + QStringLiteral("/calls.log");
    QVERIFY(writeStub());
    qputenv("CASDAP_WP_CONF_DIR", m_confDir.toUtf8());
    qputenv("CASDAP_SYSTEMCTL_BIN", m_stub.toUtf8());
    QCOMPARE(CodecFragment::confDir(), m_confDir);
    QCOMPARE(CodecFragment::confPath(), m_confDir + QStringLiteral("/50-casdap-codec.conf"));
    QCOMPARE(CodecFragment::systemctlPath(), m_stub);
}

void CodecTest::init()
{
    QFile::remove(m_log);
    QFile::remove(CodecFragment::confPath());
    QVERIFY(writeStub());
}

void CodecTest::fragmentText()
{
    QCOMPARE(CodecFragment::fragmentText(QStringLiteral("aptx")),
             QStringLiteral("monitor.bluez.properties = {\n    bluez5.codecs = [ sbc aptx aptx_hd ]\n}\n"));
    QCOMPARE(CodecFragment::fragmentText(QStringLiteral("aac")),
             QStringLiteral("monitor.bluez.properties = {\n    bluez5.codecs = [ sbc aac ]\n}\n"));
    QCOMPARE(CodecFragment::fragmentText(QStringLiteral("ldac")),
             QStringLiteral("monitor.bluez.properties = {\n    bluez5.codecs = [ sbc ldac ]\n}\n"));
    QCOMPARE(CodecFragment::fragmentText(QStringLiteral("sbc")),
             QStringLiteral("monitor.bluez.properties = {\n    bluez5.codecs = [ sbc ]\n}\n"));
    QCOMPARE(CodecFragment::fragmentText(QStringLiteral("auto")), QString());
    QCOMPARE(CodecFragment::codecsFor(QStringLiteral("mp3")), QStringList());
}

void CodecTest::writesAndRestarts()
{
    CodecFragment fragment;
    QCOMPARE(fragment.codec(), QStringLiteral("auto"));
    QVERIFY(!fragment.pending());
    QVERIFY(!fragment.restarting());
    QSignalSpy finished(&fragment, &CodecFragment::restartFinished);
    QSignalSpy codecSpy(&fragment, &CodecFragment::codecChanged);

    QVERIFY(fragment.apply(QStringLiteral("aptx")));
    QCOMPARE(codecSpy.count(), 1);
    QCOMPARE(fragment.codec(), QStringLiteral("aptx"));
    QCOMPARE(readFile(CodecFragment::confPath()), CodecFragment::fragmentText(QStringLiteral("aptx")));
    QVERIFY(QDir(m_confDir).entryList(QDir::Files).size() == 1);
    QVERIFY(fragment.restarting());
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, waitMs);
    QVERIFY(finished.at(0).at(0).toBool());
    QVERIFY(!fragment.restarting());
    QVERIFY(!fragment.pending());
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl --user restart wireplumber\n"));

    QVERIFY(fragment.apply(QStringLiteral("aac")));
    QCOMPARE(readFile(CodecFragment::confPath()), CodecFragment::fragmentText(QStringLiteral("aac")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, waitMs);
    QCOMPARE(readFile(m_log).count(QLatin1Char('\n')), 2);
}

void CodecTest::unchangedSkipsRestart()
{
    QVERIFY(writeFile(CodecFragment::confPath(), CodecFragment::fragmentText(QStringLiteral("ldac"))));
    CodecFragment fragment;
    QSignalSpy finished(&fragment, &CodecFragment::restartFinished);
    QTest::ignoreMessage(QtInfoMsg, "casdap codec: \"ldac\" already in place");
    QVERIFY(fragment.apply(QStringLiteral("ldac")));
    QVERIFY(!fragment.restarting());
    QTest::qWait(100);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!QFile::exists(m_log));

    CodecFragment other;
    QTest::ignoreMessage(QtInfoMsg, "casdap codec: \"auto\" already in place");
    QVERIFY(QFile::remove(CodecFragment::confPath()));
    QVERIFY(other.apply(QStringLiteral("auto")));
    QVERIFY(!other.restarting());
    QVERIFY(!QFile::exists(m_log));
}

void CodecTest::autoRemoves()
{
    QVERIFY(writeFile(CodecFragment::confPath(), CodecFragment::fragmentText(QStringLiteral("sbc"))));
    CodecFragment fragment;
    QSignalSpy finished(&fragment, &CodecFragment::restartFinished);
    QVERIFY(fragment.apply(QStringLiteral("auto")));
    QVERIFY(!QFile::exists(CodecFragment::confPath()));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, waitMs);
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl --user restart wireplumber\n"));
}

void CodecTest::transportActiveDefers()
{
    FakeBluezModel bluez;
    bluez.setTransportActive(true);
    CodecFragment fragment;
    fragment.setBluez(&bluez);
    QCOMPARE(fragment.bluez(), &bluez);
    QSignalSpy pending(&fragment, &CodecFragment::pendingChanged);
    QSignalSpy finished(&fragment, &CodecFragment::restartFinished);

    QVERIFY(fragment.apply(QStringLiteral("sbc")));
    QCOMPARE(readFile(CodecFragment::confPath()), CodecFragment::fragmentText(QStringLiteral("sbc")));
    QVERIFY(fragment.pending());
    QCOMPARE(pending.count(), 1);
    QVERIFY(!fragment.restarting());
    QTest::qWait(100);
    QCOMPARE(finished.count(), 0);
    QVERIFY(!QFile::exists(m_log));

    bluez.setTransportActive(false);
    QTest::qWait(100);
    QVERIFY(fragment.pending());
    QVERIFY(!QFile::exists(m_log));

    QVERIFY(fragment.apply(QStringLiteral("aac")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, waitMs);
    QVERIFY(finished.at(0).at(0).toBool());
    QVERIFY(!fragment.pending());
    QCOMPARE(pending.count(), 2);
    QCOMPARE(readFile(m_log), QStringLiteral("systemctl --user restart wireplumber\n"));
}

void CodecTest::restartFailure()
{
    QVERIFY(writeStub(QStringLiteral("1")));
    CodecFragment fragment;
    QSignalSpy finished(&fragment, &CodecFragment::restartFinished);
    QTest::ignoreMessage(QtWarningMsg, "casdap codec: wireplumber restart exit 1");
    QVERIFY(fragment.apply(QStringLiteral("aptx")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, waitMs);
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(fragment.pending());
    QCOMPARE(readFile(CodecFragment::confPath()), CodecFragment::fragmentText(QStringLiteral("aptx")));

    qputenv("CASDAP_SYSTEMCTL_BIN", (m_dir.path() + QStringLiteral("/missing/systemctl")).toUtf8());
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("^casdap codec: cannot start ")));
    QVERIFY(fragment.apply(QStringLiteral("ldac")));
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, waitMs);
    QVERIFY(!finished.at(1).at(0).toBool());
    QVERIFY(fragment.pending());
    qputenv("CASDAP_SYSTEMCTL_BIN", m_stub.toUtf8());
}

void CodecTest::rejectsUnknown()
{
    CodecFragment fragment;
    QTest::ignoreMessage(QtWarningMsg, "casdap codec: unknown codec \"mp3\"");
    QVERIFY(!fragment.apply(QStringLiteral("mp3")));
    QCOMPARE(fragment.codec(), QStringLiteral("auto"));
    QVERIFY(!QFile::exists(CodecFragment::confPath()));
    QVERIFY(!QFile::exists(m_log));
}

QTEST_GUILESS_MAIN(CodecTest)

#include "tst_codec.moc"
