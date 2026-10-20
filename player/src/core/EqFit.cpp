#include "EqFit.h"

#include "Biquad.h"

#include <QElapsedTimer>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <cmath>
#include <numbers>

namespace microdap {

const std::array<double, EqFit::points> EqFit::frequencies{
    20,    21,    22,    23,    24,    26,    27,    29,    30,    32,    34,    36,    38,    40,    43,    45,
    48,    50,    53,    56,    59,    63,    66,    70,    74,    78,    83,    87,    92,    97,    103,   109,
    115,   121,   128,   136,   143,   151,   160,   169,   178,   188,   199,   210,   222,   235,   248,   262,
    277,   292,   309,   326,   345,   364,   385,   406,   429,   453,   479,   506,   534,   565,   596,   630,
    665,   703,   743,   784,   829,   875,   924,   977,   1032,  1090,  1151,  1216,  1284,  1357,  1433,  1514,
    1599,  1689,  1784,  1885,  1991,  2103,  2221,  2347,  2479,  2618,  2766,  2921,  3086,  3260,  3443,  3637,
    3842,  4058,  4287,  4528,  4783,  5052,  5337,  5637,  5955,  6290,  6644,  7018,  7414,  7831,  8272,  8738,
    9230,  9749,  10298, 10878, 11490, 12137, 12821, 13543, 14305, 15110, 15961, 16860, 17809, 18812, 19871};

namespace {

constexpr int fitBands = EqFit::bands;
constexpr int params = 3 * fitBands;
constexpr int graphicPoints = EqFit::points;
constexpr int pts = 2 * graphicPoints;
constexpr int maxTrials = 4;
constexpr std::array<double, 2> fitRates{44100.0, 96000.0};
static_assert(EqFit::bands == EqProfile::bandCount);

bool blank(char c)
{
    return c == ' ' || c == '\t';
}

bool space(char c)
{
    return blank(c) || c == '\n' || c == '\r' || c == '\v' || c == '\f';
}

bool number(const char *&p, const char *end, double &v)
{
    while (p < end && blank(*p))
        ++p;
    const char *from = p < end && *p == '+' ? p + 1 : p;
    if (from != p && from < end && (*from == '+' || *from == '-'))
        return false;
    const auto [next, error] = std::from_chars(from, end, v);
    if (error != std::errc() || next == from || !std::isfinite(v))
        return false;
    p = next;
    return true;
}

bool starts(const QByteArray &line, const char *word)
{
    const qsizetype n = qsizetype(std::strlen(word));
    return line.size() >= n && line.left(n).compare(QByteArray(word), Qt::CaseInsensitive) == 0;
}

bool readPoints(const char *p, const char *end, std::vector<double> &freqs, std::vector<double> &gains, QStringList *log)
{
    bool clamped = false;
    int dropped = 0;
    double last = -1.0;
    for (;;) {
        while (p < end && blank(*p))
            ++p;
        if (p >= end)
            break;
        double f = 0.0;
        double g = 0.0;
        if (!number(p, end, f) || !number(p, end, g))
            return false;
        while (p < end && blank(*p))
            ++p;
        if (p < end && *p == ';')
            ++p;
        else if (p < end)
            return false;
        if (f <= last)
            return false;
        last = f;
        if (f < EqParser::minFreq || f > EqParser::maxFreq) {
            ++dropped;
            continue;
        }
        const double c = std::clamp(g, EqParser::minGain, EqParser::maxGain);
        if (c != g && !clamped) {
            clamped = true;
            if (log)
                log->append(QStringLiteral("gain %1 clamped to %2").arg(g).arg(c));
        }
        freqs.push_back(f);
        gains.push_back(c);
    }
    if (dropped && log)
        log->append(QStringLiteral("%1 points outside 20 to 20000 Hz dropped").arg(dropped));
    return true;
}

struct Band
{
    bool on = false;
    BiquadType type = BiquadType::Peak;
    double freq = 1000.0;
    double gain = 0.0;
    double q = 1.4;
};

struct Fit
{
    double cr[pts];
    double ci[pts];
    double c2r[pts];
    double c2i[pts];
    double tgt[pts];
    bool on[fitBands];
    BiquadType types[fitBands];
    double x[params];
    double pw[fitBands][pts];
    double jac[params][pts];
    double normal[params][params];
    double aug[params][params + 1];
    double trial[fitBands][pts];
    double lam = 1e-2;
    double cost = 0.0;
    long evals = 0;

