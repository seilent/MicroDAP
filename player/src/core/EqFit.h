#pragma once

#include "Eq.h"

#include <QByteArray>
#include <QStringList>

#include <array>
#include <atomic>
#include <functional>
#include <vector>

namespace microdap {

enum class EqFitEnd { Done, Cancelled, Boost };

struct EqFitStats
{
    int iterations = 0;
    int trials = 0;
    int maxTrials = 0;
    int points = 0;
    long evals = 0;
    double rms = 0.0;
    double boost = 0.0;
};

struct EqFitOptions
{
    const std::atomic<bool> *cancel = nullptr;
    int budgetMs = 2500;
    int maxIters = 200;
    std::function<void(int)> onIteration;
};

struct EqFit
{
    static constexpr int points = 127;
    static constexpr int bands = 10;
    static constexpr qsizetype minPoints = 31;
    static constexpr qsizetype maxPoints = 1024;
    static const std::array<double, points> frequencies;

    static bool parse(const QByteArray &text, std::vector<double> &freqs, std::vector<double> &gains,
                      QStringList *log = nullptr);
    static void response(const EqProfile &profile, double rate, const double *freqs, int n, double *db);
    static EqFitEnd fit(const std::vector<double> &freqs, const std::vector<double> &gains, EqProfile &out,
                        const EqFitOptions &options, EqFitStats *stats = nullptr);
};

}
