#include "Eq.h"
#include "EqFit.h"

#include <QFile>
#include <QTest>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

using namespace microdap;

namespace {

EqBand band(EqBand::Type type, double freq, double gain, double q)
{
    EqBand b;
    b.type = type;
    b.freq = freq;
    b.gain = gain;
    b.q = q;
    return b;
}

std::vector<EqProfile> targets()
{
    using T = EqBand;
    const std::vector<std::vector<EqBand>> sets = {
        {band(T::LowShelf, 105, 6, 0.7), band(T::Peak, 2400, -3.2, 1.4), band(T::HighShelf, 8000, 2, 0.7)},
        {band(T::Peak, 60, 4, 0.8), band(T::Peak, 300, -2, 1), band(T::Peak, 1000, 1.5, 2), band(T::Peak, 3000, -4, 3),
         band(T::Peak, 6000, 3, 2)},
        {band(T::LowShelf, 80, -4, 0.7), band(T::HighShelf, 5000, -6, 0.7), band(T::Peak, 1500, 2, 0.7)},
        {band(T::LowShelf, 50, 2, 0.7), band(T::Peak, 40, 5, 1.5), band(T::Peak, 120, -3, 1), band(T::Peak, 400, 1, 2),
         band(T::Peak, 700, 2, 1.2), band(T::Peak, 1300, -1, 3), band(T::Peak, 2000, -2.5, 2.5), band(T::Peak, 4500, 4, 4),
         band(T::Peak, 7000, -3, 3), band(T::HighShelf, 9000, 1.5, 0.7)},
        {band(T::LowShelf, 200, 3, 0.5), band(T::Peak, 3500, 5, 1)},
        {band(T::Peak, 30, -6, 0.7), band(T::Peak, 250, 3, 0.9), band(T::Peak, 500, -1, 2), band(T::Peak, 900, -1.5, 1.4),
         band(T::Peak, 1700, 1, 3), band(T::Peak, 2700, 3.5, 1.8), band(T::Peak, 4000, -2, 4), band(T::Peak, 5500, -5, 2.5),
         band(T::Peak, 6500, 1, 2), band(T::HighShelf, 7500, -3, 0.7)},
    };
    std::vector<EqProfile> out;
    for (const auto &set : sets) {
        EqProfile p = EqProfile::flat();
        for (size_t i = 0; i < set.size(); ++i)
            p.bands[i] = set[i];
        p.count = int(set.size());
        out.push_back(p);
    }
    return out;
}

std::vector<double> autoEqFreqs()
{
    return {EqFit::frequencies.begin(), EqFit::frequencies.end()};
}

std::vector<double> curve(const EqProfile &p, const std::vector<double> &freqs)
{
    std::vector<double> db(freqs.size());
    EqFit::response(p, 96000, freqs.data(), int(freqs.size()), db.data());
    for (double &v : db)
        v = std::round(v * 10) / 10;
    return db;
}

QByteArray graphicText(const std::vector<double> &freqs, const std::vector<double> &gains)
{
    QByteArray s = "GraphicEQ: ";
    for (size_t i = 0; i < freqs.size(); ++i) {
        if (i)
            s += "; ";
        s += QByteArray::number(freqs[i], 'f', 2) + ' ' + QByteArray::number(gains[i], 'f', 1);
    }
    return s + "\n";
}

QByteArray graphicText(const EqProfile &p)
{
    return graphicText(autoEqFreqs(), curve(p, autoEqFreqs()));
}

double fitRms(const EqProfile &p, double rate, const std::vector<double> &f, const std::vector<double> &t, double &mx)
{
    std::vector<double> r(f.size());
    EqFit::response(p, rate, f.data(), int(f.size()), r.data());
    double m = 0;
    for (size_t i = 0; i < f.size(); ++i)
        m += (r[i] -= t[i]);
    m /= double(f.size());
    double s = 0;
    mx = 0;
    for (double &v : r) {
        v -= m;
        s += v * v;
        mx = std::max(mx, std::fabs(v));
    }
    return std::sqrt(s / double(f.size()));
}

EqFitOptions options(int iters)
{
    EqFitOptions o;
    o.budgetMs = 600000;
    o.maxIters = iters;
    return o;
}

double cascadePeak(const EqProfile &p, double rate)
{
    std::vector<double> at;
    const double nyquist = rate / 2;
    for (double f = 10; f < nyquist; f *= std::pow(nyquist / 10, 1.0 / 4000))
        at.push_back(f);
    at.push_back(nyquist);
    for (int i = 0; i < p.count; ++i) {
        if (p.bands[i].freq < nyquist)
            at.push_back(p.bands[i].freq);
    }
    std::vector<double> db(at.size());
    EqFit::response(p, rate, at.data(), int(at.size()), db.data());
    return p.preamp + *std::max_element(db.begin(), db.end());
}

bool sameBands(const EqProfile &a, const EqProfile &b)
{
    bool equal = a.count == b.count;
    for (int i = 0; i < a.count && equal; ++i)
        equal = a.bands[i].type == b.bands[i].type && a.bands[i].freq == b.bands[i].freq
                && a.bands[i].gain == b.bands[i].gain && a.bands[i].q == b.bands[i].q;
    return equal;
}

QByteArray fixture(const QString &name)
{
    QFile file(QStringLiteral(MICRODAP_FIXTURE_EQFIT "/") + name);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

}

class EqFitTest : public QObject
{
    Q_OBJECT

private slots:
    void parse();
    void fitsTargets();
    void capAndCancel();
    void boost();
    void densePoints();
    void savedPreset();
    void reference();
};

void EqFitTest::parse()
{
    std::vector<double> f;
    std::vector<double> g;
    const std::vector<double> ff = autoEqFreqs();
    const std::vector<double> gg = curve(targets()[0], ff);
    QStringList log;
    const QByteArray plain = graphicText(ff, gg);
    QVERIFY(EqFit::parse(plain, f, g, &log));
    QVERIFY(f == ff && g == gg && log.isEmpty());
    const QByteArray crlf = "\xEF\xBB\xBFPreamp: -6.2 dB\r\n" + plain.left(plain.size() - 1) + ";\r\n";
    QVERIFY(EqFit::parse(crlf, f, g));
    QVERIFY(f == ff && g == gg);
    log.clear();
    QVERIFY(!EqFit::parse(graphicText({ff.begin(), ff.begin() + 30}, {gg.begin(), gg.begin() + 30}), f, g, &log));
    QCOMPARE(log, QStringList{QStringLiteral("30 points, 31 to 1024 needed")});
    QVERIFY(EqFit::parse(graphicText({ff.begin(), ff.begin() + 31}, {gg.begin(), gg.begin() + 31}), f, g));
    QCOMPARE(int(f.size()), 31);
    std::vector<double> fo = ff;
    std::vector<double> go = gg;
    fo.insert(fo.begin(), {10, 15});
    go.insert(go.begin(), {1, 1});
    fo.push_back(21000);
    go.push_back(1);
    log.clear();
    QVERIFY(EqFit::parse(graphicText(fo, go), f, g, &log));
    QVERIFY(f == ff);
    QCOMPARE(log, QStringList{QStringLiteral("3 points outside 20 to 20000 Hz dropped")});
    std::vector<double> gc = gg;
    gc[3] = 20;
    gc[90] = -30;
    log.clear();
    QVERIFY(EqFit::parse(graphicText(ff, gc), f, g, &log));
    QCOMPARE(g[3], 12.0);
    QCOMPARE(g[90], -24.0);
    QCOMPARE(log, QStringList{QStringLiteral("gain 20 clamped to 12")});
    std::vector<double> fd = ff;
    std::swap(fd[40], fd[41]);
    QVERIFY(!EqFit::parse(graphicText(fd, gg), f, g));
    fd = ff;
    fd[41] = fd[40];
    QVERIFY(!EqFit::parse(graphicText(fd, gg), f, g));
    std::vector<double> fm;
    std::vector<double> gm;
    for (int i = 0; i < 1025; ++i) {
        fm.push_back(20 + i * 19.5);
        gm.push_back(0);
    }
    QVERIFY(!EqFit::parse(graphicText(fm, gm), f, g));
    fm.pop_back();
    gm.pop_back();
    QVERIFY(EqFit::parse(graphicText(fm, gm), f, g));
    QCOMPARE(int(f.size()), 1024);
    QVERIFY(!EqFit::parse(plain + "Filter 1: ON PK Fc 100 Hz Gain 1 dB Q 1\n", f, g));
    QVERIFY(!EqFit::parse("Preamp: -3 dB\n", f, g));
    QVERIFY(!EqFit::parse("GraphicEQ: 20 1; 30 x; 40 1\n", f, g));
    QVERIFY(!EqFit::parse("GraphicEQ: 20 1;; 30 1\n", f, g));
    QVERIFY(!EqFit::parse(plain + "Preamp: -3 dB\n", f, g));
    QVERIFY(!EqParser::parse(QString::fromUtf8(plain)));
    QVERIFY(!EqFit::parse("Preamp: -6.4 dB\nFilter 1: ON LSC Fc 105 Hz Gain 5.5 dB Q 0.70\n", f, g));
}

void EqFitTest::fitsTargets()
{
    const std::vector<double> ff = autoEqFreqs();
    int n = 0;
    for (const EqProfile &p : targets()) {
        ++n;
        const std::vector<double> t = curve(p, ff);
        for (const int iters : {200, 10}) {
            EqProfile o;
            EqFitStats st;
            const EqFitEnd end = EqFit::fit(ff, t, o, options(iters), &st);
            double worst = 0;
            double worstRms = 0;
            for (const double rate : {44100.0, 48000.0, 96000.0}) {
                double mx = 0;
                worstRms = std::max(worstRms, fitRms(o, rate, ff, t, mx));
                worst = std::max(worst, mx);
            }
            bool bands = o.count == 10;
            for (const EqBand &b : o.bands)
                bands = bands && b.enabled && b.freq == std::round(b.freq)
                        && std::fabs(b.gain * 10 - std::round(b.gain * 10)) < 1e-9
                        && std::fabs(b.q * 100 - std::round(b.q * 100)) < 1e-9;
            QVERIFY2(end == EqFitEnd::Done && bands && st.maxTrials <= 4, qPrintable(QString::number(n)));
            if (iters == 200) {
                QVERIFY2(worstRms < 0.5 && worst < 1.5 && st.points == 254,
                         qPrintable(QStringLiteral("target %1: %2 rms %3 max").arg(n).arg(worstRms).arg(worst)));
            } else {
                QVERIFY2(worstRms < 0.6 && st.iterations == 10,
                         qPrintable(QStringLiteral("target %1: %2 rms").arg(n).arg(worstRms)));
            }
        }
    }
}

void EqFitTest::capAndCancel()
{
    const std::vector<double> ff = autoEqFreqs();
    const std::vector<double> t = curve(targets()[3], ff);
    EqFitOptions quick = options(200);
    quick.budgetMs = 0;
    EqProfile o;
    EqFitStats st;
    QCOMPARE(EqFit::fit(ff, t, o, quick, &st), EqFitEnd::Done);
    QCOMPARE(st.iterations, 1);
    QCOMPARE(o.count, 10);
    QCOMPARE(EqFitOptions().budgetMs, 2500);
    std::atomic<bool> stop{true};
    EqFitOptions cut = options(200);
    cut.cancel = &stop;
    EqProfile untouched = EqProfile::flat();
    QCOMPARE(EqFit::fit(ff, t, untouched, cut, &st), EqFitEnd::Cancelled);
    QCOMPARE(st.iterations, 0);
    QVERIFY(untouched == EqProfile::flat());
    stop = false;
    cut.onIteration = [&stop](int i) { stop = i == 3; };
    QCOMPARE(EqFit::fit(ff, t, o, cut, &st), EqFitEnd::Cancelled);
    QCOMPARE(st.iterations, 3);
}

void EqFitTest::boost()
{
    const std::vector<double> ff = autoEqFreqs();
    std::vector<double> bass(ff.size());
    std::vector<double> spike(ff.size());
    for (size_t i = 0; i < ff.size(); ++i) {
        bass[i] = ff[i] < 100 ? 12 : ff[i] < 200 ? 12 - 20 * std::log2(ff[i] / 100) : -8;
        spike[i] = ff[i] > 900 && ff[i] < 1100 ? 12 : -24;
    }
    EqProfile o;
    EqFitStats st;
    const EqFitEnd end = EqFit::fit(ff, bass, o, options(200), &st);
    bool clean = end == EqFitEnd::Done && o.preamp >= EqParser::minPreamp;
    for (const double rate : EqProfile::boostRates)
        clean = clean && cascadePeak(o, rate) <= 0;
    QVERIFY2(clean || end == EqFitEnd::Boost,
             qPrintable(QStringLiteral("%1 preamp %2 boost %3").arg(int(end)).arg(o.preamp).arg(st.boost)));
    QCOMPARE(EqFit::fit(ff, spike, o, options(200), &st), EqFitEnd::Boost);
    QVERIFY2(st.boost > 24, qPrintable(QString::number(st.boost)));
}

void EqFitTest::densePoints()
{
    std::vector<double> dense = autoEqFreqs();
    for (int i = 0; dense.size() < 1024; ++i)
        dense.push_back(20.05 + 19.5 * i);
    std::sort(dense.begin(), dense.end());
    std::vector<double> df;
    std::vector<double> dg;
    std::vector<double> sf;
    std::vector<double> sg;
    QVERIFY(EqFit::parse(graphicText(dense, curve(targets()[5], dense)), df, dg));
    QCOMPARE(int(df.size()), 1024);
    QVERIFY(EqFit::parse(graphicText(targets()[5]), sf, sg));
    EqProfile a;
    EqProfile b;
    EqFitStats sa;
    EqFitStats sb;
    QCOMPARE(EqFit::fit(df, dg, a, options(200), &sa), EqFitEnd::Done);
    QCOMPARE(EqFit::fit(sf, sg, b, options(200), &sb), EqFitEnd::Done);
    QVERIFY(sameBands(a, b) && a.preamp == b.preamp);
    QCOMPARE(sa.points, 254);
    QCOMPARE(sa.evals, sb.evals);
    QCOMPARE(sa.iterations, sb.iterations);
}

void EqFitTest::savedPreset()
{
    const std::vector<double> ff = autoEqFreqs();
    for (const EqProfile &p : targets()) {
        EqProfile o;
        QCOMPARE(EqFit::fit(ff, curve(p, ff), o, options(200)), EqFitEnd::Done);
        for (const double rate : EqProfile::boostRates)
            QVERIFY2(cascadePeak(o, rate) <= 0, qPrintable(QStringLiteral("%1 Hz %2 dB").arg(rate).arg(cascadePeak(o, rate))));
        const std::optional<EqProfile> back = EqParser::parse(EqControl::autoEqText(o));
        QVERIFY(back);
        QVERIFY(sameBands(*back, o) && back->preamp == o.preamp);
    }
}

void EqFitTest::reference()
{
    const QList<QByteArray> lines = fixture(QStringLiteral("reference.txt")).split('\n');
    QVERIFY(lines.size() > 2);
    QCOMPARE(lines.at(0), QByteArray("flatdap 17558f5fbfbe049f66cdf31c84717fbdf9df9aa9"));
    QVERIFY(lines.at(1).startsWith("command "));
    QList<QByteArray> blocks;
    for (qsizetype i = 2; i < lines.size(); ++i) {
        if (lines.at(i).startsWith("target "))
            blocks.append(QByteArray());
        else if (!blocks.isEmpty())
            blocks.last() += lines.at(i) + '\n';
    }
    QCOMPARE(int(blocks.size()), 6);
    const std::vector<double> ff = autoEqFreqs();
    double sum = 0;
    for (int n = 1; n <= 6; ++n) {
        const std::optional<EqProfile> ref = EqParser::parse(QString::fromUtf8(blocks.at(n - 1)));
        QVERIFY(ref);
        QCOMPARE(ref->count, 10);
        std::vector<double> f;
        std::vector<double> g;
        QVERIFY(EqFit::parse(fixture(QStringLiteral("target-%1 GraphicEQ.txt").arg(n)), f, g));
        EqProfile o;
        QCOMPARE(EqFit::fit(f, g, o, options(200)), EqFitEnd::Done);
        std::vector<double> theirs(ff.size());
        EqFit::response(*ref, 96000, ff.data(), int(ff.size()), theirs.data());
        double mx = 0;
        const double rms = fitRms(o, 96000, ff, theirs, mx);
        sum += rms;
        qInfo().noquote() << "target" << n << "rms against flatdap" << QString::number(rms, 'f', 4) << "preamp"
                          << o.preamp << ref->preamp << (sameBands(o, *ref) ? "same bands" : "other bands");
    }
    QVERIFY2(sum / 6 <= 0.05, qPrintable(QString::number(sum / 6)));
}

QTEST_GUILESS_MAIN(EqFitTest)
#include "tst_eqfit.moc"