    Band unpack(const double *v, int b) const
    {
        Band e;
        e.on = on[b];
        e.type = types[b];
        e.freq = std::clamp(std::exp(std::clamp(v[3 * b], 2.9, 10.0)), EqParser::minFreq, EqParser::maxFreq);
        e.gain = std::clamp(v[3 * b + 1], EqParser::minGain, EqParser::maxGain);
        e.q = std::clamp(std::exp(std::clamp(v[3 * b + 2], -2.4, 2.4)), EqParser::minQ, EqParser::maxQ);
        return e;
    }

    void bandPower(const double *v, int b, double *out)
    {
        ++evals;
        const Band e = unpack(v, b);
        for (int h = 0; h < 2; ++h) {
            double *o = out + h * graphicPoints;
            const BiquadCoeffs c = e.on ? biquadFor(e.type, e.freq, e.gain, e.q, fitRates[h]) : biquadUnity();
            if (c.b.size() < 3 || c.a.size() < 3) {
                std::fill(o, o + graphicPoints, 1.0);
                continue;
            }
            for (int p = 0; p < graphicPoints; ++p) {
                const int k = h * graphicPoints + p;
                const double nr = c.b[0] + c.b[1] * cr[k] + c.b[2] * c2r[k];
                const double ni = c.b[1] * ci[k] + c.b[2] * c2i[k];
                const double dr = 1.0 + c.a[1] * cr[k] + c.a[2] * c2r[k];
                const double di = c.a[1] * ci[k] + c.a[2] * c2i[k];
                o[p] = (nr * nr + ni * ni) / (dr * dr + di * di);
            }
        }
    }

    void response(const double (*bp)[pts], int moved, const double *mp, double *db) const
    {
        for (int p = 0; p < pts; ++p) {
            double m = 1.0;
            for (int b = 0; b < fitBands; ++b)
                m *= b == moved ? mp[p] : bp[b][p];
            db[p] = 10.0 * std::log10(m);
        }
    }

    static void centre(double *v)
    {
        double m = 0.0;
        for (int p = 0; p < pts; ++p)
            m += v[p];
        m /= pts;
        for (int p = 0; p < pts; ++p)
            v[p] -= m;
    }

    double costOf(const double *resp) const
    {
        double r[pts];
        for (int p = 0; p < pts; ++p)
            r[p] = resp[p] - tgt[p];
        centre(r);
        double c = 0.0;
        for (int p = 0; p < pts; ++p)
            c += r[p] * r[p];
        return c;
    }

    void cascade(const double *v, double (*bp)[pts], double *resp)
    {
        for (int b = 0; b < fitBands; ++b)
            bandPower(v, b, bp[b]);
        response(bp, -1, nullptr, resp);
    }

    static bool solve(double (*m)[params + 1], double *out)
    {
        for (int k = 0; k < params; ++k) {
            int piv = k;
            for (int i = k + 1; i < params; ++i) {
                if (std::fabs(m[i][k]) > std::fabs(m[piv][k]))
                    piv = i;
            }
            if (m[piv][k] == 0.0)
                return false;
            if (piv != k) {
                for (int j = 0; j <= params; ++j)
                    std::swap(m[k][j], m[piv][j]);
            }
            for (int i = k + 1; i < params; ++i) {
                const double f = m[i][k] / m[k][k];
                for (int j = k; j <= params; ++j)
                    m[i][j] -= f * m[k][j];
            }
        }
        for (int i = params - 1; i >= 0; --i) {
            double s = m[i][params];
            for (int j = i + 1; j < params; ++j)
                s -= m[i][j] * out[j];
            out[i] = s / m[i][i];
        }
        return true;
    }

    void seed(const double *half)
    {
        double sorted[graphicPoints];
        std::copy(half, half + graphicPoints, sorted);
        std::sort(sorted, sorted + graphicPoints);
        const double med = sorted[graphicPoints / 2];
        for (int p = 0; p < pts; ++p)
            tgt[p] = half[p % graphicPoints] - med;
        const auto at = [this](double f0) {
            const double lf = std::log(f0);
            for (int k = 1; k < graphicPoints; ++k) {
                if (lf <= std::log(EqFit::frequencies[k])) {
                    const double a = std::log(EqFit::frequencies[k - 1]);
                    const double b = std::log(EqFit::frequencies[k]);
                    return tgt[k - 1] + (tgt[k] - tgt[k - 1]) * (lf - a) / (b - a);
                }
            }
            return tgt[graphicPoints - 1];
        };
        on[0] = true;
        types[0] = BiquadType::LowShelf;
        x[0] = std::log(105.0);
        x[1] = at(60.0);
        x[2] = std::log(0.7);
        on[1] = true;
        types[1] = BiquadType::HighShelf;
        x[3] = std::log(10000.0);
        x[4] = at(14000.0);
        x[5] = std::log(0.7);
        for (int b = 2; b < fitBands; ++b) {
            on[b] = false;
            types[b] = BiquadType::Peak;
            x[3 * b] = std::log(1000.0);
            x[3 * b + 1] = 0.0;
            x[3 * b + 2] = std::log(1.4);
        }
        double resp[pts];
        for (int b = 2; b < fitBands; ++b) {
            cascade(x, pw, resp);
            int best = 0;
            double bv = -1.0;
            for (int k = 0; k < graphicPoints; ++k) {
                if (std::fabs(tgt[k] - resp[k]) > bv) {
                    bv = std::fabs(tgt[k] - resp[k]);
                    best = k;
                }
            }
            on[b] = true;
            x[3 * b] = std::log(EqFit::frequencies[best]);
            x[3 * b + 1] = tgt[best] - resp[best];
        }
        cascade(x, pw, resp);
        cost = costOf(resp);
    }

