#include "LinkServer.h"

#include "BluezModel.h"
#include "Favorites.h"
#include "HubModels.h"
#include "Library.h"
#include "Cue.h"
#include "Lyrics.h"
#include "Player.h"
#include "Queue.h"
#include "Settings.h"
#include "StatusModel.h"
#include "Version.h"
#include "VolumeKeys.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QSet>
#include <QSocketNotifier>
#include <QThreadPool>

#include <algorithm>
#include <time.h>
#include <unistd.h>

#undef slots

namespace microdap {

namespace {

namespace proto = fd::proto;

constexpr uint16_t screenW = 640;
constexpr uint16_t screenH = 960;
constexpr uint16_t backdropSide = 960;
constexpr int linkQueueArtists = 8;
constexpr size_t linkNameMax = 255;
constexpr qsizetype linkJobMax = 4;
constexpr size_t linkWaitMax = 16;

qint64 clockMs(clockid_t id)
{
    timespec t{};
    clock_gettime(id, &t);
    return qint64(t.tv_sec) * 1000 + t.tv_nsec / 1000000;
}

std::string utf8(const QString &s)
{
    return s.toStdString();
}

}

bool LinkSig::same(const LinkSig &o) const
{
    return track == o.track && title == o.title && source == o.source && output == o.output && repeat == o.repeat
           && volume == o.volume && cap == o.cap && step == o.step && battery == o.battery && favorite == o.favorite
           && playing == o.playing && held == o.held && shuffle == o.shuffle && charging == o.charging
           && queueRev == o.queueRev && slot == o.slot && slotCount == o.slotCount && lyrics == o.lyrics
           && lyricsCrc == o.lyricsCrc;
}

LinkTime LinkServer::now()
{
    LinkTime t;
    t.mono = clockMs(CLOCK_MONOTONIC);
    t.boot = clockMs(CLOCK_BOOTTIME);
    return t;
}

quint64 LinkServer::features()
{
    return 1ull << proto::feature::settings | 1ull << proto::feature::library | 1ull << proto::feature::queue
           | 1ull << proto::feature::art | 1ull << proto::feature::lyrics | 1ull << proto::feature::lyricsGet
           | 1ull << proto::feature::stateClock;
}

quint32 LinkServer::randomRev()
{
    return QRandomGenerator::global()->generate();
}

QString LinkServer::address(const QString &device)
{
    const QString leaf = device.section(QLatin1Char('/'), -1);
    if (!leaf.startsWith(QLatin1StringView("dev_")))
        return device;
    return leaf.mid(4).replace(QLatin1Char('_'), QLatin1Char(':')).toUpper();
}

LinkServer::LinkServer(Player *player, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_settings(settings)
    , m_clock(&LinkServer::now)
    , m_libRev(randomRev())
    , m_snapRev(randomRev())
    , m_schemaRev(randomRev())
    , m_settingsRev(randomRev())
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &LinkServer::poll);
    m_windowTimer.setSingleShot(true);
    connect(&m_windowTimer, &QTimer::timeout, this, [this] { closeWindow(true); });
    m_pool.setMaxThreadCount(1);
    m_soon.setSingleShot(true);
    m_soon.setInterval(0);
    connect(&m_soon, &QTimer::timeout, this, &LinkServer::poll);
    auto soon = [this] {
        if (!m_peers.empty())
            m_soon.start();
    };
    connect(m_player, &Player::statusChanged, this, soon);
    connect(m_player, &Player::currentChanged, this, soon);
    connect(m_player, &Player::durationChanged, this, soon);
    connect(m_player, &Player::positionChanged, this, soon);
    Queue *queue = m_player->queue();
    connect(queue, &Queue::revChanged, this, soon);
    connect(queue, &Queue::shuffleChanged, this, soon);
    connect(queue, &Queue::repeatChanged, this, soon);
    connect(m_settings, &Settings::maxVolumeChanged, this, soon);
    connect(m_settings, &Settings::volumeStepChanged, this, soon);
    m_companion = m_settings->companion();
    connect(m_settings, &Settings::companionChanged, this, &LinkServer::companionChanged);
    auto stale = [this] {
        m_settingsFresh = false;
        if (!m_peers.empty())
            m_soon.start();
    };
    connect(m_settings, &Settings::valueChanged, this, stale);
    connect(m_settings, &Settings::outputSlotChanged, this, stale);
    connect(m_settings, &Settings::availableChanged, this, stale);
}

LinkServer::~LinkServer()
{
    for (auto &p : m_peers) {
        p->s->on_close(nullptr);
        delete p->read;
        delete p->write;
        p->read = nullptr;
        p->write = nullptr;
    }
    m_peers.clear();
}

void LinkServer::setBluez(BluezModel *bluez)
{
    m_bluez = bluez;
    if (!bluez)
        return;
    auto soon = [this] {
        if (!m_peers.empty())
            m_soon.start();
    };
    connect(bluez, &BluezModel::transportActiveChanged, this, soon);
    connect(bluez, &BluezModel::titleChanged, this, soon);
    connect(bluez, &BluezModel::statusChanged, this, soon);
    connect(bluez, &BluezModel::positionMsChanged, this, soon);
    auto window = [this] {
        const bool open = m_bluez && m_bluez->pairing() && m_bluez->pairingMode() == QLatin1StringView("companion");
        if (open && m_pairWindow) {
            m_windowSeen = true;
            m_windowTimer.stop();
        }
        else if (!open && m_windowSeen)
            closeWindow(false);
    };
    connect(bluez, &BluezModel::pairingChanged, this, window);
    connect(bluez, &BluezModel::pairingModeChanged, this, window);
    connect(bluez, &BluezModel::companionPaired, this, &LinkServer::bind);
    connect(bluez, &BluezModel::confirmChanged, this, [this] {
        if (m_bluez && m_bluez->confirmPending())
            m_confirmName = m_bluez->confirmName();
    });
}

void LinkServer::setStatus(StatusModel *status)
{
    m_status = status;
    if (!status)
        return;
    auto soon = [this] {
        if (!m_peers.empty())
            m_soon.start();
    };
    connect(status, &StatusModel::batteryChanged, this, soon);
    connect(status, &StatusModel::chargingChanged, this, soon);
}

void LinkServer::setVolume(SinkVolume *volume)
{
    m_volume = volume;
    if (!volume)
        return;
    m_level = volume->level();
    connect(volume, &SinkVolume::levelChanged, this, [this](int percent) {
        m_level = percent;
        if (!m_peers.empty())
            m_soon.start();
    });
}

void LinkServer::setFavorites(Favorites *favorites)
{
    m_favorites = favorites;
    if (!favorites)
        return;
    connect(favorites, &Favorites::changed, this, [this] { bumpLib(false); });
}

void LinkServer::setSources(const LinkSources &sources)
{
    m_sources = sources;
    if (!m_favorites && sources.favorites)
        setFavorites(sources.favorites);
    if (Library *lib = sources.library) {
        connect(lib, &Library::tracksChanged, this, [this] { bumpLib(true); });
        connect(lib, &Library::playlistsChanged, this, [this] { bumpLib(false); });
    }
    if (FolderModel *folders = sources.folders)
        connect(folders, &FolderModel::rootsChanged, this, [this] { bumpLib(true); });
}

