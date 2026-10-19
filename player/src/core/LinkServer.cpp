#include "LinkServer.h"

#include "BluezModel.h"
#include "Favorites.h"
#include "Library.h"
#include "Player.h"
#include "Queue.h"
#include "Settings.h"
#include "StatusModel.h"
#include "Version.h"
#include "VolumeKeys.h"

#include <QDebug>
#include <QRandomGenerator>
#include <QSocketNotifier>

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
           && queueRev == o.queueRev && slot == o.slot && slotCount == o.slotCount;
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
    return 1ull << proto::feature::stateClock;
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
{
    m_timer.setSingleShot(true);
    m_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer, &QTimer::timeout, this, &LinkServer::poll);
    m_windowTimer.setSingleShot(true);
    connect(&m_windowTimer, &QTimer::timeout, this, [this] { closeWindow(true); });
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
    connect(favorites, &Favorites::changed, this, [this] {
        if (!m_peers.empty())
            m_soon.start();
    });
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
    for (size_t i = 0; i < m_peers.size(); ++i) {
        Peer &p = *m_peers[i];
        if (p.s->closed())
            continue;
        tick(p, now);
        if (p.hello && !p.refused && p.s->open())
            state(p, now);
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
    if (s.favorite >= 0)
        w.flag(t::favorite, s.favorite != 0);
}

void LinkServer::state(Peer &p, qint64 now)
{
    const LinkTime at = m_clock();
    const LinkSig s = sig(at.mono);
    const bool moved = s.position != m_seen;
    m_seen = s.position;
    bool due = m_sentAt < 0 || !s.same(m_sent);
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

}
