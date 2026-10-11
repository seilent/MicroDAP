#include "Biquad.h"

#include <QTest>

#include <array>
#include <cmath>
#include <vector>

using namespace casdap;

namespace {

constexpr double rate = 48000.0;
constexpr double tolerance = 1e-9;

constexpr std::array<BiquadType, 7> allTypes{
    BiquadType::Peak, BiquadType::LowShelf, BiquadType::HighShelf, BiquadType::BandPass,
    BiquadType::LowPass, BiquadType::HighPass, BiquadType::AllPass,
};

bool isGainType(BiquadType type)
{
    return type == BiquadType::Peak || type == BiquadType::LowShelf || type == BiquadType::HighShelf;
}

bool nearVector(const std::vector<double> &actual, const std::vector<double> &expected)
{
    if (actual.size() != expected.size())
        return false;
    for (size_t i = 0; i < actual.size(); ++i) {
        if (std::abs(actual[i] - expected[i]) > tolerance)
            return false;
    }
    return true;
}

double dbToLinear(double db)
{
    return std::pow(10.0, db / 20.0);
}

}

class BiquadTest : public QObject
{
    Q_OBJECT

private slots:
    void unityRules();
    void guard();
    void cookbookProperties();
    void probeVectors();
};

void BiquadTest::unityRules()
{
    QCOMPARE(biquadUnity().b, std::vector<double>{1.0});
    QCOMPARE(biquadUnity().a, std::vector<double>{1.0});
    QVERIFY(!biquadUnity().guarded);
    QCOMPARE(biquadMute().b, std::vector<double>{0.0});
    QCOMPARE(biquadMute().a, std::vector<double>{1.0});
    QVERIFY(!biquadMute().guarded);

    for (BiquadType type : allTypes) {
        QCOMPARE(biquadIsUnity(type, 0.0), isGainType(type));
        QCOMPARE(biquadIsUnity(type, 1e-10), isGainType(type));
        QVERIFY(!biquadIsUnity(type, 1e-9));
        QVERIFY(!biquadIsUnity(type, 0.1));
        QVERIFY(!biquadIsUnity(type, -0.1));
        const BiquadCoeffs flat = biquadFor(type, 1000.0, 0.0, 0.71, rate);
        QVERIFY(!flat.guarded);
        if (isGainType(type)) {
            QCOMPARE(flat.b, std::vector<double>{1.0});
            QCOMPARE(flat.a, std::vector<double>{1.0});
        } else {
            QCOMPARE(flat.b.size(), size_t(3));
            QCOMPARE(flat.a.size(), size_t(3));
        }
    }
}

void BiquadTest::guard()
{
    const double edge = 0.49 * rate;
    for (BiquadType type : allTypes) {
        const BiquadCoeffs at = biquadFor(type, edge, 6.0, 0.71, rate);
        QVERIFY(at.guarded);
        const bool mutes = type == BiquadType::HighPass || type == BiquadType::BandPass;
        QCOMPARE(at.b, std::vector<double>{mutes ? 0.0 : 1.0});
        QCOMPARE(at.a, std::vector<double>{1.0});

        const BiquadCoeffs above = biquadFor(type, 30000.0, 6.0, 0.71, rate);
        QVERIFY(above.guarded);
        QCOMPARE(above.b, at.b);

        const BiquadCoeffs below = biquadFor(type, 0.489 * rate, 6.0, 0.71, rate);
        QVERIFY(!below.guarded);
        QCOMPARE(below.b.size(), size_t(3));
        QCOMPARE(below.a.size(), size_t(3));
        QCOMPARE(below.a[0], 1.0);

        const BiquadCoeffs flatAt = biquadFor(type, edge, 0.0, 0.71, rate);
        if (isGainType(type)) {
            QVERIFY(!flatAt.guarded);
            QCOMPARE(flatAt.b, std::vector<double>{1.0});
            QCOMPARE(flatAt.a, std::vector<double>{1.0});
        } else {
            QVERIFY(flatAt.guarded);
        }
    }
}

void BiquadTest::cookbookProperties()
{
    const double f0 = 1000.0;
    const double nyquist = rate / 2.0;
    for (double gain : {6.0, -9.5, 12.0}) {
        for (double q : {0.71, 2.0}) {
            const double expected = dbToLinear(gain);
            QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::Peak, f0, gain, q, rate), f0, rate) - expected)
                    < tolerance);
            QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::LowShelf, f0, gain, q, rate), 0.0, rate) - expected)
                    < tolerance);
            QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::HighShelf, f0, gain, q, rate), nyquist, rate)
                             - expected)
                    < tolerance);
        }
    }
    for (double q : {0.71, 2.0, 8.0}) {
        QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::LowPass, f0, 6.0, q, rate), f0, rate) - q) < tolerance);
        QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::HighPass, f0, 6.0, q, rate), f0, rate) - q) < tolerance);
        QVERIFY(std::abs(biquadMagnitude(biquadFor(BiquadType::BandPass, f0, 6.0, q, rate), f0, rate) - 1.0)
                < tolerance);
        const BiquadCoeffs allPass = biquadFor(BiquadType::AllPass, f0, 6.0, q, rate);
        for (double f : {20.0, 250.0, 1000.0, 4000.0, 20000.0})
            QVERIFY(std::abs(biquadMagnitude(allPass, f, rate) - 1.0) < tolerance);
    }
}

void BiquadTest::probeVectors()
{
    struct Vector
    {
        BiquadType type;
        std::vector<double> b;
        std::vector<double> a;
    };
    const std::vector<Vector> vectors{
        {BiquadType::Peak,
         {1.0608088779813332, -1.8617384469608247, 0.8169944366488713},
         {1.0, -1.8617384469608247, 0.8778033146302044}},
        {BiquadType::LowShelf,
         {1.0324507425339748, -1.839397631544125, 0.8294067875967484},
         {1.0, -1.844999273617341, 0.8562558880575075}},
        {BiquadType::HighShelf,
         {1.9325496440361383, -3.565552689476126, 1.6547570116693835},
         {1.0, -1.781583910753588, 0.8033378769829835}},
        {BiquadType::BandPass,
         {0.08418186862948991, 0.0, -0.08418186862948991},
         {1.0, -1.815966360600515, 0.8316362627410203}},
        {BiquadType::LowPass,
         {0.0039174755351263245, 0.007834951070252649, 0.0039174755351263245},
         {1.0, -1.815966360600515, 0.8316362627410203}},
        {BiquadType::HighPass,
         {0.9119006558353839, -1.8238013116707679, 0.9119006558353839},
         {1.0, -1.815966360600515, 0.8316362627410203}},
        {BiquadType::AllPass,
         {0.8316362627410203, -1.815966360600515, 1.0},
         {1.0, -1.815966360600515, 0.8316362627410203}},
    };
    for (const Vector &v : vectors) {
        const BiquadCoeffs coeffs = biquadFor(v.type, 1000.0, 6.0, 0.71, rate);
        QVERIFY2(nearVector(coeffs.b, v.b), qPrintable(QStringLiteral("b of type %1").arg(int(v.type))));
        QVERIFY2(nearVector(coeffs.a, v.a), qPrintable(QStringLiteral("a of type %1").arg(int(v.type))));
        QVERIFY(!coeffs.guarded);
    }
}

QTEST_GUILESS_MAIN(BiquadTest)
#include "tst_biquad.moc"
