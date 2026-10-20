#include "eq.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

fa::EqBand fband(fa::BandType t, double f, double g, double q)
{
    fa::EqBand b;
    b.type = t;
    b.freq = f;
    b.gain = g;
    b.q = q;
    return b;
}

std::vector<fa::EqProfile> fit_targets()
{
    using T = fa::BandType;
    std::vector<std::vector<fa::EqBand>> sets = {
        {fband(T::LowShelf, 105, 6, 0.7), fband(T::Peak, 2400, -3.2, 1.4), fband(T::HighShelf, 8000, 2, 0.7)},
        {fband(T::Peak, 60, 4, 0.8), fband(T::Peak, 300, -2, 1), fband(T::Peak, 1000, 1.5, 2), fband(T::Peak, 3000, -4, 3),
         fband(T::Peak, 6000, 3, 2)},
        {fband(T::LowShelf, 80, -4, 0.7), fband(T::HighShelf, 5000, -6, 0.7), fband(T::Peak, 1500, 2, 0.7)},
        {fband(T::LowShelf, 50, 2, 0.7), fband(T::Peak, 40, 5, 1.5), fband(T::Peak, 120, -3, 1), fband(T::Peak, 400, 1, 2),
         fband(T::Peak, 700, 2, 1.2), fband(T::Peak, 1300, -1, 3), fband(T::Peak, 2000, -2.5, 2.5), fband(T::Peak, 4500, 4, 4),
         fband(T::Peak, 7000, -3, 3), fband(T::HighShelf, 9000, 1.5, 0.7)},
        {fband(T::LowShelf, 200, 3, 0.5), fband(T::Peak, 3500, 5, 1)},
        {fband(T::Peak, 30, -6, 0.7), fband(T::Peak, 250, 3, 0.9), fband(T::Peak, 500, -1, 2), fband(T::Peak, 900, -1.5, 1.4),
         fband(T::Peak, 1700, 1, 3), fband(T::Peak, 2700, 3.5, 1.8), fband(T::Peak, 4000, -2, 4), fband(T::Peak, 5500, -5, 2.5),
         fband(T::Peak, 6500, 1, 2), fband(T::HighShelf, 7500, -3, 0.7)},
    };
    std::vector<fa::EqProfile> out;
    for (const auto& set : sets) {
        fa::EqProfile p;
        for (size_t i = 0; i < set.size(); i++)
            p.bands[i] = set[i];
        out.push_back(p);
    }
    return out;
}

const char* code(fa::BandType t)
{
    return t == fa::BandType::LowShelf ? "LSC" : t == fa::BandType::HighShelf ? "HSC" : "PK";
}

}

int main()
{
    std::vector<double> ff(fa::kGraphicFreqs, fa::kGraphicFreqs + fa::kGraphicPoints);
    printf("flatdap 17558f5fbfbe049f66cdf31c84717fbdf9df9aa9\n");
    printf("command g++ -O2 -std=c++17 -I$FLATDAP/player/audio/src gen.cpp $FLATDAP/player/audio/src/eq.cpp "
           "$FLATDAP/player/audio/src/eqfit.cpp -o gen && ./gen > reference.txt\n");
    int n = 0;
    for (const fa::EqProfile& p : fit_targets()) {
        n++;
        std::vector<double> db(ff.size());
        fa::eq_response(p, 96000, ff.data(), (int)ff.size(), db.data());
        std::string text = "GraphicEQ: ";
        char b[48];
        for (size_t i = 0; i < ff.size(); i++) {
            snprintf(b, sizeof b, "%s%.2f %.1f", i ? "; " : "", ff[i], std::round(db[i] * 10) / 10);
            text += b;
        }
        text += "\n";
        std::string name = "target-" + std::to_string(n) + " GraphicEQ.txt";
        FILE* f = fopen(name.c_str(), "w");
        fputs(text.c_str(), f);
        fclose(f);
        std::vector<double> fq, gn;
        fa::parse_graphiceq(text, fq, gn, nullptr);
        fa::EqProfile o;
        fa::EqFitStats st;
        fa::EqFitOptions opt;
        opt.budget_ms = 600000;
        opt.max_iters = 200;
        fa::EqFitEnd end = fa::fit_graphiceq(fq, gn, o, opt, &st);
        printf("target %d end %d iterations %d rms %.6f\n", n, (int)end, st.iterations, st.rms);
        printf("Preamp: %.10g dB\n", o.preamp);
        for (int i = 0; i < o.count; i++)
            printf("Filter %d: ON %s Fc %.10g Hz Gain %.10g dB Q %.10g\n", i + 1, code(o.bands[i].type), o.bands[i].freq,
                   o.bands[i].gain, o.bands[i].q);
    }
    return 0;
}
