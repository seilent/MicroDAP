#include "Eq.h"
#include "EqFit.h"

#include <QElapsedTimer>
#include <QTest>
#include <QThread>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace microdap;

namespace {

std::vector<double> target(const std::vector<double> &freqs)
{
    EqProfile p = EqProfile::flat();
    const double f[] = {30, 250, 500, 900, 1700, 2700, 4000, 5500, 6500, 7500};
    const double g[] = {-6, 3, -1, -1.5, 1, 3.5, -2, -5, 1, -3};
    const double q[] = {0.7, 0.9, 2, 1.4, 3, 1.8, 4, 2.5, 2, 0.7};
    for (int i = 0; i < 10; ++i) {
        p.bands[i].type = i == 9 ? EqBand::HighShelf : EqBand::Peak;
        p.bands[i].freq = f[i];
        p.bands[i].gain = g[i];
        p.bands[i].q = q[i];
    }
    std::vector<double> db(freqs.size());
    EqFit::response(p, 96000, freqs.data(), int(freqs.size()), db.data());
    for (double &v : db)
        v = std::round(v * 10) / 10;
    return db;
}

qint64 once(const std::vector<double> &freqs, const std::vector<double> &gains)
{
    EqFitOptions options;
    options.budgetMs = 600000;
    EqProfile o;
    QElapsedTimer clock;
    clock.start();
    EqFit::fit(freqs, gains, o, options);
    return clock.nsecsElapsed();
}

}

class EqFitTimingTest : public QObject
{
    Q_OBJECT

private slots:
    void cap();
    void densePoints();
};

void EqFitTimingTest::cap()
{
    std::vector<double> freqs(EqFit::frequencies.begin(), EqFit::frequencies.end());
    const std::vector<double> gains = target(freqs);
    EqFitOptions options;
    options.maxIters = 1000000;
    options.onIteration = [](int) { QThread::msleep(100); };
    EqProfile o;
    EqFitStats st;
    QElapsedTimer clock;
    clock.start();
    QCOMPARE(EqFit::fit(freqs, gains, o, options, &st), EqFitEnd::Done);
    const qint64 ms = clock.elapsed();
    qInfo() << "cap" << ms << "ms" << st.iterations << "iterations";
    QVERIFY2(ms >= 2500 && ms < 2500 + 400, qPrintable(QString::number(ms)));
    QCOMPARE(o.count, 10);
}

void EqFitTimingTest::densePoints()
{
    std::vector<double> sparse(EqFit::frequencies.begin(), EqFit::frequencies.end());
    std::vector<double> dense = sparse;
    for (int i = 0; dense.size() < 1024; ++i)
        dense.push_back(20.05 + 19.5 * i);
    std::sort(dense.begin(), dense.end());
    const std::vector<double> sparseGains = target(sparse);
    const std::vector<double> denseGains = target(dense);
    qint64 a = -1;
    qint64 b = -1;
    for (int i = 0; i < 15; ++i) {
        const qint64 x = once(sparse, sparseGains);
        const qint64 y = once(dense, denseGains);
        a = a < 0 ? x : std::min(a, x);
        b = b < 0 ? y : std::min(b, y);
    }
    qInfo() << "127 points" << a / 1000 << "us, 1024 points" << b / 1000 << "us";
    QVERIFY2(std::abs(double(b - a)) <= 0.05 * double(a), qPrintable(QStringLiteral("%1 %2").arg(a).arg(b)));
}

QTEST_GUILESS_MAIN(EqFitTimingTest)
#include "tst_eqfittiming.moc"