void LinkServer::setLyrics(Lyrics *lyrics)
{
    m_lyrics = lyrics;
    if (!lyrics)
        return;
    connect(lyrics, &Lyrics::changed, this, [this] {
        if (!m_peers.empty())
            m_soon.start();
    });
}

quint32 LinkServer::lyricsCrc(const LyricsText &text)
{
    const bool synced = text.timed();
    const char kind = synced ? 0 : 1;
    quint32 crc = linkCrc32(&kind, 1);
    auto line = [&crc](const QString &t) {
        const QByteArray bytes = Lyrics::cutLine(t);
        crc = linkCrc32(bytes, crc);
        crc = linkCrc32("\n", 1, crc);
    };
    if (synced) {
        for (const LyricLine &l : text.lines) {
            const quint32 ms = quint32(l.ms);
            const char le[4] = {char(ms & 0xff), char((ms >> 8) & 0xff), char((ms >> 16) & 0xff), char((ms >> 24) & 0xff)};
            crc = linkCrc32(le, 4, crc);
            line(l.text);
        }
    } else {
        for (const QString &t : text.plain)
            line(t);
    }
    return crc;
}

void LinkServer::bumpLib(bool snap)
{
    ++m_libRev;
    if (snap)
        ++m_snapRev;
    if (!m_peers.empty())
        m_soon.start();
}

void LinkServer::setClock(Clock clock)
{
    m_clock = std::move(clock);
}

fd::LinkHello LinkServer::hello() const
{
    fd::LinkHello h;
    h.board = "pm2";
    h.version = utf8(version());
    h.screen_w = screenW;
    h.screen_h = screenH;
    h.backdrop_w = backdropSide;
    h.backdrop_h = backdropSide;
    h.features = features();
    h.lib_rev = m_libRev;
    h.settings_rev = m_settingsRev;
    h.snap_rev = m_snapRev;
    return h;
}

bool LinkServer::connected() const
{
    return m_connected;
}

bool LinkServer::pairWindow() const
{
    return m_pairWindow;
}

int LinkServer::sessions() const
{
    int n = 0;
    for (const auto &p : m_peers)
        n += p->s->closed() ? 0 : 1;
    return n;
}

quint32 LinkServer::libRev() const
{
    return m_libRev;
}

quint32 LinkServer::snapRev() const
{
    return m_snapRev;
}

void LinkServer::setPairWindow(bool on)
{
    if (on == m_pairWindow)
        return;
    m_pairWindow = on;
    emit pairWindowChanged();
}

void LinkServer::pairPhoneApp()
{
    if (!m_settings->companion() || !m_bluez)
        return;
    m_windowEnd.setRemainingTime(m_windowMs);
    m_windowTimer.start(m_windowMs);
    m_windowSeen = false;
    setPairWindow(true);
    m_bluez->startCompanionPairing(BluezModel::defaultPairingTimeout);
}

void LinkServer::setPairWindowMs(int ms)
{
    m_windowMs = ms;
}

bool LinkServer::companionWindow() const
{
    return m_pairWindow && m_windowSeen && m_bluez && m_bluez->pairing()
           && m_bluez->pairingMode() == QLatin1StringView("companion");
}

void LinkServer::closeWindow(bool stop)
{
    m_windowTimer.stop();
    m_windowSeen = false;
    if (!m_pairWindow)
        return;
    setPairWindow(false);
    if (stop && m_bluez && m_bluez->pairing() && m_bluez->pairingMode() == QLatin1StringView("companion"))
        m_bluez->stopPairing();
}

void LinkServer::bind(const QString &device)
{
    if (!m_settings->companion())
        return;
    const QString name = m_confirmName;
    qInfo().noquote() << "microdap link: phone app bound to" << address(device) << "by pairing";
    m_settings->setCompanionPhone(address(device));
    const qint64 now = m_clock().boot;
    for (auto &p : m_peers) {
        if (!p->refused && !p->s->closed() && p->device != device)
            p->s->bye(proto::val::BYE_REASON::replaced, now);
    }
    closeWindow(true);
    emit phoneBound(name.isEmpty() ? address(device) : name);
    poll();
}

void LinkServer::companionChanged()
{
    const bool on = m_settings->companion();
    if (on == m_companion)
        return;
    m_companion = on;
    if (on)
        return;
    closeWindow(true);
    bye(proto::val::BYE_REASON::companionOff);
    m_settings->setCompanionPhone(QString());
}

void LinkServer::open(int fd, const QString &device)
{
    if (fd < 0)
        return;
    if (!m_settings->companion()) {
        ::close(fd);
        return;
    }
    const qint64 now = m_clock().boot;
    const QString from = address(device);
    const QString bound = m_settings->companionPhone();
    const bool other = !bound.isEmpty() && from != bound;
    const bool window = companionWindow();
    const bool known = window && m_bluez->companionKnown().contains(device);
    const bool gone = m_bluez && m_bluez->companionRemoving().contains(device);
    const bool candidate = other && known && !gone && m_windowEnd.remainingTime() != 0;
    const bool refused = (other && !candidate) || (window && !known) || gone;
    refresh();
    auto p = std::make_unique<Peer>();
    p->device = device;
    p->refused = refused;
    p->candidate = candidate;
    p->gen = ++m_gen;
    p->s = std::make_unique<fd::LinkSession>(fd, hello(), this, now);
    if (p->s->closed()) {
        qWarning().noquote() << "microdap link: session from" << from << "closed at open:"
                             << QString::fromStdString(p->s->why());
        return;
    }
    p->s->on_close([this](int f) { unwatch(f); });
    p->read = new QSocketNotifier(fd, QSocketNotifier::Read, this);
    p->write = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    p->write->setEnabled(false);
    connect(p->read, &QSocketNotifier::activated, this, &LinkServer::poll);
    connect(p->write, &QSocketNotifier::activated, this, &LinkServer::poll);
    qInfo().noquote() << "microdap link: session" << p->gen << "from" << from << (refused ? "refused" : "");
    if (refused) {
        p->s->bye(proto::val::BYE_REASON::refused, now);
    } else if (!candidate) {
        for (auto &o : m_peers) {
            if (!o->refused && !o->s->closed())
                o->s->bye(proto::val::BYE_REASON::replaced, now);
        }
    }
    m_peers.push_back(std::move(p));
    poll();
}

void LinkServer::end(const QString &device)
{
    const qint64 now = m_clock().boot;
    for (auto &p : m_peers) {
        if ((device.isEmpty() || p->device == device) && !p->s->closed())
            p->s->bye(proto::val::BYE_REASON::shutdown, now);
    }
    poll();
}

void LinkServer::bye(uint8_t reason)
{
    const qint64 now = m_clock().boot;
    for (auto &p : m_peers) {
        if (!p->s->closed())
            p->s->bye(reason, now);
    }
    poll();
}

void LinkServer::unwatch(int fd)
{
    for (auto &p : m_peers) {
        if (p->s->fd() != fd)
            continue;
        if (p->read) {
            p->read->setEnabled(false);
            p->read->deleteLater();
            p->read = nullptr;
        }
        if (p->write) {
            p->write->setEnabled(false);
            p->write->deleteLater();
            p->write = nullptr;
        }
    }
}

