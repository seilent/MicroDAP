#pragma once

#include "AlsaVolume.h"
#include "OutputPorts.h"
#include "Player.h"

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QThread>
#include <QTimer>

#include <cerrno>
#include <functional>
#include <optional>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace microdap {

class FakeMixer : public MixerBackend
{
public:
    explicit FakeMixer(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    ~FakeMixer() override
    {
        closePipe();
    }

    int open(const QString &cardId) override
    {
        opens.append(cardId);
        if (openResult < 0)
            return openResult;
        if (::pipe2(m_pipe, O_NONBLOCK | O_CLOEXEC) < 0)
            return -errno;
        isOpen = true;
        return 0;
    }

    void close() override
    {
        if (onClose)
            onClose();
        ++closes;
        isOpen = false;
        closePipe();
    }

    MixerRange range() const override
    {
        return mixerRange;
    }

    int get(long *out) override
    {
        if (getResult < 0)
            return getResult;
        *out = value;
        return 0;
    }

    int set(long step) override
    {
        writes.append(step);
        if (m_log)
            m_log->append(QStringLiteral("PCM %1").arg(step));
        if (setResult < 0)
            return setResult;
        value = step;
        fire(POLLIN);
        return 0;
    }

    int dbOf(long step, double *db) override
    {
        if (dbCurve) {
            *db = dbCurve(step);
            return 0;
        }
        *db = mixerRange.minDb + double(step - mixerRange.min) * (mixerRange.maxDb - mixerRange.minDb)
            / double(mixerRange.max - mixerRange.min);
        return 0;
    }

    QList<int> pollFds() override
    {
        return m_pipe[0] >= 0 ? QList<int>{m_pipe[0]} : QList<int>{};
    }

    short revents() override
    {
        drain();
        const short out = m_events;
        m_events = 0;
        return out;
    }

    int handleEvents() override
    {
        drain();
        return handleResult;
    }

    void fire(short events)
    {
        if (m_pipe[1] < 0)
            return;
        m_events = short(m_events | events);
        const char byte = 1;
        if (::write(m_pipe[1], &byte, 1) < 0)
            return;
    }

    void foreign(long step)
    {
        value = step;
        fire(POLLIN);
    }

    MixerRange mixerRange{0, 120, -60.0, 0.0};
    QStringList opens;
    QList<long> writes;
    std::function<void()> onClose;
    std::function<double(long)> dbCurve;
    long value = 80;
    int openResult = 0;
    int getResult = 0;
    int setResult = 0;
    int handleResult = 0;
    int closes = 0;
    bool isOpen = false;

private:
    void drain()
    {
        char buffer[64];
        while (m_pipe[0] >= 0 && ::read(m_pipe[0], buffer, sizeof(buffer)) > 0) {
        }
    }

    void closePipe()
    {
        for (int &fd : m_pipe) {
            if (fd >= 0)
                ::close(fd);
            fd = -1;
        }
    }

    QStringList *m_log;
    int m_pipe[2] = {-1, -1};
    short m_events = 0;
};

class FakeDacSink : public DacSinkPort
{
public:
    explicit FakeDacSink(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void cardInfo(quint32, std::function<void(const DacCard &)> done) override
    {
        const DacCard out = card;
        QTimer::singleShot(0, &m_context, [done, out] { done(out); });
    }

    void sinkCard(const QString &name, std::function<void(const DacCard &)> done) override
    {
        const DacCard out = sinkCards.isEmpty() ? card : sinkCards.value(name);
        QTimer::singleShot(0, &m_context, [done, out] { done(out); });
    }

    void setDefaultSink(const QString &name) override
    {
        record(QStringLiteral("default %1").arg(name));
    }

    void sinkVolumeDb(const QString &, std::function<void(bool ok, double db)> done) override
    {
        const double out = volumeDb;
        QTimer::singleShot(0, &m_context, [done, out] { done(true, out); });
    }

    void sinkMuted(const QString &, std::function<void(bool ok, bool muted)> done) override
    {
        const bool out = muted;
        QTimer::singleShot(0, &m_context, [done, out] { done(true, out); });
    }

    void setSinkVolumeDb(const QString &name, double db, std::function<void(bool ok)> done = nullptr) override
    {
        record(QStringLiteral("sink volume %1 %2").arg(name).arg(db));
        QTimer::singleShot(0, &m_context, [this, name, db, done] {
            QTimer::singleShot(0, &m_context, [this, name, db, done] {
                if (setOk)
                    volumeDb = db;
                record(QStringLiteral("sink volume done %1").arg(name));
                if (done)
                    done(setOk);
            });
        });
    }

    void unmuteSink(const QString &name) override
    {
        muted = false;
        record(QStringLiteral("unmute %1").arg(name));
    }

    void bluezInputs(std::function<void(const QList<quint32> &)> done) override
    {
        const QList<quint32> out = inputs;
        QTimer::singleShot(0, &m_context, [done, out] { done(out); });
    }

    void moveSinkInput(quint32 index, const QString &sink) override
    {
        record(QStringLiteral("move %1 %2").arg(index).arg(sink));
    }

    QStringList calls;
    DacCard card;
    QHash<QString, DacCard> sinkCards;
    QList<quint32> inputs;
    double volumeDb = 0.0;
    bool muted = false;
    bool setOk = true;

private:
    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(call);
    }

