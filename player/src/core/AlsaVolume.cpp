#include "AlsaVolume.h"

#include <QDebug>
#include <QSocketNotifier>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#include <alsa/asoundlib.h>
#include <poll.h>

namespace casdap {

struct SndMixer::Handle
{
    snd_mixer_t *mixer = nullptr;
    snd_mixer_elem_t *elem = nullptr;
    MixerRange range;
    std::vector<pollfd> fds;
};

SndMixer::SndMixer() = default;

SndMixer::~SndMixer()
{
    close();
}

int SndMixer::open(const QString &cardId)
{
    close();
    snd_mixer_t *mixer = nullptr;
    int err = snd_mixer_open(&mixer, 0);
    if (err < 0)
        return err;
    const QByteArray name = "hw:CARD=" + cardId.toUtf8();
    err = snd_mixer_attach(mixer, name.constData());
    if (err >= 0)
        err = snd_mixer_selem_register(mixer, nullptr, nullptr);
    if (err >= 0)
        err = snd_mixer_load(mixer);
    if (err < 0) {
        snd_mixer_close(mixer);
        return err;
    }
    for (snd_mixer_elem_t *elem = snd_mixer_first_elem(mixer); elem; elem = snd_mixer_elem_next(elem)) {
        if (!snd_mixer_selem_is_active(elem) || !snd_mixer_selem_has_playback_volume(elem))
            continue;
        long min = 0;
        long max = 0;
        long minDb = 0;
        long maxDb = 0;
        if (snd_mixer_selem_get_playback_volume_range(elem, &min, &max) < 0 || min >= max)
            continue;
        if (snd_mixer_selem_get_playback_dB_range(elem, &minDb, &maxDb) < 0 || minDb >= maxDb)
            continue;
        m_handle = std::make_unique<Handle>();
        m_handle->mixer = mixer;
        m_handle->elem = elem;
        m_handle->range = MixerRange{min, max, minDb / 100.0, maxDb / 100.0};
        const int count = snd_mixer_poll_descriptors_count(mixer);
        if (count > 0) {
            m_handle->fds.resize(size_t(count));
            const int got = snd_mixer_poll_descriptors(mixer, m_handle->fds.data(), unsigned(count));
            m_handle->fds.resize(size_t(qMax(got, 0)));
        }
        return 0;
    }
    snd_mixer_close(mixer);
    return -ENOENT;
}

void SndMixer::close()
{
    if (!m_handle)
        return;
    snd_mixer_close(m_handle->mixer);
    m_handle.reset();
}

MixerRange SndMixer::range() const
{
    return m_handle ? m_handle->range : MixerRange{};
}

int SndMixer::get(long *value)
{
    if (!m_handle)
        return -ENODEV;
    return snd_mixer_selem_get_playback_volume(m_handle->elem, SND_MIXER_SCHN_FRONT_LEFT, value);
}

int SndMixer::set(long value)
{
    if (!m_handle)
        return -ENODEV;
    return snd_mixer_selem_set_playback_volume_all(m_handle->elem, value);
}

int SndMixer::dbOf(long value, double *db)
{
    if (!m_handle)
        return -ENODEV;
    long centi = 0;
    const int err = snd_mixer_selem_ask_playback_vol_dB(m_handle->elem, value, &centi);
    if (err < 0)
        return err;
    *db = centi / 100.0;
    return 0;
}

QList<int> SndMixer::pollFds()
{
    QList<int> fds;
    if (m_handle) {
        for (const pollfd &fd : m_handle->fds)
            fds.append(fd.fd);
    }
    return fds;
}

short SndMixer::revents()
{
    if (!m_handle || m_handle->fds.empty())
        return POLLNVAL;
    for (pollfd &fd : m_handle->fds)
        fd.revents = 0;
    if (::poll(m_handle->fds.data(), nfds_t(m_handle->fds.size()), 0) < 0)
        return POLLERR;
    unsigned short events = 0;
    if (snd_mixer_poll_descriptors_revents(m_handle->mixer, m_handle->fds.data(), unsigned(m_handle->fds.size()),
                                           &events) < 0)
        return POLLERR;
    return short(events);
}

int SndMixer::handleEvents()
{
    if (!m_handle)
        return -ENODEV;
    return snd_mixer_handle_events(m_handle->mixer);
}

AlsaVolume::AlsaVolume(std::unique_ptr<MixerBackend> backend, int pacingMs, QObject *parent)
    : QObject(parent)
    , m_backend(std::move(backend))
    , m_pacingMs(pacingMs)
    , m_sentMs(-pacingMs)
{
    m_clock.start();
    m_send.setSingleShot(true);
    connect(&m_send, &QTimer::timeout, this, &AlsaVolume::write);
    m_watch.setSingleShot(true);
    m_watch.setTimerType(Qt::PreciseTimer);
    connect(&m_watch, &QTimer::timeout, this, [this] {
        qInfo().noquote() << "casdap volume: watch ended on" << m_cardId << "step" << m_owned << "rewrites"
                          << m_rewrites;
    });
}

AlsaVolume::~AlsaVolume()
{
    closeMixer();
}

double AlsaVolume::percentOfDb(double db)
{
    return 100.0 * std::pow(10.0, db / 60.0);
}

double AlsaVolume::dbOfPercent(double percent)
{
    if (percent <= 0.0)
        return -std::numeric_limits<double>::infinity();
    return 60.0 * std::log10(percent / 100.0);
}

double AlsaVolume::dbOfStep(int step, const MixerRange &range)
{
    if (range.max <= range.min)
        return range.minDb;
    const long bounded = qBound(range.min, long(step), range.max);
    return range.minDb + double(bounded - range.min) * (range.maxDb - range.minDb) / double(range.max - range.min);
}

int AlsaVolume::stepFor(int step, int deltaPercent, const MixerRange &range)
{
    if (range.max <= range.min)
        return step;
    const long current = qBound(range.min, long(step), range.max);
    if (deltaPercent == 0)
        return int(current);
    const double percent = percentOfDb(dbOfStep(int(current), range)) + deltaPercent;
    const double perStep = (range.maxDb - range.minDb) / double(range.max - range.min);
    long target = range.min;
    if (percent > 0.0) {
        const double db = qMax(dbOfPercent(percent), range.minDb - perStep);
        target = range.min + long(qRound64((db - range.minDb) / perStep));
    }
    target = deltaPercent > 0 ? qMax(target, current + 1) : qMin(target, current - 1);
    return int(qBound(range.min, target, range.max));
}

bool AlsaVolume::open(const QString &cardId)
{
    if (m_open && cardId == m_cardId)
        return true;
    closeMixer();
    const int err = m_backend->open(cardId);
    if (err < 0) {
        qWarning().noquote() << "casdap volume: cannot open hw:CARD=" + cardId << std::strerror(-err);
        return false;
    }
    MixerRange range = m_backend->range();
    if (!usableRange(&range)) {
        qWarning().noquote() << "casdap volume: hw:CARD=" + cardId << "has no linear dB volume, not used";
        m_backend->close();
        return false;
    }
    const bool sameCard = !m_cardId.isEmpty() && cardId == m_cardId;
    m_open = true;
    m_cardId = cardId;
    m_range = range;
    long value = m_range.min;
    if (m_backend->get(&value) < 0)
        value = m_range.min;
    m_hw = value;
    if (!sameCard) {
        m_owned = clamped(int(value));
        m_pending = false;
    }
    const QList<int> fds = m_backend->pollFds();
    for (int fd : fds) {
        auto *notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(notifier, &QSocketNotifier::activated, this, &AlsaVolume::onActivated);
        m_notifiers.append(notifier);
    }
    qInfo().noquote() << "casdap volume: hw:CARD=" + cardId << "open, step" << value << "range"
                      << m_range.minDb << "to" << m_range.maxDb << "dB";
    if (m_pending)
        write();
    return m_open;
}

void AlsaVolume::close()
{
    closeMixer();
}

bool AlsaVolume::isOpen() const
{
    return m_open;
}

QString AlsaVolume::cardId() const
{
    return m_cardId;
}

int AlsaVolume::step() const
{
    return m_owned;
}

double AlsaVolume::stepDb() const
{
    return dbOfStep(m_owned, m_range);
}

double AlsaVolume::rangeDb() const
{
    return m_range.maxDb - m_range.minDb;
}

void AlsaVolume::setOwnedStep(int step)
{
    m_owned = clamped(step);
}

void AlsaVolume::setStep(int step)
{
    m_owned = clamped(step);
    if (!m_open) {
        m_pending = true;
        return;
    }
    if (m_send.isActive())
        return;
    const qint64 wait = m_pacingMs - (m_clock.elapsed() - m_sentMs);
    if (wait > 0)
        m_send.start(int(wait));
    else
        write();
}

void AlsaVolume::pressed(int deltaPercent)
{
    if (deltaPercent == 0 || m_range.max <= m_range.min)
        return;
    const int next = stepFor(m_owned, deltaPercent, m_range);
    const int percent = qRound(percentOfDb(dbOfStep(next, m_range)));
    qInfo() << "casdap volume:" << percent;
    emit levelChanged(percent);
    if (next != m_owned)
        setStep(next);
}

void AlsaVolume::watchReturn(int ms)
{
    if (!m_open) {
        qWarning().noquote() << "casdap volume: watch on a closed mixer for" << m_cardId;
        return;
    }
    if (!m_watch.isActive())
        m_rewrites = 0;
    m_watch.start(qMax(ms, m_watch.remainingTime()));
    m_send.stop();
    write();
}

void AlsaVolume::nudge()
{
    if (!m_open || m_hw != m_owned || m_range.max <= m_range.min)
        return;
    const long neighbor = m_owned < m_range.max ? m_owned + 1 : m_owned - 1;
    const int err = m_backend->set(neighbor);
    if (err == -ENODEV) {
        lose("write failed");
        return;
    }
    if (err < 0) {
        qWarning().noquote() << "casdap volume: write failed on" << m_cardId << std::strerror(-err);
        return;
    }
    m_hw = neighbor;
}

bool AlsaVolume::usableRange(MixerRange *range)
{
    if (range->max <= range->min)
        return false;
    if (qRound64(range->minDb * 100.0) <= SND_CTL_TLV_DB_GAIN_MUTE) {
        ++range->min;
        if (range->max <= range->min || m_backend->dbOf(range->min, &range->minDb) < 0)
            return false;
    }
    if (range->maxDb <= range->minDb)
        return false;
    const long middle = range->min + (range->max - range->min) / 2;
    double db = 0.0;
    if (m_backend->dbOf(middle, &db) < 0)
        return false;
    return qAbs(db - dbOfStep(int(middle), *range)) <= linearToleranceDb;
}

int AlsaVolume::clamped(int step) const
{
    if (m_range.max <= m_range.min)
        return step;
    return int(qBound(m_range.min, long(step), m_range.max));
}

void AlsaVolume::onActivated()
{
    if (!m_open)
        return;
    const short events = m_backend->revents();
    if (events & (POLLERR | POLLHUP | POLLNVAL)) {
        lose("poll error");
        return;
    }
    if (!(events & POLLIN))
        return;
    if (m_backend->handleEvents() < 0) {
        lose("events failed");
        return;
    }
    long value = 0;
    const int err = m_backend->get(&value);
    if (err == -ENODEV) {
        lose("read failed");
        return;
    }
    if (err < 0 || value == m_hw)
        return;
    m_hw = value;
    emit foreignStep(int(value));
    if (!m_open || !m_watch.isActive())
        return;
    if (m_rewrites >= maxRewrites) {
        m_watch.stop();
        qWarning().noquote() << "casdap volume: watch gave up on" << m_cardId << "after" << m_rewrites << "rewrites";
        return;
    }
    ++m_rewrites;
    m_send.stop();
    write();
}

void AlsaVolume::write()
{
    if (!m_open)
        return;
    m_sentMs = m_clock.elapsed();
    m_pending = false;
    const int err = m_backend->set(m_owned);
    if (err == -ENODEV) {
        lose("write failed");
        return;
    }
    if (err < 0) {
        qWarning().noquote() << "casdap volume: write failed on" << m_cardId << std::strerror(-err);
        return;
    }
    m_hw = m_owned;
}

void AlsaVolume::lose(const char *reason)
{
    qWarning().noquote() << "casdap volume: hw:CARD=" + m_cardId << reason << "closing";
    closeMixer();
    QMetaObject::invokeMethod(this, &AlsaVolume::disconnected, Qt::QueuedConnection);
}

void AlsaVolume::closeMixer()
{
    for (QSocketNotifier *notifier : std::as_const(m_notifiers)) {
        notifier->setEnabled(false);
        notifier->deleteLater();
    }
    m_notifiers.clear();
    if (m_send.isActive()) {
        m_send.stop();
        m_pending = true;
    }
    m_watch.stop();
    if (m_open)
        m_backend->close();
    m_open = false;
}

}
