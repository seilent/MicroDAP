#include "Favorites.h"

#include <algorithm>
#include <numeric>

namespace microdap {

QList<qsizetype> Favorites::order(const QList<qint64> &seqs)
{
    QList<qsizetype> out(seqs.size());
    std::iota(out.begin(), out.end(), 0);
    std::stable_sort(out.begin(), out.end(), [&seqs](qsizetype a, qsizetype b) {
        const qint64 x = seqs.at(a);
        const qint64 y = seqs.at(b);
        if ((x >= 0) != (y >= 0))
            return x >= 0;
        return x >= 0 && x > y;
    });
    return out;
}

}
