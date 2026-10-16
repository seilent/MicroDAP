#include "GuideKey.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace microdap;

namespace {

constexpr int settleMs = 150;
constexpr int waitMs = 3000;

}

class GuideTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void opens();
    void notFoundRetries();
    void tap();
    void chordDropsTap();
    void takeOutsideWindow();
    void hold();
    void chordDropsHold();
    void longTap();
    void pressDuringGrace();
    void staleTime();
    void otherKeysIgnored();
    void pocketTap();
    void pocketDouble();
    void pocketHold();
    void pocketTapThenHold();
    void pocketNoChord();
    void reopen();

private:
    void makeNode(const QString &name);
    void openWriter();
    void send(int code, int value, qint64 atMs);
    void press(qint64 atMs = 0);
    void release(qint64 atMs = 0);

    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_fifo;
    int m_writer = -1;
};

void GuideTest::makeNode(const QString &name)
{
    const QString sys = m_dir->path() + QStringLiteral("/sys/event3/device");
    QVERIFY(QDir().mkpath(sys));
    QFile file(sys + QStringLiteral("/name"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(name.toUtf8() + '\n');
    file.close();
    QVERIFY(QDir().mkpath(m_dir->path() + QStringLiteral("/dev")));
    m_fifo = m_dir->path() + QStringLiteral("/dev/event3");
    QCOMPARE(::mkfifo(QFile::encodeName(m_fifo).constData(), 0600), 0);
}

void GuideTest::openWriter()
{
    m_writer = ::open(QFile::encodeName(m_fifo).constData(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    QVERIFY(m_writer >= 0);
}

void GuideTest::send(int code, int value, qint64 atMs)
{
    input_event events[2]{};
    events[0].input_event_sec = atMs / 1000;
    events[0].input_event_usec = (atMs % 1000) * 1000;
    events[0].type = EV_KEY;
    events[0].code = code;
    events[0].value = value;
    events[1].input_event_sec = events[0].input_event_sec;
    events[1].input_event_usec = events[0].input_event_usec;
    events[1].type = EV_SYN;
    events[1].code = SYN_REPORT;
    QCOMPARE(::write(m_writer, events, sizeof(events)), ssize_t(sizeof(events)));
}

void GuideTest::press(qint64 atMs)
{
    send(KEY_F13, 1, atMs ? atMs : GuideKey::nowMs());
}

void GuideTest::release(qint64 atMs)
{
    send(KEY_F13, 0, atMs ? atMs : GuideKey::nowMs());
}

void GuideTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("MICRODAP_DEV_INPUT", QFile::encodeName(m_dir->path() + QStringLiteral("/dev")));
    qputenv("MICRODAP_SYS_INPUT", QFile::encodeName(m_dir->path() + QStringLiteral("/sys")));
    qunsetenv("MICRODAP_GUIDE_NAME");
    makeNode(QStringLiteral("InputPlumber Keyboard"));
    openWriter();
}

void GuideTest::cleanup()
{
    if (m_writer >= 0)
        ::close(m_writer);
    m_writer = -1;
    m_dir.reset();
}

void GuideTest::opens()
{
    QCOMPARE(GuideKey::deviceName(), QStringLiteral("InputPlumber Keyboard"));
    GuideKey guide;
    QVERIFY(guide.isOpen());
}

void GuideTest::notFoundRetries()
{
    qputenv("MICRODAP_GUIDE_NAME", "Other Keyboard");
    GuideKey guide;
    QVERIFY(!guide.isOpen());
    QFile file(m_dir->path() + QStringLiteral("/sys/event3/device/name"));
    QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write("Other Keyboard\n");
    file.close();
    QTRY_VERIFY_WITH_TIMEOUT(guide.isOpen(), GuideKey::retryMs + waitMs);
}

void GuideTest::tap()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    QSignalSpy buzz(&guide, &GuideKey::buzz);
    press();
    QTest::qWait(50);
    release();
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, waitMs);
    QTest::qWait(GuideKey::holdMs);
    QCOMPARE(play.count(), 1);
    QCOMPARE(prompt.count(), 0);
    QCOMPARE(buzz.count(), 0);
}

void GuideTest::chordDropsTap()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QVERIFY(!guide.take());
    press();
    QTest::qWait(settleMs);
    QVERIFY(guide.take());
    release();
    QTest::qWait(settleMs + GuideKey::chordGraceMs);
    QCOMPARE(play.count(), 0);
}

void GuideTest::takeOutsideWindow()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    press(GuideKey::nowMs() - GuideKey::chordMs - 100);
    QTest::qWait(settleMs);
    QVERIFY(!guide.take());
    release();
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, waitMs);
    QVERIFY(!guide.take());
}

void GuideTest::hold()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    QSignalSpy buzz(&guide, &GuideKey::buzz);
    press();
    QTest::qWait(GuideKey::holdMs - 200);
    QCOMPARE(prompt.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(prompt.count(), 1, waitMs);
    QCOMPARE(buzz.count(), 1);
    QCOMPARE(buzz.at(0).at(0).value<QList<int>>(), QList<int>{GuideKey::buzzMs});
    QVERIFY(!guide.take());
    release();
    QTest::qWait(settleMs);
    QCOMPARE(prompt.count(), 1);
    QCOMPARE(play.count(), 0);
}

