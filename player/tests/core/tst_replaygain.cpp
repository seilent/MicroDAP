#include "Queue.h"
#include "ReplayGain.h"

#include <QtTest>

#include <cmath>
#include <optional>

Q_DECLARE_METATYPE(microdap::QueueEntry)

using namespace microdap;

namespace {

QueueEntry tagged(std::optional<double> trackGain, std::optional<double> trackPeak, std::optional<double> albumGain,
                  std::optional<double> albumPeak)
{
    QueueEntry entry;
    entry.trackGainDb = trackGain;
    entry.trackPeak = trackPeak;
    entry.albumGainDb = albumGain;
    entry.albumPeak = albumPeak;
    return entry;
}

}

class ReplayGainTest : public QObject
{
    Q_OBJECT

private slots:
    void modes();
    void rule_data();
    void rule();
    void factor();
};

void ReplayGainTest::modes()
{
    QCOMPARE(replayGainMode(QStringLiteral("off")), ReplayGainMode::Off);
    QCOMPARE(replayGainMode(QStringLiteral("track")), ReplayGainMode::Track);
    QCOMPARE(replayGainMode(QStringLiteral("album")), ReplayGainMode::Album);
    QCOMPARE(replayGainMode(QStringLiteral("loud")), ReplayGainMode::Off);
    QCOMPARE(replayGainMode(QString()), ReplayGainMode::Off);
}

void ReplayGainTest::rule_data()
{
    QTest::addColumn<QueueEntry>("entry");
    QTest::addColumn<int>("mode");
    QTest::addColumn<double>("db");

    const int off = int(ReplayGainMode::Off);
    const int track = int(ReplayGainMode::Track);
    const int album = int(ReplayGainMode::Album);
    const double halfScale = -20.0 * std::log10(0.5);

    QTest::newRow("off ignores tags") << tagged(-6.0, 0.9, -4.0, 0.9) << off << 0.0;
    QTest::newRow("track gain") << tagged(-6.0, 0.9, -4.0, 0.9) << track << -6.0;
    QTest::newRow("album gain") << tagged(-6.0, 0.9, -4.0, 0.9) << album << -4.0;
    QTest::newRow("track falls back to album") << tagged(std::nullopt, std::nullopt, -4.0, 0.9) << track << -4.0;
    QTest::newRow("album falls back to track") << tagged(-6.0, 0.9, std::nullopt, std::nullopt) << album << -6.0;
    QTest::newRow("no tag") << tagged(std::nullopt, 0.5, std::nullopt, 0.5) << track << 0.0;
    QTest::newRow("positive gain under the peak") << tagged(5.0, 0.5, std::nullopt, std::nullopt) << track << 5.0;
    QTest::newRow("positive gain held at the peak") << tagged(9.0, 0.5, std::nullopt, std::nullopt) << track
                                                   << halfScale;
    QTest::newRow("positive gain without a peak plays at 0 db") << tagged(4.0, std::nullopt, std::nullopt, std::nullopt)
                                                                << track << 0.0;
    QTest::newRow("zero peak counts as full scale") << tagged(4.0, 0.0, std::nullopt, std::nullopt) << track << 0.0;
    QTest::newRow("negative gain without a peak") << tagged(-7.5, std::nullopt, std::nullopt, std::nullopt) << track
                                                  << -7.5;
    QTest::newRow("cap at 12 db") << tagged(20.0, 0.05, std::nullopt, std::nullopt) << track << 12.0;
    QTest::newRow("peak over full scale lowers the gain") << tagged(-1.0, 2.0, std::nullopt, std::nullopt) << track
                                                          << -20.0 * std::log10(2.0);
    QTest::newRow("outside 51 db counts as no tag") << tagged(60.0, 0.01, std::nullopt, std::nullopt) << track << 0.0;
    QTest::newRow("outside 51 db falls back") << tagged(-52.0, 0.9, -3.0, 0.9) << track << -3.0;
    QTest::newRow("album peak goes with the album gain") << tagged(2.0, 0.1, 9.0, 0.5) << album << halfScale;
    QTest::newRow("track peak goes with the track gain") << tagged(9.0, 0.5, 2.0, 0.1) << track << halfScale;
    QTest::newRow("fallback takes the album peak") << tagged(std::nullopt, 0.1, 9.0, 0.5) << track << halfScale;
}

void ReplayGainTest::rule()
{
    QFETCH(QueueEntry, entry);
    QFETCH(int, mode);
    QFETCH(double, db);
    QVERIFY2(std::abs(replayGainDb(entry, ReplayGainMode(mode)) - db) < 1e-9,
             qPrintable(QString::number(replayGainDb(entry, ReplayGainMode(mode)))));
}

void ReplayGainTest::factor()
{
    QVERIFY(std::abs(replayGainFactor(tagged(-20.0, 1.0, std::nullopt, std::nullopt), ReplayGainMode::Track) - 0.1)
            < 1e-12);
    QCOMPARE(replayGainFactor(tagged(-20.0, 1.0, std::nullopt, std::nullopt), ReplayGainMode::Off), 1.0);
    QVERIFY(std::abs(replayGainFactor(tagged(12.0, 0.01, std::nullopt, std::nullopt), ReplayGainMode::Track)
                     - std::pow(10.0, 0.6))
            < 1e-12);
}

QTEST_APPLESS_MAIN(ReplayGainTest)
#include "tst_replaygain.moc"
