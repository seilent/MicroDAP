#include "Crossfade.h"
#include "Queue.h"

#include <QtTest>

#include <cmath>

using namespace microdap;

namespace {

QueueEntry track(qint64 album, int disc, int number, const QString &title = QStringLiteral("Album"))
{
    QueueEntry entry;
    entry.albumId = album;
    entry.album = title;
    entry.discNo = disc;
    entry.trackNo = number;
    return entry;
}

}

class CrossfadeTest : public QObject
{
    Q_OBJECT

private slots:
    void equalPower();
    void schedule();
    void albumRule();
    void lengths();
};

void CrossfadeTest::equalPower()
{
    QCOMPARE(crossfadeWeight(0.0, true), 1.0);
    QVERIFY(std::abs(crossfadeWeight(0.0, false)) < 1e-12);
    QVERIFY(std::abs(crossfadeWeight(1.0, true)) < 1e-12);
    QCOMPARE(crossfadeWeight(1.0, false), 1.0);
    QVERIFY(std::abs(crossfadeWeight(0.5, true) - std::sqrt(0.5)) < 1e-12);
    QVERIFY(std::abs(crossfadeWeight(0.5, false) - std::sqrt(0.5)) < 1e-12);
    double last = 1.0;
    for (int i = 0; i <= 100; ++i) {
        const double t = i / 100.0;
        const double out = crossfadeWeight(t, true);
        const double in = crossfadeWeight(t, false);
        QVERIFY(std::abs(out * out + in * in - 1.0) < 1e-12);
        QVERIFY(out <= last);
        last = out;
    }
    QCOMPARE(crossfadeWeight(-1.0, true), 1.0);
    QCOMPARE(crossfadeWeight(2.0, false), 1.0);
}

void CrossfadeTest::schedule()
{
    const GstClockTime second = GST_SECOND;
    CrossfadeSchedule s = crossfadeSchedule(240 * second, 3 * second, 3 * second);
    QVERIFY(s.valid());
    QCOMPARE(s.start, 237 * second);
    QCOMPARE(s.prepare, 234 * second);
    s = crossfadeSchedule(4 * second, 2 * second, 3 * second);
    QVERIFY(s.valid());
    QCOMPARE(s.start, 2 * second);
    QCOMPARE(s.prepare, GstClockTime(0));
    QVERIFY(!crossfadeSchedule(4 * second - 1, 2 * second, 3 * second).valid());
    QVERIFY(!crossfadeSchedule(GST_CLOCK_TIME_NONE, 2 * second, 3 * second).valid());
    QVERIFY(!crossfadeSchedule(240 * second, 0, 3 * second).valid());
}

void CrossfadeTest::albumRule()
{
    QVERIFY(albumContinues(track(5, 1, 3), track(5, 1, 4)));
    QVERIFY(!albumContinues(track(5, 1, 3), track(5, 1, 5)));
    QVERIFY(!albumContinues(track(5, 1, 3), track(5, 1, 3)));
    QVERIFY(!albumContinues(track(5, 1, 3), track(6, 1, 4)));
    QVERIFY(!albumContinues(track(5, 1, 9), track(5, 2, 10)));
    QVERIFY(!albumContinues(track(5, 1, 0), track(5, 1, 1)));
    QVERIFY(!albumContinues(track(5, 1, 3, QStringLiteral("unknown album")), track(5, 1, 4)));
    QVERIFY(!albumContinues(track(5, 1, 3, QString()), track(5, 1, 4)));
    QVERIFY(!albumContinues(track(0, 1, 3), track(0, 1, 4)));
}

void CrossfadeTest::lengths()
{
    QVERIFY(crossfadeFits(4000, 4000, 2000));
    QVERIFY(!crossfadeFits(3999, 4000, 2000));
    QVERIFY(!crossfadeFits(4000, 3999, 2000));
    QVERIFY(!crossfadeFits(4000, 0, 2000));
    QVERIFY(!crossfadeFits(4000, 4000, 0));
}

QTEST_APPLESS_MAIN(CrossfadeTest)
#include "tst_crossfade.moc"