void LinkServer::watch(Peer &p)
{
    if (p.s->closed())
        return;
    if (p.read)
        p.read->setEnabled(p.s->reading());
    if (p.write)
        p.write->setEnabled(p.s->want_write());
}

void LinkServer::tick(Peer &p, qint64 now)
{
    m_ticking = &p;
    const int ms = p.s->tick(now);
    m_ticking = nullptr;
    watch(p);
    if (ms >= 0 && (m_nextAt < 0 || now + ms < m_nextAt))
        m_nextAt = now + ms;
}

void LinkServer::poll()
{
    if (m_polling)
        return;
    m_polling = true;
    const qint64 now = m_clock().boot;
    m_nextAt = -1;
    if (m_connected)
        refresh();
    for (size_t i = 0; i < m_peers.size(); ++i) {
        Peer &p = *m_peers[i];
        if (p.s->closed())
            continue;
        tick(p, now);
        if (p.hello && !p.refused && p.s->open()) {
            state(p, now);
            settings(p, now);
        }
    }
    for (auto it = m_peers.begin(); it != m_peers.end();) {
        if ((*it)->s->closed()) {
            qInfo().noquote() << "microdap link: session" << (*it)->gen << "closed:" << QString::fromStdString((*it)->s->why());
            it = m_peers.erase(it);
        } else {
            ++it;
        }
    }
    updateConnected();
    if (m_sources.folders)
        m_sources.folders->setActive(m_connected);
    if (m_connected && m_sent.playing && m_sentAt >= 0) {
        const qint64 t = m_sentAt + proto::stateIntervalMs;
        if (m_nextAt < 0 || t < m_nextAt)
            m_nextAt = t;
    }
    schedule(now);
    m_polling = false;
}

void LinkServer::schedule(qint64 now)
{
    if (m_nextAt < 0) {
        m_timer.stop();
        return;
    }
    m_timer.start(int(std::clamp<qint64>(m_nextAt - now, 0, 24 * 3600 * 1000)));
}

void LinkServer::updateConnected()
{
    bool on = false;
    for (const auto &p : m_peers)
        on = on || (p->hello && !p->refused && p->s->open());
    if (on == m_connected)
        return;
    m_connected = on;
    emit connectedChanged();
}

bool LinkServer::phone() const
{
    return m_bluez && m_bluez->transportActive();
}

int LinkServer::cap() const
{
    return m_settings->maxVolume();
}

LinkSig LinkServer::sig(qint64) const
{
    LinkSig s;
    s.source = phone() ? proto::val::SOURCE::phone : proto::val::SOURCE::local;
    s.output = phone() ? proto::val::OUTPUT::phoneReceiver : proto::val::OUTPUT::wired;
    s.cap = cap();
    s.volume = m_level < 0 ? -1 : std::min(m_level, s.cap);
    s.step = m_settings->volumeStep();
    if (m_status && m_status->batteryPresent()) {
        s.battery = m_status->battery();
        s.charging = m_status->charging();
    }
    const Queue *queue = m_player->queue();
    s.queueRev = queue->rev();
    s.slot = quint32(std::max(0, queue->currentSlot()));
    s.slotCount = quint32(std::max(0, queue->count()));
    if (phone()) {
        s.title = m_bluez->title();
        s.playing = m_bluez->status() == QLatin1StringView("playing");
        s.position = m_bluez->positionMs();
        return s;
    }
    const int index = queue->currentIndex();
    if (index >= 0) {
        const QueueEntry e = queue->entry(index);
        s.track = e.id;
        s.title = e.title;
        if (m_favorites && e.id > 0)
            s.favorite = m_favorites->contains(e.id) ? 1 : 0;
        if (m_lyrics && !m_lyrics->pending()) {
            const LyricsText text = m_lyrics->current();
            s.lyrics = text.timed() ? 1 : text.plain.isEmpty() ? 0 : 2;
            if (s.lyrics > 0)
                s.lyricsCrc = lyricsCrc(text);
        }
    }
    s.held = m_player->playPending();
    s.playing = m_player->status() == Player::Playing || s.held;
    s.shuffle = queue->shuffle();
    s.repeat = int(queue->repeat());
    s.position = m_player->position();
    return s;
}

void LinkServer::writeState(fd::LinkWriter &w, const LinkTime &at, const LinkSig &s) const
{
    namespace t = proto::tag::STATE;
    w.u8(t::source, uint8_t(s.source)).u8(t::output, uint8_t(s.output));
    if (phone()) {
        w.u64(t::track, 0).str(t::title, utf8(m_bluez->title())).str(t::artist, utf8(m_bluez->artist()));
        if (!m_bluez->artist().isEmpty())
            w.str(t::artists, utf8(m_bluez->artist()));
        w.u32(t::durationMs, uint32_t(std::max<qint64>(0, m_bluez->durationMs())))
            .u32(t::positionMs, uint32_t(std::max<qint64>(0, s.position)));
    } else {
        const Queue *queue = m_player->queue();
        const int index = queue->currentIndex();
        if (index >= 0) {
            const QueueEntry e = queue->entry(index);
            w.u64(t::track, uint64_t(std::max<qint64>(0, e.id))).str(t::title, utf8(e.title)).str(t::artist, utf8(e.artist));
            QStringList names = m_player->library() && e.id > 0 ? m_player->library()->trackArtists(e.id) : QStringList();
            if (names.isEmpty() && !e.artist.isEmpty())
                names = {e.artist};
            for (const QString &a : names)
                w.str(t::artists, utf8(a));
        }
        w.u32(t::durationMs, uint32_t(std::max<qint64>(0, m_player->duration())))
            .u32(t::positionMs, uint32_t(std::max<qint64>(0, s.position)));
    }
    w.u64(t::sampleMs, uint64_t(at.boot)).flag(t::playing, s.playing).flag(t::positionHeld, s.held);
    w.flag(t::shuffle, s.shuffle).u8(t::repeat, uint8_t(s.repeat));
    const int level = std::max(0, s.volume);
    w.u16(t::volume, uint16_t(level)).u16(t::maxVolume, uint16_t(s.cap));
    w.u8(t::volumePct, uint8_t(level)).u8(t::volumeStepPct, uint8_t(s.step)).u8(t::maxVolumePct, uint8_t(s.cap));
    if (s.battery >= 0)
        w.u8(t::battery, uint8_t(std::min(s.battery, 100)));
    if (m_status)
        w.flag(t::charging, s.charging);
    w.u32(t::queueRev, s.queueRev).u32(t::slot, s.slot).u32(t::slots, s.slotCount).u32(t::libRev, m_libRev);
    if (phone()) {
        w.str(t::album, utf8(m_bluez->album()));
    } else {
        const Queue *queue = m_player->queue();
        if (queue->currentIndex() >= 0)
            w.str(t::album, utf8(queue->entry(queue->currentIndex()).album));
    }
    w.u32(t::sleepLeftS, 0);
    if (s.lyrics >= 0)
        w.flag(t::lyrics, s.lyrics == 1).flag(t::lyricsPlain, s.lyrics == 2);
    if (s.lyrics > 0)
        w.u32(t::lyricsCrc, s.lyricsCrc);
    if (s.favorite >= 0)
        w.flag(t::favorite, s.favorite != 0);
}

