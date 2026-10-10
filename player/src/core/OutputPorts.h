#pragma once

#include <QString>

namespace casdap {

class ReservationPort
{
public:
    virtual ~ReservationPort() = default;

    virtual void acquire(const QString &reserveName, const QString &cardId) = 0;
    virtual void grantRelease() = 0;
    virtual void release() = 0;
    virtual void markGone() = 0;
    virtual bool waitReleased(int ms) = 0;
    virtual void quitAndJoin(int ms) = 0;
};

}