    bool iterate(int &trials)
    {
        double base[pts];
        double tmp[pts];
        double resp[pts];
        double r[pts];
        response(pw, -1, nullptr, base);
        for (int p = 0; p < pts; ++p)
            r[p] = base[p] - tgt[p];
        centre(r);
        for (int i = 0; i < params; ++i) {
            double d[params];
            std::copy(x, x + params, d);
            d[i] += 1e-4;
            const int b = i / 3;
            bandPower(d, b, tmp);
            response(pw, b, tmp, resp);
            for (int p = 0; p < pts; ++p)
                jac[i][p] = (resp[p] - base[p]) / 1e-4;
            centre(jac[i]);
        }
        double g[params];
        for (int i = 0; i < params; ++i) {
            for (int j = i; j < params; ++j) {
                double s = 0.0;
                for (int p = 0; p < pts; ++p)
                    s += jac[i][p] * jac[j][p];
                normal[i][j] = normal[j][i] = s;
            }
            double s = 0.0;
            for (int p = 0; p < pts; ++p)
                s += jac[i][p] * r[p];
            g[i] = -s;
        }
        trials = 0;
        double step[params] = {};
        for (int k = 0; k < maxTrials; ++k) {
            double xn[params];
            for (int i = 0; i < params; ++i) {
                std::copy(normal[i], normal[i] + params, aug[i]);
                aug[i][i] += lam * (normal[i][i] + 1e-9);
                aug[i][params] = g[i];
            }
            ++trials;
            if (!solve(aug, step))
                return false;
            for (int i = 0; i < params; ++i)
                xn[i] = x[i] + step[i];
            for (int b = 0; b < fitBands; ++b) {
                const Band e = unpack(xn, b);
                xn[3 * b] = std::log(e.freq);
                xn[3 * b + 1] = e.gain;
                xn[3 * b + 2] = std::log(e.q);
            }
            cascade(xn, trial, resp);
            const double cn = costOf(resp);
            if (cn < cost) {
                std::copy(xn, xn + params, x);
                std::copy(&trial[0][0], &trial[0][0] + fitBands * pts, &pw[0][0]);
                cost = cn;
                lam = std::max(lam / 3.0, 1e-7);
                break;
            }
            lam *= 4.0;
        }
        double big = 0.0;
        for (const double s : step)
            big = std::max(big, std::fabs(s));
        return lam <= 1e8 && big >= 1e-6;
    }
};

EqBand::Type bandType(BiquadType type)
{
    return type == BiquadType::LowShelf ? EqBand::LowShelf : type == BiquadType::HighShelf ? EqBand::HighShelf
                                                                                            : EqBand::Peak;
}

}

bool EqFit::parse(const QByteArray &text, std::vector<double> &freqs, std::vector<double> &gains, QStringList *log)
{
    freqs.clear();
    gains.clear();
    qsizetype pos = text.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    bool seen = false;
    bool preamp = false;
    while (pos <= text.size()) {
        qsizetype nl = text.indexOf('\n', pos);
        if (nl < 0)
            nl = text.size();
        QByteArray line = text.mid(pos, nl - pos);
        pos = nl + 1;
        qsizetype end = line.size();
        while (end > 0 && space(line.at(end - 1)))
            --end;
        qsizetype lead = 0;
        while (lead < end && space(line.at(lead)))
            ++lead;
        line = line.mid(lead, end - lead);
        if (line.isEmpty())
            continue;
        if (!seen && !preamp && starts(line, "preamp:")) {
            preamp = true;
            continue;
        }
        if (seen || !starts(line, "graphiceq:"))
            return false;
        seen = true;
        if (!readPoints(line.constData() + 10, line.constData() + line.size(), freqs, gains, log))
            return false;
    }
    const qsizetype n = qsizetype(freqs.size());
    if (!seen || n < minPoints || n > maxPoints) {
        if (log && seen)
            log->append(QStringLiteral("%1 points, 31 to 1024 needed").arg(n));
        return false;
    }
    return true;
}

void EqFit::response(const EqProfile &profile, double rate, const double *at, int n, double *db)
{
    std::fill(db, db + n, 0.0);
    for (int b = 0; b < profile.count; ++b) {
        const EqBand &band = profile.bands[b];
        if (!band.enabled)
            continue;
        const BiquadCoeffs c = biquadFor(BiquadType(int(band.type)), band.freq, band.gain, band.q, rate);
        for (int i = 0; i < n; ++i) {
            const double m = biquadMagnitude(c, at[i], rate);
            db[i] += 20.0 * std::log10(m);
        }
    }
}

EqFitEnd EqFit::fit(const std::vector<double> &freqs, const std::vector<double> &gains, EqProfile &out,
                    const EqFitOptions &options, EqFitStats *stats)
{
    QElapsedTimer clock;
    clock.start();
    std::vector<Fit> heap(1);
    Fit &f = heap[0];
    double half[graphicPoints];
    const size_t n = std::min(freqs.size(), gains.size());
    for (int k = 0; k < graphicPoints; ++k) {
        const double f0 = frequencies[k];
        size_t j = 0;
        while (j < n && freqs[j] < f0)
            ++j;
        if (n == 0) {
            half[k] = 0.0;
        } else if (j < n && freqs[j] == f0) {
            half[k] = gains[j];
        } else if (j == 0) {
            half[k] = gains[0];
        } else if (j == n) {
            half[k] = gains[n - 1];
        } else {
            const double a = std::log(freqs[j - 1]);
            const double b = std::log(freqs[j]);
            half[k] = gains[j - 1] + (gains[j] - gains[j - 1]) * (std::log(f0) - a) / (b - a);
        }
    }
    for (int h = 0; h < 2; ++h) {
        for (int p = 0; p < graphicPoints; ++p) {
            const int k = h * graphicPoints + p;
            const double w = 2.0 * std::numbers::pi * frequencies[p] / fitRates[h];
            f.cr[k] = std::cos(w);
            f.ci[k] = -std::sin(w);
            f.c2r[k] = std::cos(2.0 * w);
            f.c2i[k] = -std::sin(2.0 * w);
        }
    }
    f.seed(half);
    EqFitStats st;
    st.points = pts;
    const auto cancelled = [&options] { return options.cancel && options.cancel->load(); };
    for (;;) {
        if (cancelled()) {
            if (stats)
                *stats = st;
            return EqFitEnd::Cancelled;
        }
        if (st.iterations >= options.maxIters)
            break;
        int trials = 0;
        const bool more = f.iterate(trials);
        ++st.iterations;
        st.trials += trials;
        st.maxTrials = std::max(st.maxTrials, trials);
        if (options.onIteration)
            options.onIteration(st.iterations);
        if (!more || clock.elapsed() >= options.budgetMs)
            break;
    }
    if (cancelled()) {
        if (stats)
            *stats = st;
        return EqFitEnd::Cancelled;
    }
    EqProfile fitted = EqProfile::flat();
    for (int b = 0; b < fitBands; ++b) {
        const Band e = f.unpack(f.x, b);
        EqBand &band = fitted.bands[b];
        band.enabled = true;
        band.type = bandType(e.type);
        band.freq = std::clamp(std::round(e.freq), EqParser::minFreq, EqParser::maxFreq);
        band.gain = std::clamp(std::round(e.gain * 10.0) / 10.0, EqParser::minGain, EqParser::maxGain);
        band.q = std::clamp(std::round(e.q * 100.0) / 100.0, EqParser::minQ, EqParser::maxQ);
        band.freqIndex = -1;
    }
    std::stable_sort(fitted.bands.begin(), fitted.bands.end(),
                     [](const EqBand &a, const EqBand &b) { return a.freq < b.freq; });
    fitted.count = fitBands;
    st.evals = f.evals;
    st.rms = std::sqrt(f.cost / pts);
    const double peak = fitted.boostPeak();
    const double pre = peak > 0.0 ? -std::ceil((peak + 0.05) * 10.0) / 10.0 : 0.0;
    st.boost = peak;
    if (stats)
        *stats = st;
    if (pre < EqParser::minPreamp)
        return EqFitEnd::Boost;
    fitted.preamp = pre;
    out = fitted;
    return EqFitEnd::Done;
}

}