void LinkServer::state(Peer &p, qint64 now)
{
    const LinkTime at = m_clock();
    const LinkSig s = sig(at.mono);
    const bool moved = s.position != m_seen;
    m_seen = s.position;
    bool due = m_sentAt < 0 || !s.same(m_sent) || m_libRev != m_sentRev;
    if (!due && s.playing) {
        const qint64 expect = m_sent.position + (m_sent.playing ? now - m_sentAt : 0);
        due = now - m_sentAt >= proto::stateIntervalMs || (moved && std::llabs(s.position - expect) > 2000);
    }
    if (!due && !s.playing && m_sent.position != s.position)
        due = true;
    if (!due)
        return;
    fd::LinkWriter w;
    writeState(w, at, s);
    w.u32(proto::tag::STATE::snapRev, m_snapRev);
    w.u64(proto::tag::STATE::sentMs, uint64_t(m_clock().boot));
    if (!p.s->push(proto::msg::STATE, w, now))
        return;
    m_sent = s;
    m_sentRev = m_libRev;
    m_sentAt = now;
    tick(p, now);
}

void LinkServer::command(fd::LinkCommand &c, int64_t now)
{
    Peer *p = m_ticking;
    if (!p)
        return;
    namespace m = proto::msg;
    if (c.type == m::HELLO) {
        p->hello = true;
        m_sentAt = -1;
        qInfo().noquote() << "microdap link: session" << p->gen << "hello, app"
                          << QString::fromStdString(c.body.str("appVersion"));
        if (p->refused)
            return;
        const QString from = address(p->device);
        const QString bound = m_settings->companionPhone();
        const bool window = companionWindow();
        const bool known = window && m_bluez->companionKnown().contains(p->device);
        const bool gone = m_bluez && m_bluez->companionRemoving().contains(p->device);
        const bool rebind = p->candidate && known && !gone && m_windowEnd.remainingTime() != 0;
        p->candidate = false;
        if (!m_settings->companion() || gone || (window && !known)
            || (!bound.isEmpty() && bound != from && !rebind)) {
            p->refused = true;
            p->s->bye(proto::val::BYE_REASON::refused, now);
            return;
        }
        if (bound.isEmpty() || rebind) {
            qInfo().noquote() << "microdap link: phone app bound to" << from << (rebind ? "by the pairing window" : "");
            m_settings->setCompanionPhone(from);
        }
        if (rebind) {
            for (auto &o : m_peers) {
                if (o.get() != p && !o->refused && !o->s->closed())
                    o->s->bye(proto::val::BYE_REASON::replaced, now);
            }
            closeWindow(true);
            m_soon.start();
        }
        emit phoneHello();
        return;
    }
    if (c.type == m::SET || c.type == m::DRAFT) {
        settingsCommand(*p, c, now);
        return;
    }
    if (c.type == m::ART_GET)
        return artGet(*p, c, now);
    if (c.type == m::LRC_PUT || c.type == m::LRC_DEL)
        return lrcCommand(*p, c, now);
    if (c.type == m::LYR_GET)
        return lyrGet(*p, c, now);
    if (library(*p, c, now))
        return;
    if (c.type != m::TRANSPORT && c.type != m::VOLUME)
        return;
    const uint8_t code = p->refused ? uint8_t(proto::err::refused) : c.type == m::TRANSPORT ? transport(c) : volume(c);
    if (!code)
        p->s->reply(c, fd::LinkWriter(), now);
    else
        p->s->fail(c, code, fd::link_err_text(code), now);
    m_soon.start();
}

uint8_t LinkServer::transport(const fd::LinkCommand &c)
{
    namespace v = proto::val::TRANSPORT_CMD;
    if (!c.body.has("cmd"))
        return proto::err::badValue;
    const bool fromPhone = phone();
    Queue *queue = m_player->queue();
    switch (c.body.u("cmd")) {
    case v::toggle:
        if (!fromPhone)
            m_player->playPause();
        else if (m_bluez->status() == QLatin1StringView("playing"))
            m_bluez->pause();
        else
            m_bluez->play();
        return 0;
    case v::play:
        if (fromPhone)
            m_bluez->play();
        else
            m_player->resume();
        return 0;
    case v::pause:
        if (fromPhone)
            m_bluez->pause();
        else
            m_player->pause();
        return 0;
    case v::next:
        if (fromPhone)
            m_bluez->next();
        else
            m_player->next();
        return 0;
    case v::prev:
        if (fromPhone)
            m_bluez->previous();
        else
            m_player->previous();
        return 0;
    case v::seek:
        if (fromPhone)
            return proto::err::refused;
        if (!c.body.has("seekMs"))
            return proto::err::badValue;
        m_player->seek(qint64(c.body.u("seekMs")));
        return 0;
    case v::shuffle:
        if (fromPhone)
            return proto::err::refused;
        if (!c.body.has("on"))
            return proto::err::badValue;
        queue->setShuffle(c.body.u("on") != 0);
        return 0;
    case v::repeat:
        if (fromPhone)
            return proto::err::refused;
        if (!c.body.has("repeat"))
            return proto::err::badValue;
        queue->setRepeat(Queue::Repeat(c.body.u("repeat")));
        return 0;
    default:
        return proto::err::badValue;
    }
}

uint8_t LinkServer::volume(const fd::LinkCommand &c)
{
    if (!m_volume)
        return proto::err::busy;
    if (c.body.has("set")) {
        m_volume->set(std::clamp(int(c.body.u("set")), 0, cap()));
        return 0;
    }
    if (c.body.has("percent")) {
        m_volume->set(std::min<int>(int(std::min<uint64_t>(c.body.u("percent"), 100)), cap()));
        return 0;
    }
    if (c.body.has("step")) {
        if (c.body.i("step") != 0)
            m_volume->step(int(std::clamp<int64_t>(c.body.i("step"), -100, 100)) * m_settings->volumeStep());
        return 0;
    }
    return proto::err::badValue;
}

bool LinkServer::knownTracks(const QVariantList &ids) const
{
    Library *lib = m_player->library();
    if (!lib)
        return false;
    QSet<qint64> found;
    for (const QVariant &r : lib->trackRows(ids))
        found.insert(r.toMap().value(QStringLiteral("id")).toLongLong());
    for (const QVariant &id : ids) {
        if (!found.contains(id.toLongLong()))
            return false;
    }
    return true;
}

uint8_t LinkServer::revCode(const fd::LinkBody &b, bool list, bool need) const
{
    const bool snap = b.has("snapRev");
    if (snap && list && !b.has("libRev"))
        return proto::err::badValue;
    if (snap && !list)
        return b.u("snapRev") == m_snapRev ? 0 : uint8_t(proto::err::staleRev);
    if (!b.has("libRev"))
        return need ? uint8_t(proto::err::staleRev) : 0;
    return b.u("libRev") == m_libRev ? 0 : uint8_t(proto::err::staleRev);
}