    QStringList *m_log;
    QObject m_context;
};

inline QString chainName(PlayerPort::OutputChain chain)
{
    switch (chain) {
    case PlayerPort::OutputChain::Pw:
        return QStringLiteral("pw");
    case PlayerPort::OutputChain::Bp:
        return QStringLiteral("bp");
    case PlayerPort::OutputChain::Ex:
        return QStringLiteral("ex");
    case PlayerPort::OutputChain::None:
        break;
    }
    return QStringLiteral("none");
}

class FakePlayer : public PlayerPort
{
public:
    explicit FakePlayer(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void buildPending(OutputChain chain) override
    {
        if (!pending) {
            record(QStringLiteral("build %1 nothing pending").arg(chainName(chain)));
            return;
        }
        const Player::PendingBuild build = *std::exchange(pending, std::nullopt);
        ++buildPendingCalls;
        if (emptyQueue) {
            stop();
            return;
        }
        built(build, chain);
    }

    void rebuildAt(qint64 positionMs, GstState state) override
    {
        record(QStringLiteral("rebuild %1 %2").arg(positionMs).arg(stateName(state)));
        request(entry, state, positionMs);
    }

    void resumeFrom() override
    {
        if (status != PlayState::Paused)
            return;
        const OutputChain wanted = policy ? policy->chainFor(GST_STATE_PLAYING) : OutputChain::Pw;
        if (!isParked && !pending && chain == wanted) {
            record(QStringLiteral("resume"));
            setStatus(PlayState::Playing);
            return;
        }
        request(entry, GST_STATE_PLAYING, pending ? pending->positionMs : position);
    }

    void park(qint64 positionMs) override
    {
        if (status == PlayState::Stopped)
            return;
        record(QStringLiteral("park %1").arg(positionMs));
        pending.reset();
        chain = OutputChain::None;
        isParked = true;
        position = positionMs;
        setStatus(PlayState::Paused);
    }

    PlayState playState() const override
    {
        return status;
    }

    qint64 positionMs() const override
    {
        return position;
    }

    QVariantMap sourceFormat() const override
    {
        return source;
    }

    QVariantMap sinkFormat() const override
    {
        return sink;
    }

    OutputChain builtChain() const override
    {
        return chain;
    }

    bool parked() const override
    {
        return isParked;
    }

    void play(qint64 id)
    {
        request(id, GST_STATE_PLAYING, 0);
    }

    void playPaused(qint64 id, qint64 positionMs = 0)
    {
        request(id, GST_STATE_PAUSED, positionMs);
    }

    void next()
    {
        request(entry + 1, GST_STATE_PLAYING, 0);
    }

    void resume()
    {
        if (status == PlayState::Paused)
            resumeFrom();
    }

    void pause()
    {
        if (pending) {
            park(pending->positionMs);
            return;
        }
        if (status != PlayState::Playing)
            return;
        record(QStringLiteral("pause"));
        setStatus(PlayState::Paused);
    }

    void stop()
    {
        record(QStringLiteral("stop"));
        pending.reset();
        chain = OutputChain::None;
        isParked = false;
        position = 0;
        setStatus(PlayState::Stopped);
    }

    void seek(qint64 ms)
    {
        if (pending)
            pending->positionMs = ms;
        position = ms;
    }

    static QString stateName(GstState state)
    {
        return state == GST_STATE_PLAYING ? QStringLiteral("playing") : QStringLiteral("paused");
    }

    OutputPolicy *policy = nullptr;
    std::function<void()> onStatus;
    std::optional<Player::PendingBuild> pending;
    QVariantMap source;
    QVariantMap sink;
    PlayState status = PlayState::Stopped;
    OutputChain chain = OutputChain::None;
    qint64 position = 0;
    qint64 entry = 0;
    int buildPendingCalls = 0;
    bool isParked = false;
    bool emptyQueue = false;

private:
    void request(qint64 id, GstState target, qint64 positionMs)
    {
        Player::PendingBuild build;
        build.entry.id = id;
        build.target = target;
        build.positionMs = positionMs;
        if (!policy) {
            pending.reset();
            built(build, OutputChain::Pw);
            return;
        }
        const Admit admit = policy->admit(build);
        if (admit.now) {
            pending.reset();
            built(build, admit.chain);
            return;
        }
        pending = build;
        chain = OutputChain::None;
        isParked = false;
        entry = id;
        position = positionMs;
        record(QStringLiteral("hold %1 %2 %3").arg(id).arg(stateName(target)).arg(positionMs));
        setStatus(target == GST_STATE_PLAYING ? PlayState::Playing : PlayState::Paused);
    }

    void built(const Player::PendingBuild &build, OutputChain next)
    {
        chain = next;
        isParked = false;
        entry = build.entry.id;
        position = build.positionMs;
        record(QStringLiteral("built %1 %2 %3 %4")
                   .arg(chainName(next), stateName(build.target))
                   .arg(build.entry.id)
                   .arg(build.positionMs));
        setStatus(build.target == GST_STATE_PLAYING ? PlayState::Playing : PlayState::Paused);
    }

    void setStatus(PlayState next)
    {
        if (status == next)
            return;
        status = next;
        if (onStatus)
            onStatus();
    }

    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(QStringLiteral("player ") + call);
    }