void GuideTest::chordDropsHold()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    QSignalSpy buzz(&guide, &GuideKey::buzz);
    press();
    QTest::qWait(settleMs);
    QVERIFY(guide.take());
    QTest::qWait(GuideKey::holdMs + settleMs);
    release();
    QTest::qWait(settleMs);
    QCOMPARE(prompt.count(), 0);
    QCOMPARE(play.count(), 0);
    QCOMPARE(buzz.count(), 0);
}

void GuideTest::longTap()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    const qint64 now = GuideKey::nowMs();
    press(now - 700);
    release(now);
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, settleMs);
    QTest::qWait(GuideKey::holdMs);
    QCOMPARE(prompt.count(), 0);
    QCOMPARE(play.count(), 1);
}

void GuideTest::pressDuringGrace()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    const qint64 now = GuideKey::nowMs();
    press(now - 30);
    release(now - 20);
    press(now);
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, settleMs);
    release();
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 2, waitMs);
}

void GuideTest::staleTime()
{
    GuideKey guide;
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    send(KEY_F13, 1, 1);
    QTest::qWait(GuideKey::holdMs - 200);
    QCOMPARE(prompt.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(prompt.count(), 1, waitMs);
    release();
}

void GuideTest::otherKeysIgnored()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    send(KEY_F14, 1, GuideKey::nowMs());
    send(KEY_F14, 0, GuideKey::nowMs());
    send(KEY_F15, 1, GuideKey::nowMs());
    QTest::qWait(GuideKey::holdMs + settleMs);
    send(KEY_F15, 0, GuideKey::nowMs());
    QTest::qWait(settleMs);
    QCOMPARE(play.count(), 0);
    QCOMPARE(prompt.count(), 0);
}

void GuideTest::pocketTap()
{
    GuideKey guide;
    guide.setScreenOff(true);
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy next(&guide, &GuideKey::next);
    press();
    QTest::qWait(50);
    release();
    QTest::qWait(GuideKey::doubleMs - 150);
    QCOMPARE(play.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, waitMs);
    QCOMPARE(next.count(), 0);
}

void GuideTest::pocketDouble()
{
    GuideKey guide;
    guide.setScreenOff(true);
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy next(&guide, &GuideKey::next);
    QSignalSpy buzz(&guide, &GuideKey::buzz);
    const qint64 base = GuideKey::nowMs() - 200;
    press(base);
    release(base + 40);
    press(base + 150);
    release(base + 190);
    QTRY_COMPARE_WITH_TIMEOUT(next.count(), 1, waitMs);
    const QList<int> pattern{GuideKey::buzzMs, GuideKey::buzzGapMs, GuideKey::buzzMs};
    QCOMPARE(buzz.count(), 1);
    QCOMPARE(buzz.at(0).at(0).value<QList<int>>(), pattern);
    QTest::qWait(GuideKey::doubleMs + settleMs);
    QCOMPARE(play.count(), 0);
}

void GuideTest::pocketHold()
{
    GuideKey guide;
    guide.setScreenOff(true);
    QSignalSpy play(&guide, &GuideKey::playPause);
    QSignalSpy previous(&guide, &GuideKey::previous);
    QSignalSpy prompt(&guide, &GuideKey::switchPrompt);
    QSignalSpy buzz(&guide, &GuideKey::buzz);
    press();
    QTest::qWait(GuideKey::pocketHoldMs - 200);
    QCOMPARE(previous.count(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(previous.count(), 1, waitMs);
    QCOMPARE(buzz.count(), 1);
    QTest::qWait(GuideKey::holdMs);
    release();
    QTest::qWait(GuideKey::doubleMs + settleMs);
    QCOMPARE(previous.count(), 1);
    QCOMPARE(prompt.count(), 0);
    QCOMPARE(play.count(), 0);
}

void GuideTest::pocketTapThenHold()
{
    GuideKey guide;
    guide.setScreenOff(true);
    QList<QString> order;
    connect(&guide, &GuideKey::playPause, this, [&order] { order.append(QStringLiteral("play")); });
    connect(&guide, &GuideKey::previous, this, [&order] { order.append(QStringLiteral("previous")); });
    const qint64 base = GuideKey::nowMs() - 150;
    press(base);
    release(base + 40);
    press(base + 150);
    QTRY_COMPARE_WITH_TIMEOUT(order.size(), 2, waitMs);
    const QList<QString> expected{QStringLiteral("play"), QStringLiteral("previous")};
    QCOMPARE(order, expected);
    release();
    QTest::qWait(GuideKey::doubleMs + settleMs);
    QCOMPARE(order, expected);
}

void GuideTest::pocketNoChord()
{
    GuideKey guide;
    guide.setScreenOff(true);
    QSignalSpy play(&guide, &GuideKey::playPause);
    press();
    QTest::qWait(settleMs);
    QVERIFY(!guide.take());
    release();
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, waitMs);
}

void GuideTest::reopen()
{
    GuideKey guide;
    QSignalSpy play(&guide, &GuideKey::playPause);
    QVERIFY(guide.isOpen());
    ::close(m_writer);
    m_writer = -1;
    QTRY_VERIFY_WITH_TIMEOUT(!guide.isOpen(), waitMs);
    openWriter();
    QTRY_VERIFY_WITH_TIMEOUT(guide.isOpen(), GuideKey::retryMs + waitMs);
    press();
    release();
    QTRY_COMPARE_WITH_TIMEOUT(play.count(), 1, waitMs);
}

QTEST_GUILESS_MAIN(GuideTest)
#include "tst_guide.moc"