bool LinkServer::library(Peer &p, fd::LinkCommand &c, qint64 now)
{
    namespace m = proto::msg;
    if (c.type != m::LIB_GET && c.type != m::PLAY && c.type != m::QUEUE_OP && c.type != m::ACTION)
        return false;
    uint8_t code = 0;
    if (p.refused)
        code = proto::err::refused;
    else if (c.type == m::LIB_GET)
        code = libGet(p, c, now);
    else if (c.type == m::PLAY)
        code = play(c);
    else if (c.type == m::QUEUE_OP && c.body.u("cmd", 255) == proto::val::QUEUE_CMD::page)
        code = queuePage(p, c, now);
    else if (c.type == m::QUEUE_OP)
        code = queueOp(c);
    else
        code = action(c);
    if (!code)
        p.s->reply(c, fd::LinkWriter(), now);
    else
        p.s->fail(c, code, fd::link_err_text(code), now);
    m_soon.start();
    return true;
}

bool LinkServer::pages(Peer &p, uint8_t type, const fd::LinkWriter &head, const uint8_t tags[4], quint32 &at,
                       quint32 end, quint32 total, const std::function<void(quint32, fd::LinkWriter &)> &rec,
                       qint64 now)
{
    const size_t fill = size_t(proto::maxPayload) * proto::pageFillPct / 100;
    const size_t fixed = head.size() + 3 * proto::tlvHeaderBytes + 9;
    bool first = true;
    do {
        if (!first && p.s->queued() > fd::kLinkQueueBytes / 2)
            return true;
        std::vector<fd::LinkWriter> recs;
        size_t used = fixed;
        while (at + recs.size() < end) {
            fd::LinkWriter w;
            rec(at + quint32(recs.size()), w);
            const size_t n = proto::tlvHeaderBytes + w.size();
            if (!recs.empty() && used + n > fill)
                break;
            used += n;
            recs.push_back(std::move(w));
        }
        if (p.s->queued() + used + proto::headerBytes > fd::kLinkQueueBytes)
            return false;
        const quint32 n = quint32(recs.size());
        fd::LinkWriter body = head;
        body.u32(tags[0], at).u32(tags[1], total).flag(tags[2], at + n < total);
        for (const fd::LinkWriter &w : recs)
            body.rec(tags[3], w);
        if (!p.s->push(type, body, now))
            return false;
        at += n;
        first = false;
    } while (at < end);
    return true;
}

uint8_t LinkServer::libGet(Peer &p, const fd::LinkCommand &c, qint64 now)
{
    namespace t = proto::tag::LIB;
    namespace r = proto::tag::LibRow;
    if (!c.body.has("view"))
        return proto::err::badValue;
    const uint8_t view = uint8_t(c.body.u("view"));
    const bool list = view == proto::val::VIEW::playlists || view == proto::val::VIEW::favorites;
    if (const uint8_t e = revCode(c.body, list, false))
        return e;
    const QString parent = QString::fromStdString(c.body.str("parent"));
    LinkView v(m_sources);
    if (const uint8_t e = v.open(view, parent))
        return e;
    const quint64 offset = c.body.u("offset");
    if (offset > v.total())
        return proto::err::badValue;
    const quint64 count = c.body.has("count") ? c.body.u("count") : v.total();
    const quint32 end = count >= v.total() - offset ? v.total() : quint32(offset + count);
    fd::LinkWriter head;
    head.u32(t::libRev, m_libRev).u8(t::view, view).str(t::parent, utf8(parent)).u32(t::snapRev, m_snapRev);
    const uint8_t tags[4] = {t::offset, t::total, t::more, t::row};
    quint32 at = quint32(offset);
    const bool ok = pages(p, proto::msg::LIB, head, tags, at, end, v.total(), [&v](quint32 i, fd::LinkWriter &w) {
        const LinkRow row = v.row(i);
        w.str(r::name, utf8(row.name)).u32(r::count, row.count);
        if (row.hasFirst)
            w.u64(r::first, uint64_t(row.first));
        w.str(r::key, utf8(row.key));
    }, now);
    return ok ? 0 : uint8_t(proto::err::busy);
}

uint8_t LinkServer::play(const fd::LinkCommand &c)
{
    if (!m_player->library() || !m_player->library()->isOpen())
        return proto::err::noLibrary;
    QVariantList tracks;
    const std::vector<const fd::LinkField *> ids = c.body.all("track");
    if (!ids.empty()) {
        if (ids.size() > size_t(proto::playMaxTracks))
            return proto::err::tooBig;
        for (const fd::LinkField *f : ids)
            tracks.push_back(qint64(f->u));
        if (!knownTracks(tracks))
            return proto::err::staleRev;
    } else {
        if (!c.body.has("view"))
            return proto::err::badValue;
        const uint8_t view = uint8_t(c.body.u("view"));
        const bool list = view == proto::val::VIEW::playlists || view == proto::val::VIEW::favorites;
        if (const uint8_t e = revCode(c.body, list, true))
            return e;
        LinkView v(m_sources);
        if (const uint8_t e = v.open(view, QString::fromStdString(c.body.str("parent"))))
            return e;
        tracks = v.tracks();
    }
    const quint64 start = c.body.u("start");
    if (tracks.isEmpty() || start >= quint64(tracks.size()))
        return proto::err::badValue;
    const QList<QueueEntry> entries = m_player->library()->queueEntries(tracks);
    if (entries.size() != tracks.size())
        return proto::err::staleRev;
    if (c.body.u("shuffle") != 0)
        m_player->queue()->setShuffle(true);
    m_player->play(entries, int(start));
    return 0;
}

uint8_t LinkServer::queuePage(Peer &p, const fd::LinkCommand &c, qint64 now)
{
    namespace t = proto::tag::QUEUE;
    namespace q = proto::tag::QueueItem;
    Queue *queue = m_player->queue();
    if (c.body.has("rev") && c.body.u("rev") != queue->rev())
        return proto::err::staleRev;
    const quint32 slots = quint32(std::max(0, queue->count()));
    const quint64 offset = c.body.u("offset");
    if (offset > slots)
        return proto::err::badValue;
    const quint64 count = c.body.has("count") ? c.body.u("count") : slots;
    const quint32 end = count >= slots - offset ? slots : quint32(offset + count);
    fd::LinkWriter head;
    head.u32(t::rev, queue->rev());
    const uint8_t tags[4] = {t::offset, t::total, t::more, t::item};
    Library *lib = m_player->library();
    quint32 at = quint32(offset);
    const bool ok = pages(p, proto::msg::QUEUE, head, tags, at, end, slots, [queue, lib](quint32 i, fd::LinkWriter &w) {
        const QueueEntry e = queue->entry(queue->rowAt(int(i)));
        w.u64(q::track, uint64_t(std::max<qint64>(0, e.id))).str(q::title, utf8(e.title));
        QStringList names = lib && e.id > 0 ? lib->trackArtists(e.id) : QStringList();
        if (names.isEmpty() && !e.artist.isEmpty())
            names = {e.artist};
        int kept = 0;
        for (const QString &a : std::as_const(names)) {
            const std::string name = utf8(a);
            if (kept >= linkQueueArtists || name.size() > linkNameMax)
                continue;
            w.str(q::artists, name);
            ++kept;
        }
        if (!e.album.isEmpty())
            w.str(q::album, utf8(e.album));
        if (e.durationMs > 0)
            w.u32(q::durationMs, uint32_t(e.durationMs));
    }, now);
    return ok ? 0 : uint8_t(proto::err::busy);
}

