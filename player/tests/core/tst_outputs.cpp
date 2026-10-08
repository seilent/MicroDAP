#include "OutputWatch.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <sys/stat.h>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;

bool writeFile(const QString &path, const QByteArray &text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text);
    return true;
}

}

class OutputsTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void internalOnly();
    void external();
    void pollFlips();
    void internalKinds();
    void writeback();
    void screenChange();
    void refreshSync();
    void defaultRoot();

private:
    bool setConnector(const QString &name, bool enabled);

    std::unique_ptr<QTemporaryDir> m_dir;
};

bool OutputsTest::setConnector(const QString &name, bool enabled)
{
    return writeFile(m_dir->path() + QStringLiteral("/card0-") + name + QStringLiteral("/enabled"),
                     enabled ? "enabled\n" : "disabled\n");
}

void OutputsTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    QVERIFY(setConnector(QStringLiteral("DSI-1"), true));
    QVERIFY(setConnector(QStringLiteral("DP-1"), false));
    const QString modes = m_dir->path() + QStringLiteral("/card0-DP-1/modes");
    QCOMPARE(mkfifo(QFile::encodeName(modes).constData(), 0600), 0);
    QVERIFY(writeFile(m_dir->path() + QStringLiteral("/version"), "drm\n"));
}

void OutputsTest::internalOnly()
{
    OutputWatch watch(m_dir->path());
    QCOMPARE(watch.root(), m_dir->path());
    QVERIFY(watch.internal());
}

void OutputsTest::external()
{
    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    OutputWatch watch(m_dir->path());
    QVERIFY(!watch.internal());
}

void OutputsTest::pollFlips()
{
    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    OutputWatch watch(m_dir->path());
    QSignalSpy spy(&watch, &OutputWatch::internalChanged);
    QVERIFY(!watch.internal());
    QVERIFY(setConnector(QStringLiteral("DP-1"), false));
    QTRY_VERIFY_WITH_TIMEOUT(watch.internal(), OutputWatch::pollMs + waitMs);
    QCOMPARE(spy.count(), 1);
    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    QTRY_VERIFY_WITH_TIMEOUT(!watch.internal(), OutputWatch::pollMs + waitMs);
    QCOMPARE(spy.count(), 2);
    QTest::qWait(OutputWatch::pollMs + 200);
    QCOMPARE(spy.count(), 2);

    watch.setScreenOff(true);
    QVERIFY(setConnector(QStringLiteral("DP-1"), false));
    QTest::qWait(OutputWatch::pollMs + 500);
    QVERIFY(!watch.internal());
    watch.setScreenOff(false);
    QTRY_VERIFY_WITH_TIMEOUT(watch.internal(), OutputWatch::pollMs + waitMs);
    QCOMPARE(spy.count(), 3);
}

void OutputsTest::internalKinds()
{
    QVERIFY(setConnector(QStringLiteral("DSI-1"), false));
    QVERIFY(setConnector(QStringLiteral("eDP-1"), true));
    QVERIFY(OutputWatch(m_dir->path()).internal());
    QVERIFY(setConnector(QStringLiteral("eDP-1"), false));
    QVERIFY(setConnector(QStringLiteral("LVDS-1"), true));
    QVERIFY(OutputWatch(m_dir->path()).internal());
    QVERIFY(setConnector(QStringLiteral("HDMI-A-1"), true));
    QVERIFY(!OutputWatch(m_dir->path()).internal());
}

void OutputsTest::writeback()
{
    QVERIFY(setConnector(QStringLiteral("Writeback-1"), true));
    QVERIFY(OutputWatch(m_dir->path()).internal());
}

void OutputsTest::screenChange()
{
    OutputWatch watch(m_dir->path());
    watch.setScreenOff(true);
    QSignalSpy spy(&watch, &OutputWatch::internalChanged);
    QElapsedTimer clock;

    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    clock.start();
    watch.scheduleReads();
    QVERIFY(!watch.internal());
    QCOMPARE(spy.count(), 1);

    QVERIFY(setConnector(QStringLiteral("DP-1"), false));
    QTest::qWait(100);
    if (clock.elapsed() < OutputWatch::settleShortMs - 50)
        QVERIFY(!watch.internal());
    QTRY_VERIFY_WITH_TIMEOUT(watch.internal(), waitMs);
    QVERIFY(clock.elapsed() >= OutputWatch::settleShortMs - 50);
    QCOMPARE(spy.count(), 2);

    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    if (clock.elapsed() < OutputWatch::settleLongMs - 300) {
        QTest::qWait(100);
        QVERIFY(watch.internal());
    }
    QTRY_VERIFY_WITH_TIMEOUT(!watch.internal(), waitMs);
    QVERIFY(clock.elapsed() >= OutputWatch::settleLongMs - 100);
    QCOMPARE(spy.count(), 3);

    QVERIFY(setConnector(QStringLiteral("DP-1"), false));
    QTest::qWait(OutputWatch::pollMs + 500);
    QVERIFY(!watch.internal());
    QCOMPARE(spy.count(), 3);
}

void OutputsTest::refreshSync()
{
    OutputWatch watch(m_dir->path());
    watch.setScreenOff(true);
    bool seen = false;
    connect(&watch, &OutputWatch::internalChanged, this, [&seen] { seen = true; });
    QVERIFY(setConnector(QStringLiteral("DP-1"), true));
    watch.refresh();
    QVERIFY(seen);
    QVERIFY(!watch.internal());
    seen = false;
    watch.refresh();
    QVERIFY(!seen);
}

void OutputsTest::defaultRoot()
{
    qunsetenv("CASDAP_DRM_ROOT");
    QCOMPARE(OutputWatch::defaultRoot(), QStringLiteral("/sys/class/drm"));
    qputenv("CASDAP_DRM_ROOT", m_dir->path().toUtf8());
    QCOMPARE(OutputWatch::defaultRoot(), m_dir->path());
    QVERIFY(OutputWatch().internal());
    qunsetenv("CASDAP_DRM_ROOT");
}

QTEST_MAIN(OutputsTest)

#include "tst_outputs.moc"
