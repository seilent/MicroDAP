#include "Biquad.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <utility>

namespace microdap {

namespace {

constexpr double guardRatio = 0.49;
constexpr double unityGainDb = 1e-9;

bool usesGain(BiquadType type)
{
    return type == BiquadType::Peak || type == BiquadType::LowShelf || type == BiquadType::HighShelf
           || type == BiquadType::MatchedPeak;
}

BiquadCoeffs matchedPeak(double f0, double gainDb, double q, double rate)
{
    if (q <= 0.0)
        return biquadUnity();
    const double amp = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * std::numbers::pi * f0 / rate;
    const double g = amp * amp;
    const double z = 1.0 / (2.0 * q * amp);
    const double r = z <= 1.0 ? std::cos(std::sqrt(1.0 - z * z) * w) : std::cosh(std::sqrt(z * z - 1.0) * w);
    const double a1 = -2.0 * std::exp(-z * w) * r;
    const double a2 = std::exp(-2.0 * z * w);
    const double pa0 = (1.0 + a1 + a2) * (1.0 + a1 + a2);
    const double pa1 = (1.0 - a1 + a2) * (1.0 - a1 + a2);
    const double pa2 = -4.0 * a2;
    const double p1 = std::sin(w / 2.0) * std::sin(w / 2.0);
    const double p0 = 1.0 - p1;
    const double p2 = 4.0 * p0 * p1;
    const double r1 = (pa0 * p0 + pa1 * p1 + pa2 * p2) * g * g;
    const double r2 = (-pa0 + pa1 + 4.0 * (p0 - p1) * pa2) * g * g;
    const double qb0 = pa0;
    const double qb2 = (r1 - r2 * p1 - qb0) / (4.0 * p1 * p1);
    const double qb1 = r2 + qb0 + 4.0 * (p1 - p0) * qb2;
    const double mid = (std::sqrt(qb0) + std::sqrt(std::max(qb1, 0.0))) / 2.0;
    if (qb1 < 0.0 || mid * mid + qb2 < 0.0)
        return biquadUnity();
    const double b0 = (mid + std::sqrt(mid * mid + qb2)) / 2.0;
    const double b1 = (std::sqrt(qb0) - std::sqrt(qb1)) / 2.0;
    return {{b0, b1, -qb2 / (4.0 * b0)}, {1.0, a1, a2}, false};
}

BiquadCoeffs normalized(std::vector<double> b, std::vector<double> a)
{
    const double a0 = a[0];
    for (double &v : b)
        v /= a0;
    for (double &v : a)
        v /= a0;
    return {std::move(b), std::move(a), false};
}

}

BiquadCoeffs biquadUnity()
{
    return {{1.0}, {1.0}, false};
}

BiquadCoeffs biquadMute()
{
    return {{0.0}, {1.0}, false};
}

bool biquadIsUnity(BiquadType type, double gainDb)
{
    return usesGain(type) && std::abs(gainDb) < unityGainDb;
}

BiquadCoeffs biquadFor(BiquadType type, double f0, double gainDb, double q, double rate)
{
    if (biquadIsUnity(type, gainDb))
        return biquadUnity();
    if (f0 >= guardRatio * rate) {
        const bool mutes = type == BiquadType::HighPass || type == BiquadType::BandPass;
        BiquadCoeffs guarded = mutes ? biquadMute() : biquadUnity();
        guarded.guarded = true;
        return guarded;
    }
    if (type == BiquadType::MatchedPeak)
        return matchedPeak(f0, gainDb, q, rate);
    const double amp = std::pow(10.0, gainDb / 40.0);
    const double w = 2.0 * std::numbers::pi * f0 / rate;
    const double c = std::cos(w);
    const double alpha = std::sin(w) / (2.0 * q);
    const double plus = amp + 1.0;
    const double minus = amp - 1.0;
    const double r = 2.0 * std::sqrt(amp) * alpha;
    switch (type) {
    case BiquadType::Peak:
        return normalized({1.0 + alpha * amp, -2.0 * c, 1.0 - alpha * amp},
                          {1.0 + alpha / amp, -2.0 * c, 1.0 - alpha / amp});
    case BiquadType::LowShelf:
        return normalized({amp * (plus - minus * c + r), 2.0 * amp * (minus - plus * c), amp * (plus - minus * c - r)},
                          {plus + minus * c + r, -2.0 * (minus + plus * c), plus + minus * c - r});
    case BiquadType::HighShelf:
        return normalized({amp * (plus + minus * c + r), -2.0 * amp * (minus + plus * c), amp * (plus + minus * c - r)},
                          {plus - minus * c + r, 2.0 * (minus - plus * c), plus - minus * c - r});
    case BiquadType::BandPass:
        return normalized({alpha, 0.0, -alpha}, {1.0 + alpha, -2.0 * c, 1.0 - alpha});
    case BiquadType::LowPass:
        return normalized({(1.0 - c) / 2.0, 1.0 - c, (1.0 - c) / 2.0}, {1.0 + alpha, -2.0 * c, 1.0 - alpha});
    case BiquadType::HighPass:
        return normalized({(1.0 + c) / 2.0, -(1.0 + c), (1.0 + c) / 2.0}, {1.0 + alpha, -2.0 * c, 1.0 - alpha});
    case BiquadType::AllPass:
        return normalized({1.0 - alpha, -2.0 * c, 1.0 + alpha}, {1.0 + alpha, -2.0 * c, 1.0 - alpha});
    case BiquadType::MatchedPeak:
        break;
    }
    return biquadUnity();
}

double biquadMagnitude(const BiquadCoeffs &coeffs, double f, double rate)
{
    const double w = 2.0 * std::numbers::pi * f / rate;
    const std::complex<double> zInv = std::polar(1.0, -w);
    auto eval = [&zInv](const std::vector<double> &p) {
        std::complex<double> sum = 0.0;
        std::complex<double> power = 1.0;
        for (double v : p) {
            sum += v * power;
            power *= zInv;
        }
        return sum;
    };
    return std::abs(eval(coeffs.b) / eval(coeffs.a));
}

}