uint8_t LinkServer::queueOp(const fd::LinkCommand &c)
{
    namespace q = proto::val::QUEUE_CMD;
    Queue *queue = m_player->queue();
    if (!c.body.has("rev") || c.body.u("rev") != queue->rev())
        return proto::err::staleRev;
    const quint64 slot = c.body.u("slot", UINT64_MAX);
    const quint64 slots = quint64(std::max(0, queue->count()));
    const bool in = slot < slots;
    switch (c.body.u("cmd", 255)) {
    case q::playAt:
        if (!in)
            return proto::err::badValue;
        m_player->playAt(queue->rowAt(int(slot)));
        return 0;
    case q::remove:
        if (!in)
            return proto::err::badValue;
        queue->remove(queue->rowAt(int(slot)));
        return 0;
    case q::move:
        if (!in || c.body.u("to", UINT64_MAX) >= slots)
            return proto::err::badValue;
        queue->moveSlot(int(slot), int(c.body.u("to")));
        return 0;
    case q::playNext: {
        const std::vector<const fd::LinkField *> ids = c.body.all("track");
        if (ids.empty()) {
            if (!in)
                return proto::err::badValue;
            queue->playNextRow(queue->rowAt(int(slot)));
            return 0;
        }
        if (ids.size() > size_t(proto::playMaxTracks))
            return proto::err::tooBig;
        QVariantList tracks;
        for (const fd::LinkField *f : ids)
            tracks.push_back(qint64(f->u));
        if (!knownTracks(tracks))
            return proto::err::staleRev;
        return m_player->playNextTracks(tracks) > 0 ? 0 : uint8_t(proto::err::badValue);
    }
    case q::clear:
        m_player->stop();
        queue->clear();
        return 0;
    default:
        return proto::err::badValue;
    }
}

uint8_t LinkServer::action(const fd::LinkCommand &c)
{
    namespace a = proto::val::ACTION_CMD;
    Library *lib = m_player->library();
    if (!lib || !lib->isOpen())
        return proto::err::noLibrary;
    switch (c.body.u("cmd", 255)) {
    case a::rescan:
        lib->rescanRoots();
        return 0;
    case a::shuffleLibrary:
        return m_player->playSource(QStringLiteral("all"), 0, QStringLiteral("shuffle")) > 0 ? 0
                                                                                            : uint8_t(proto::err::noLibrary);
    case a::favorite: {
        if (!c.body.has("track"))
            return proto::err::badValue;
        const qint64 id = qint64(c.body.u("track"));
        if (!knownTracks({id}))
            return proto::err::staleRev;
        if (!m_favorites)
            return proto::err::busy;
        m_favorites->toggle(id);
        return 0;
    }
    default:
        return proto::err::badValue;
    }
}

void LinkServer::refresh()
{
    if (m_settingsFresh)
        return;
    m_settingsFresh = true;
    const LinkSettings link(m_settings, version());
    const QList<LinkSettingRow> rows = link.rows();
    std::vector<Pivot> pivots;
    QByteArray sig;
    for (const LinkSettingRow &r : rows) {
        if (pivots.empty() || pivots.back().name != r.pivot)
            pivots.push_back({r.pivot, {}});
        namespace t = proto::tag::Row;
        fd::LinkWriter w;
        w.str(t::key, utf8(r.key)).str(t::label, utf8(r.label)).str(t::pivot, utf8(r.pivot)).u8(t::kind, r.kind);
        for (const QString &v : r.values)
            w.str(t::value, utf8(v));
        if (r.max > r.min)
            w.i32(t::min, r.min).i32(t::max, r.max).i32(t::step, r.step);
        for (const QString &word : r.words)
            w.str(t::word, utf8(word));
        if (r.readOnly)
            w.u8(t::flag, proto::val::ROW_FLAG::readOnly);
        if (r.restart)
            w.u8(t::flag, proto::val::ROW_FLAG::restart);
        if (r.boardOnly)
            w.u8(t::flag, proto::val::ROW_FLAG::boardOnly);
        sig.append(r.pivot.toUtf8()).append('\n').append(reinterpret_cast<const char *>(w.data().data()), qsizetype(w.size()));
        pivots.back().rows.push_back(std::move(w));
    }
    if (sig != m_schemaSig) {
        m_schemaSig = sig;
        m_schema = std::move(pivots);
        ++m_schemaRev;
    }
    const QString text = link.text();
    if (text != m_settingsText) {
        m_settingsText = text;
        ++m_settingsRev;
    }
}

void LinkServer::pushSchema(Peer &p, qint64 now)
{
    namespace t = proto::tag::SCHEMA;
    const size_t cap = size_t(proto::maxPayload) * proto::pageFillPct / 100;
    for (size_t pi = 0; pi < m_schema.size(); ++pi) {
        const std::vector<fd::LinkWriter> &rows = m_schema[pi].rows;
        const std::string name = utf8(m_schema[pi].name);
        auto page = [&](size_t at, size_t n) {
            fd::LinkWriter w;
            w.str(t::pivot, name).u32(t::offset, uint32_t(at)).u32(t::total, uint32_t(rows.size()));
            w.flag(t::more, at + n < rows.size());
            for (size_t i = at; i < at + n; ++i)
                w.rec(t::row, rows[i]);
            w.u32(t::rev, m_schemaRev).u8(t::pivotIndex, uint8_t(pi)).u8(t::pivotCount, uint8_t(m_schema.size()));
            p.s->push(proto::msg::SCHEMA, w, now);
        };
        const size_t fixed = name.size() + 48;
        size_t at = 0;
        size_t used = 0;
        size_t n = 0;
        for (size_t i = 0; i < rows.size(); ++i) {
            if (n > 0 && fixed + used + 3 + rows[i].size() > cap) {
                page(at, n);
                at = i;
                used = 0;
                n = 0;
            }
            used += 3 + rows[i].size();
            ++n;
        }
        page(at, n);
    }
}

void LinkServer::pushSettings(Peer &p, qint64 now)
{
    fd::LinkWriter w;
    w.u32(proto::tag::SETTINGS::rev, m_settingsRev).str(proto::tag::SETTINGS::text, utf8(m_settingsText));
    p.s->push(proto::msg::SETTINGS, w, now);
    p.settings = m_settingsRev;
    p.settingsSent = true;
}

void LinkServer::settings(Peer &p, qint64 now)
{
    bool sent = false;
    if (!p.schemaSent || p.schema != m_schemaRev) {
        pushSchema(p, now);
        p.schema = m_schemaRev;
        p.schemaSent = true;
        sent = true;
    }
    if (!p.settingsSent || p.settings != m_settingsRev) {
        pushSettings(p, now);
        sent = true;
    }
    if (sent && !m_ticking && p.s->open())
        tick(p, now);
}

