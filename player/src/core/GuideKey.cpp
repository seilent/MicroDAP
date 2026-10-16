#include "GuideKey.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>
#include <ctime>

#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace microdap {

namespace {

constexpr int readBatch = 64;

QString envPath(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

}

GuideKey::GuideKey(QObject *parent)
    : QObject(parent)
    , m_devRoot(envPath("MICRODAP_DEV_INPUT", QStringLiteral("/dev/input")))
    , m_sysRoot(envPath("MICRODAP_SYS_INPUT", QStringLiteral("/sys/class/input")))
    , m_name(deviceName())
{
    m_retry.setSingleShot(true);
    m_retry.setInterval(retryMs);
    connect(&m_retry, &QTimer::timeout, this, &GuideKey::open);
    m_hold.setSingleShot(true);
    m_hold.setTimerType(Qt::PreciseTimer);
    connect(&m_hold, &QTimer::timeout, this, &GuideKey::onHoldTimer);
    m_pending.setSingleShot(true);
    m_pending.setTimerType(Qt::PreciseTimer);
    connect(&m_pending, &QTimer::timeout, this, &GuideKey::flushPending);
    m_grace.setSingleShot(true);
    m_grace.setTimerType(Qt::PreciseTimer);
    connect(&m_grace, &QTimer::timeout, this, &GuideKey::resolveTap);
    open();
}

GuideKey::~GuideKey()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

QString GuideKey::deviceName()
{
    return envPath("MICRODAP_GUIDE_NAME", QStringLiteral("InputPlumber Keyboard"));
}

qint64 GuideKey::nowMs()
{
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return qint64(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool GuideKey::isOpen() const
{
    return m_fd >= 0;
}

bool GuideKey::screenOff() const
{
    return m_screenOff;
}

void GuideKey::setScreenOff(bool off)
{
    m_screenOff = off;
}

bool GuideKey::take()
{
    if (m_pocket || !(m_down || m_tapDue) || m_holdFired)
        return false;
    const qint64 age = nowMs() - m_pressAt;
    if (age < 0 || age > chordMs)
        return false;
    m_chord = true;
    return true;
}

QString GuideKey::find() const
{
    const QStringList events = QDir(m_devRoot).entryList({QStringLiteral("event*")},
                                                         QDir::System | QDir::Files, QDir::Name);
    for (const QString &event : events) {
        QFile file(m_sysRoot + QLatin1Char('/') + event + QStringLiteral("/device/name"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8(file.readAll()).trimmed() == m_name)
            return m_devRoot + QLatin1Char('/') + event;
    }
    return QString();
}

void GuideKey::open()
{
    if (m_fd >= 0)
        return;
    const QString path = find();
    const int fd = path.isEmpty() ? -1 : ::open(QFile::encodeName(path).constData(),
                                                O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        if (!m_logged) {
            if (path.isEmpty())
                qInfo().noquote() << "microdap guide:" << m_name << "not found, retrying";
            else
                qWarning().noquote() << "microdap guide: cannot open" << path << std::strerror(errno);
            m_logged = true;
        }
        m_retry.start();
        return;
    }
    int clock = CLOCK_MONOTONIC;
    ::ioctl(fd, EVIOCSCLOCKID, &clock);
    m_fd = fd;
    m_logged = false;
    m_notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &GuideKey::drain);
    qInfo().noquote() << "microdap guide:" << m_name << "on" << path;
}

void GuideKey::closeNode()
{
    if (m_notifier) {
        m_notifier->setEnabled(false);
        m_notifier->deleteLater();
        m_notifier = nullptr;
    }
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
    m_hold.stop();
    m_down = false;
    m_holdFired = false;
    if (m_grace.isActive()) {
        m_grace.stop();
        resolveTap();
    }
    if (m_pending.isActive()) {
        m_pending.stop();
        flushPending();
    }
    qInfo().noquote() << "microdap guide:" << m_name << "closed";
    m_retry.start();
}

void GuideKey::drain()
{
    input_event events[readBatch];
    while (m_fd >= 0) {
        const ssize_t got = ::read(m_fd, events, sizeof(events));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            qWarning().noquote() << "microdap guide:" << m_name << "read failed" << std::strerror(errno);
            closeNode();
            return;
        }
        if (got == 0) {
            closeNode();
            return;
        }
        const qsizetype count = qsizetype(got) / qsizetype(sizeof(input_event));
        for (qsizetype i = 0; i < count; ++i) {
            const input_event &event = events[i];
            if (event.type != EV_KEY || event.code != KEY_F13)
                continue;
            const qint64 at = anchor(qint64(event.input_event_sec) * 1000 + event.input_event_usec / 1000);
            if (event.value == 1)
                onPress(at);
            else if (event.value == 0)
                onRelease(at);
        }
    }
}

qint64 GuideKey::anchor(qint64 eventMs) const
{
    const qint64 now = nowMs();
    if (eventMs > 0 && now - eventMs >= 0 && now - eventMs <= staleMs)
        return eventMs;
    return now;
}

void GuideKey::onPress(qint64 at)
{
    if (m_down)
        return;
    if (m_grace.isActive()) {
        m_grace.stop();
        resolveTap();
    }
    if (m_tapPending && at - m_releaseAt >= doubleMs)
        flushPending();
    m_pending.stop();
    if (!m_tapPending)
        m_pocket = m_screenOff;
    m_down = true;
    m_holdFired = false;
    m_chord = false;
    m_pressAt = at;
    const qint64 hold = m_pocket ? pocketHoldMs : holdMs;
    m_hold.start(int(qMax<qint64>(0, at + hold - nowMs())));
}

void GuideKey::onRelease(qint64 at)
{
    if (!m_down)
        return;
    m_down = false;
    m_hold.stop();
    if (m_holdFired) {
        m_holdFired = false;
        return;
    }
    const qint64 held = at - m_pressAt;
    if (held >= (m_pocket ? pocketHoldMs : holdMs)) {
        onHoldTimer();
        m_holdFired = false;
        return;
    }
    if (m_pocket) {
        if (m_tapPending) {
            m_tapPending = false;
            qInfo() << "microdap guide: double tap, next";
            emit buzz({buzzMs, buzzGapMs, buzzMs});
            emit next();
            return;
        }
        m_tapPending = true;
        m_releaseAt = at;
        m_pending.start(int(qMax<qint64>(0, at + doubleMs - nowMs())));
        return;
    }
    m_tapDue = true;
    if (held < chordMs)
        m_grace.start(chordGraceMs);
    else
        resolveTap();
}

void GuideKey::onHoldTimer()
{
    if (m_holdFired)
        return;
    m_holdFired = true;
    if (m_pocket) {
        flushPending();
        qInfo() << "microdap guide: hold, previous";
        emit buzz({buzzMs});
        emit previous();
        return;
    }
    if (m_chord) {
        qInfo() << "microdap guide: chord, hold dropped";
        return;
    }
    qInfo() << "microdap guide: hold, switch prompt";
    emit buzz({buzzMs});
    emit switchPrompt();
}

void GuideKey::resolveTap()
{
    if (!m_tapDue)
        return;
    m_tapDue = false;
    if (m_chord) {
        qInfo() << "microdap guide: chord, tap dropped";
        return;
    }
    qInfo() << "microdap guide: tap, play pause";
    emit playPause();
}

void GuideKey::flushPending()
{
    m_pending.stop();
    if (!m_tapPending)
        return;
    m_tapPending = false;
    qInfo() << "microdap guide: tap, play pause";
    emit playPause();
}

}
