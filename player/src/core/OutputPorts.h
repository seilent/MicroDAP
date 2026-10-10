#pragma once

#include <QList>
#include <QString>

#include <functional>

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

struct DacCard
{
    quint32 cardIndex = 0xffffffffu;
    int alsaCard = -1;
    QString reserveName;

    bool isValid() const
    {
        return alsaCard >= 0 && !reserveName.isEmpty();
    }
};

class DacSinkPort
{
public:
    virtual ~DacSinkPort() = default;

    virtual void cardInfo(quint32 sinkIndex, std::function<void(const DacCard &)> done) = 0;
    virtual void setDefaultSink(const QString &name) = 0;
    virtual void sinkVolumeDb(const QString &name, std::function<void(bool ok, double db)> done) = 0;
    virtual void sinkMuted(const QString &name, std::function<void(bool ok, bool muted)> done) = 0;
    virtual void setSinkVolumeDb(const QString &name, double db) = 0;
    virtual void unmuteSink(const QString &name) = 0;
    virtual void bluezInputs(std::function<void(const QList<quint32> &)> done) = 0;
    virtual void moveSinkInput(quint32 index, const QString &sink) = 0;
};

}
