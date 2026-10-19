#pragma once

#include <QList>

namespace microdap {

class Favorites
{
public:
    static QList<qsizetype> order(const QList<qint64> &seqs);
};

}