void LinkServer::settingsCommand(Peer &p, fd::LinkCommand &c, qint64 now)
{
    namespace m = proto::msg;
    QStringList kept;
    QStringList refused;
    uint8_t code = proto::err::refused;
    if (!p.refused && c.type == m::SET) {
        code = c.body.has("text") ? LinkSettings(m_settings, version()).set(QString::fromStdString(c.body.str("text")), kept,
                                                                             refused)
                                  : uint8_t(proto::err::badValue);
    }
    m_settingsFresh = false;
    refresh();
    if (!code)
        pushSettings(p, now);
    std::vector<std::string> names;
    for (const QString &k : std::as_const(refused))
        names.push_back(utf8(k));
    if (code) {
        p.s->fail(c, code, fd::link_err_text(code), now, names);
        return;
    }
    namespace t = proto::tag::SET_reply;
    fd::LinkWriter w;
    w.u32(t::rev, m_settingsRev);
    for (const QString &k : std::as_const(kept))
        w.str(t::kept, utf8(k));
    for (const std::string &k : names)
        w.str(t::refused, k);
    p.s->reply(c, w, now);
}

LinkServer::Peer *LinkServer::peer(quint32 gen) const
{
    for (const auto &p : m_peers) {
        if (p->gen == gen && !p->s->closed())
            return p.get();
    }
    return nullptr;
}

void LinkServer::artReply(Peer &p, const fd::LinkCommand &c, quint32 offset, bool hasHave, quint32 have, qint64 now)
{
    namespace t = proto::tag::ART_GET_reply;
    const LinkJpeg &j = m_art.jpeg;
    const quint32 total = quint32(j.bytes.size());
    if (offset > total) {
        p.s->fail(c, proto::err::badValue, fd::link_err_text(proto::err::badValue), now);
        return;
    }
    if (offset > 0 && hasHave && have != j.crc)
        offset = 0;
    fd::LinkWriter w;
    if (offset == 0)
        w.u32(t::crc, j.crc).u32(t::total, total).u16(t::w, uint16_t(j.side)).u16(t::h, uint16_t(j.side));
    w.u32(t::offset, offset);
    if (!(offset == 0 && hasHave && have == j.crc)) {
        const qsizetype n = std::min<qsizetype>(qsizetype(proto::artChunk), qsizetype(total - offset));
        w.bytes(t::data, j.bytes.constData() + offset, size_t(n));
    }
    p.s->reply(c, w, now);
}

void LinkServer::artGet(Peer &p, fd::LinkCommand &c, qint64 now)
{
    uint8_t code = 0;
    Library *lib = m_player->library();
    QList<QueueEntry> entries;
    if (p.refused)
        code = proto::err::refused;
    else if (!c.body.has("track") || !c.body.has("px"))
        code = proto::err::badValue;
    else if (!lib || !lib->isOpen())
        code = proto::err::noLibrary;
    else if ((entries = lib->queueEntries({qint64(c.body.u("track"))})).isEmpty())
        code = proto::err::staleRev;
    if (code) {
        p.s->fail(c, code, fd::link_err_text(code), now);
        return;
    }
    const qint64 track = qint64(c.body.u("track"));
    const int px = int(std::clamp<uint64_t>(c.body.u("px"), uint64_t(proto::artMinPx), uint64_t(proto::artMaxPx)));
    const quint32 offset = quint32(std::min<uint64_t>(c.body.u("offset"), UINT32_MAX));
    const bool hasHave = c.body.has("have");
    const quint32 have = quint32(c.body.u("have"));
    if (m_art.ok && m_art.track == track && m_art.px == px && m_art.rev == m_snapRev) {
        artReply(p, c, offset, hasHave, have, now);
        return;
    }
    const auto same = std::find_if(m_artPending.cbegin(), m_artPending.cend(), [&](const ArtPending &o) {
        return o.track == track && o.px == px && o.rev == m_snapRev;
    });
    if (m_artPending.size() >= linkWaitMax || (same == m_artPending.cend() && jobs(m_artPending) >= linkJobMax)) {
        p.s->fail(c, proto::err::busy, fd::link_err_text(proto::err::busy), now);
        return;
    }
    ArtPending q;
    q.job = same != m_artPending.cend() ? same->job : ++m_artJob;
    q.gen = p.gen;
    q.c.type = c.type;
    q.c.flags = c.flags;
    q.c.seq = c.seq;
    q.offset = offset;
    q.have = have;
    q.hasHave = hasHave;
    q.track = track;
    q.px = px;
    q.rev = m_snapRev;
    const bool started = same != m_artPending.cend();
    m_artPending.push_back(q);
    c.deferred = true;
    if (started)
        return;
    const QString path = entries.constFirst().artPath;
    const quint32 job = q.job;
    QPointer<LinkServer> self(this);
    m_pool.start([self, job, path, px] {
        LinkJpeg jpeg;
        const bool ok = linkEncodeArt(path, px, jpeg);
        QMetaObject::invokeMethod(
            self.data(), [self, job, ok, jpeg] {
                if (self)
                    self->artDone(job, ok, jpeg);
            },
            Qt::QueuedConnection);
    });
}

qsizetype LinkServer::jobs(const std::vector<ArtPending> &pending)
{
    QSet<quint32> ids;
    for (const ArtPending &q : pending)
        ids.insert(q.job);
    return ids.size();
}

void LinkServer::artDone(quint32 job, bool ok, const LinkJpeg &jpeg)
{
    std::vector<ArtPending> done;
    for (auto it = m_artPending.begin(); it != m_artPending.end();) {
        if (it->job == job) {
            done.push_back(*it);
            it = m_artPending.erase(it);
        } else {
            ++it;
        }
    }
    if (done.empty())
        return;
    if (ok) {
        m_art.ok = true;
        m_art.track = done.front().track;
        m_art.px = done.front().px;
        m_art.rev = done.front().rev;
        m_art.jpeg = jpeg;
    }
    const qint64 now = m_clock().boot;
    for (const ArtPending &q : done) {
        Peer *p = peer(q.gen);
        if (!p)
            continue;
        if (!ok)
            p->s->fail(q.c, proto::err::notFound, fd::link_err_text(proto::err::notFound), now);
        else
            artReply(*p, q.c, q.offset, q.hasHave, q.have, now);
    }
    m_soon.start();
}

