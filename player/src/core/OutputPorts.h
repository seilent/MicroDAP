#pragma once

#include <QList>
#include <QString>
#include <QVariantMap>

#include <gst/gst.h>

#include <functional>

namespace microdap {

class ReservationPort
{
public:
    virtual ~ReservationPort() = default;

    virtual void acquire(const QString &reserveName, const QString &cardId, quint64 seq) = 0;
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
    virtual void refresh() = 0;
    virtual quint64 readsStarted() const = 0;
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
    virtual void sinkCard(const QString &name, std::function<void(const DacCard &)> done) = 0;
    virtual void sinkVolumeDb(const QString &name, std::function<void(bool ok, double db)> done) = 0;
    virtual void sinkMuted(const QString &name, std::function<void(bool ok, bool muted)> done) = 0;
    virtual void setSinkVolumeDb(const QString &name, double db, std::function<void(bool ok)> done = nullptr) = 0;
    virtual void unmuteSink(const QString &name) = 0;
    virtual void bluezInputs(std::function<void(const QList<quint32> &)> done) = 0;
    virtual void moveSinkInput(quint32 index, const QString &sink) = 0;
    virtual int defaultSinkRate() const = 0;
};

class DacVolumePort
{
public:
    enum class Route { Pulse, Alsa };

    virtual ~DacVolumePort() = default;

    virtual void setRoute(Route route, const QString &cardId, const QString &sink) = 0;
    virtual void setStoreAllowed(bool allowed) = 0;
    virtual void storeSync(const QString &cardId) = 0;
    virtual int ownedStep(const QString &cardId) const = 0;
    virtual void setOwnedStep(const QString &cardId, int step) = 0;
};

class PlayerPort
{
public:
    enum class OutputChain { None, Pw, Bp, Ex };
    enum class PlayState { Stopped, Playing, Paused };

    virtual ~PlayerPort() = default;

    virtual void buildPending(OutputChain chain) = 0;
    virtual void rebuildAt(qint64 positionMs, GstState state) = 0;
    virtual void resumeFrom() = 0;
    virtual void park(qint64 positionMs) = 0;
    virtual PlayState playState() const = 0;
    virtual qint64 positionMs() const = 0;
    virtual QVariantMap sourceFormat() const = 0;
    virtual QVariantMap sinkFormat() const = 0;
    virtual OutputChain builtChain() const = 0;
    virtual bool parked() const = 0;
    virtual bool dsd() const = 0;
};

class DacEqPort
{
public:
    virtual ~DacEqPort() = default;

    virtual void startRelease(std::function<void()> released) = 0;
    virtual void quitAndJoin(int ms) = 0;
};

class RestorePort
{
public:
    virtual ~RestorePort() = default;

    virtual bool levelSet() const = 0;
    virtual void restorePlaying(bool playing) = 0;
    virtual bool restorePending() const = 0;
};

}
