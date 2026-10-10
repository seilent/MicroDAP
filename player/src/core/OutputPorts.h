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

class HeldOutputPort
{
public:
    enum class HeldState { None, Held, Returning };

    virtual ~HeldOutputPort() = default;

    virtual void setHeld(HeldState state) = 0;
    virtual void watchCard(int index, const QString &id) = 0;
    virtual void unwatchCard() = 0;
};

}
