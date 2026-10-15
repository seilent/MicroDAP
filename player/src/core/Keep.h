#pragma once

#include <QString>

namespace microdap {

class Keep
{
public:
    static QString markerPath();
    static QString bootIdPath();
    static QString bootId();
    static QString marker();
    static bool write();
    static bool clear();
};

}