void LinkServer::lrcCommand(Peer &p, fd::LinkCommand &c, qint64 now)
{
    namespace m = proto::msg;
    const bool put = c.type == m::LRC_PUT;
    const fd::LinkField *text = c.body.get("text");
    Library *lib = m_player->library();
    QList<QueueEntry> entries;
    uint8_t code = 0;
    if (p.refused)
        code = proto::err::refused;
    else if (!c.body.has("track")
             || (put && (!text || (!c.body.has("libRev") && !c.body.has("snapRev")) || !c.body.has("crc"))))
        code = proto::err::badValue;
    else if (put && text->s.size() > size_t(proto::lrcMaxBytes))
        code = proto::err::tooBig;
    else if (const uint8_t e = put ? revCode(c.body, false, true) : 0)
        code = e;
    else if (put && linkCrc32(text->s.data(), qsizetype(text->s.size())) != quint32(c.body.u("crc")))
        code = proto::err::crc;
    else if (!lib || !lib->isOpen())
        code = proto::err::noLibrary;
    else if ((entries = lib->queueEntries({qint64(c.body.u("track"))})).isEmpty())
        code = proto::err::staleRev;
    else if (put && entries.constFirst().cueIndex > 0)
        code = proto::err::badValue;
    if (!code && entries.constFirst().cueIndex == 0) {
        const QString audio = entries.constFirst().path;
        if (put) {
            switch (Lyrics::storePut(audio, QByteArray::fromStdString(text->s))) {
            case Lyrics::Put::Ok:
                trimLyrics(audio);
                break;
            case Lyrics::Put::Empty:
                code = proto::err::badValue;
                break;
            case Lyrics::Put::Synced:
                code = proto::err::refused;
                break;
            case Lyrics::Put::Failed:
                code = proto::err::noSpace;
                break;
            }
        } else if (!Lyrics::storeDel(audio)) {
            code = proto::err::readOnly;
        }
        if (!code && m_lyrics)
            m_lyrics->refresh();
    }
    if (code)
        p.s->fail(c, code, fd::link_err_text(code), now);
    else
        p.s->reply(c, fd::LinkWriter(), now);
}

void LinkServer::lyrReply(Peer &p, const fd::LinkCommand &c, const LyricsText &text, quint32 offset, bool hasHave,
                          quint32 have, qint64 now)
{
    namespace t = proto::tag::LYR_GET_reply;
    namespace l = proto::tag::LyrLine;
    const bool synced = text.timed();
    const quint32 total = quint32(synced ? text.lines.size() : text.plain.size());
    if (!total) {
        p.s->fail(c, proto::err::notFound, fd::link_err_text(proto::err::notFound), now);
        return;
    }
    const quint32 crc = lyricsCrc(text);
    if (offset > 0 && hasHave && have != crc)
        offset = 0;
    if (offset > total) {
        p.s->fail(c, proto::err::badValue, fd::link_err_text(proto::err::badValue), now);
        return;
    }
    const bool head = offset == 0 && hasHave && have == crc;
    fd::LinkWriter w;
    if (offset == 0)
        w.u32(t::crc, crc)
            .u8(t::kind, synced ? proto::val::LYRICS_KIND::synced : proto::val::LYRICS_KIND::plain)
            .u32(t::total, total);
    const size_t fill = size_t(proto::maxPayload) * proto::pageFillPct / 100;
    size_t used = w.size() + 2 * proto::tlvHeaderBytes + 5;
    std::vector<fd::LinkWriter> recs;
    while (!head && offset + recs.size() < total) {
        const qsizetype i = qsizetype(offset + recs.size());
        fd::LinkWriter r;
        if (synced)
            r.u32(l::ms, quint32(text.lines.at(i).ms));
        const QByteArray line = Lyrics::cutLine(synced ? text.lines.at(i).text : text.plain.at(i));
        r.str(l::text, std::string_view(line.constData(), size_t(line.size())));
        const size_t n = proto::tlvHeaderBytes + r.size();
        if (!recs.empty() && used + n > fill)
            break;
        used += n;
        recs.push_back(std::move(r));
    }
    w.u32(t::offset, offset).flag(t::more, offset + recs.size() < total && !head);
    for (const fd::LinkWriter &r : recs)
        w.rec(t::line, r);
    p.s->reply(c, w, now);
}

void LinkServer::lyrGet(Peer &p, fd::LinkCommand &c, qint64 now)
{
    Library *lib = m_player->library();
    QList<QueueEntry> entries;
    uint8_t code = 0;
    if (p.refused)
        code = proto::err::refused;
    else if (!c.body.has("track"))
        code = proto::err::badValue;
    else if (!lib || !lib->isOpen())
        code = proto::err::noLibrary;
    else if ((entries = lib->queueEntries({qint64(c.body.u("track"))})).isEmpty())
        code = proto::err::staleRev;
    if (code) {
        p.s->fail(c, code, fd::link_err_text(code), now);
        return;
    }
    const qint64 track = qint64(c.body.u("track"));
    const quint32 offset = quint32(std::min<uint64_t>(c.body.u("offset"), UINT32_MAX));
    const bool hasHave = c.body.has("have");
    const quint32 have = quint32(c.body.u("have"));
    const Queue *queue = m_player->queue();
    if (m_lyrics && !m_lyrics->pending() && queue->currentIndex() >= 0 && queue->entry(queue->currentIndex()).id == track) {
        lyrReply(p, c, m_lyrics->current(), offset, hasHave, have, now);
        return;
    }
    const auto same = std::find_if(m_lyrPending.cbegin(), m_lyrPending.cend(),
                                   [track](const ArtPending &o) { return o.track == track; });
    if (m_lyrPending.size() >= linkWaitMax || (same == m_lyrPending.cend() && jobs(m_lyrPending) >= linkJobMax)) {
        p.s->fail(c, proto::err::busy, fd::link_err_text(proto::err::busy), now);
        return;
    }
    ArtPending q;
    q.job = same != m_lyrPending.cend() ? same->job : ++m_artJob;
    q.gen = p.gen;
    q.c.type = c.type;
    q.c.flags = c.flags;
    q.c.seq = c.seq;
    q.offset = offset;
    q.have = have;
    q.hasHave = hasHave;
    q.track = track;
    const bool started = same != m_lyrPending.cend();
    m_lyrPending.push_back(q);
    c.deferred = true;
    if (started)
        return;
    const QueueEntry e = entries.constFirst();
    const LyricsTrack lt{e.path, cueFrameMs(e.startFrame), e.endFrame ? cueFrameMs(e.endFrame) : 0, e.cueIndex > 0};
    const quint32 job = q.job;
    QPointer<LinkServer> self(this);
    m_pool.start([self, job, lt] {
        const LyricsText text = Lyrics::load(lt, lt.cue ? QString() : Lyrics::storeFile(lt.path));
        QMetaObject::invokeMethod(
            self.data(), [self, job, text] {
                if (self)
                    self->lyrDone(job, text);
            },
            Qt::QueuedConnection);
    });
}

void LinkServer::trimLyrics(const QString &keep)
{
    if (m_trimQueued)
        return;
    m_trimQueued = true;
    QPointer<LinkServer> self(this);
    m_pool.start([self, keep] {
        Lyrics::storeTrim(keep);
        QMetaObject::invokeMethod(
            self.data(), [self] {
                if (self)
                    self->m_trimQueued = false;
            },
            Qt::QueuedConnection);
    });
}

void LinkServer::lyrDone(quint32 job, const LyricsText &text)
{
    const qint64 now = m_clock().boot;
    bool any = false;
    for (auto it = m_lyrPending.begin(); it != m_lyrPending.end();) {
        if (it->job != job) {
            ++it;
            continue;
        }
        const ArtPending q = *it;
        it = m_lyrPending.erase(it);
        any = true;
        if (Peer *p = peer(q.gen))
            lyrReply(*p, q.c, text, q.offset, q.hasHave, q.have, now);
    }
    if (any)
        m_soon.start();
}

}