    QStringList *m_log;

public:
    QStringList calls;
};

class FakeReservation : public ReservationPort
{
public:
    explicit FakeReservation(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void acquire(const QString &reserveName, const QString &cardId, quint64 seq) override
    {
        lastSeq = seq;
        record(QStringLiteral("acquire %1 %2").arg(reserveName, cardId));
        if (onAcquire)
            onAcquire(seq);
    }

    void grantRelease() override
    {
        record(QStringLiteral("grant"));
        if (onGrant)
            onGrant();
    }

    void release() override
    {
        record(QStringLiteral("release"));
        if (onRelease)
            onRelease();
    }

    void markGone() override
    {
        record(QStringLiteral("mark gone"));
    }

    bool waitReleased(int ms) override
    {
        record(QStringLiteral("wait released %1").arg(ms));
        if (releaseCompletes)
            return true;
        QThread::msleep(ulong(ms));
        return false;
    }

    void quitAndJoin(int ms) override
    {
        record(QStringLiteral("quit %1").arg(ms));
    }

    QStringList calls;
    std::function<void(quint64)> onAcquire;
    std::function<void()> onGrant;
    std::function<void()> onRelease;
    quint64 lastSeq = 0;
    bool releaseCompletes = true;

private:
    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(QStringLiteral("reserve ") + call);
    }

    QStringList *m_log;
};

class FakeHeldOutput : public HeldOutputPort
{
public:
    explicit FakeHeldOutput(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void setHeld(HeldState state) override
    {
        if (state == held)
            return;
        held = state;
        record(QStringLiteral("held %1").arg(state == HeldState::None ? QStringLiteral("none")
                                              : state == HeldState::Held ? QStringLiteral("held")
                                                                         : QStringLiteral("returning")));
    }

    void watchCard(int index, const QString &id) override
    {
        watched = index;
        record(QStringLiteral("watch %1 %2").arg(index).arg(id));
    }

    void unwatchCard() override
    {
        if (watched < 0)
            return;
        watched = -1;
        record(QStringLiteral("unwatch"));
    }

    void refresh() override
    {
        ++refreshes;
        const quint64 read = ++started;
        record(QStringLiteral("refresh"));
        if (onRefresh)
            QTimer::singleShot(0, &m_context, [this, read] { onRefresh(read); });
    }

    quint64 readsStarted() const override
    {
        return started;
    }

    QStringList calls;
    std::function<void(quint64)> onRefresh;
    quint64 started = 0;
    HeldState held = HeldState::None;
    int watched = -1;
    int refreshes = 0;

private:
    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(QStringLiteral("output ") + call);
    }

    QStringList *m_log;
    QObject m_context;
};

class FakeDacEqPort : public DacEqPort
{
public:
    enum class Answer { Now, Delayed, Never };

    explicit FakeDacEqPort(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void startRelease(std::function<void()> released) override
    {
        record(QStringLiteral("release"));
        switch (answer) {
        case Answer::Now:
            released();
            return;
        case Answer::Delayed:
            QTimer::singleShot(delayMs, &m_context, released);
            return;
        case Answer::Never:
            pending.append(released);
            return;
        }
    }

    void quitAndJoin(int ms) override
    {
        record(QStringLiteral("quit %1").arg(ms));
    }

    QStringList calls;
    QList<std::function<void()>> pending;
    Answer answer = Answer::Now;
    int delayMs = 200;

private:
    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(QStringLiteral("daceq ") + call);
    }

    QStringList *m_log;
    QObject m_context;
};

class FakeDacVolume : public DacVolumePort
{
public:
    explicit FakeDacVolume(QStringList *log = nullptr)
        : m_log(log)
    {
    }

    void setRoute(Route next, const QString &cardId, const QString &sink) override
    {
        route = next;
        record(next == Route::Alsa ? QStringLiteral("route alsa %1 %2").arg(cardId, sink) : QStringLiteral("route pulse"));
    }

    void setStoreAllowed(bool allowed) override
    {
        storeAllowed = allowed;
        record(QStringLiteral("store allowed %1").arg(allowed ? 1 : 0));
    }

    void storeSync(const QString &cardId) override
    {
        record(QStringLiteral("store sync %1").arg(cardId));
    }

    int ownedStep(const QString &cardId) const override
    {
        return owned.value(cardId, -1);
    }

    void setOwnedStep(const QString &cardId, int step) override
    {
        owned.insert(cardId, step);
        record(QStringLiteral("owned %1 %2").arg(cardId).arg(step));
    }

    QStringList calls;
    QHash<QString, int> owned;
    Route route = Route::Pulse;
    bool storeAllowed = false;

private:
    void record(const QString &call)
    {
        calls.append(call);
        if (m_log)
            m_log->append(QStringLiteral("volume ") + call);
    }

    QStringList *m_log;
};

}
