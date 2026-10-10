#pragma once

#include "AlsaVolume.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <cerrno>
#include <functional>

#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace casdap {

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

}
