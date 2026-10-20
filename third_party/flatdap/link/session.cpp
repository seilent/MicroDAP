#include "session.h"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace fd {

namespace {

bool from_app(const proto::Msg* m)
{
    return m && (std::string_view(m->dir) == "app" || std::string_view(m->dir) == "both");
}

int group_of(uint8_t type)
{
    namespace m = proto::msg;
    switch (type) {
    case m::SCHEMA:
    case m::SETTINGS:
    case m::SET:
    case m::DRAFT:
        return proto::feature::settings;
    case m::EQ:
    case m::EQ_SET:
        return proto::feature::eq;
    case m::EQ_PRESET:
        return proto::feature::eqPresets;
    case m::ART_BLOCK:
        return proto::feature::artBlock;
    case m::ART_PICK:
        return proto::feature::artPick;
    case m::LIB:
    case m::LIB_GET:
    case m::PLAY:
    case m::ACTION:
        return proto::feature::library;
    case m::QUEUE:
    case m::QUEUE_OP:
        return proto::feature::queue;
    case m::IMG_BEGIN:
    case m::IMG_DATA:
    case m::IMG_END:
    case m::IMG_LIST:
    case m::IMG_DEL:
    case m::IMG_ABORT:
        return proto::feature::image;
    case m::IMG_TAG:
        return proto::feature::imgOrigin;
    case m::ART_GET:
        return proto::feature::art;
    case m::LYR_GET:
        return proto::feature::lyricsGet;
    case m::LRC_PUT:
    case m::LRC_DEL:
        return proto::feature::lyrics;
    case m::UPD_BEGIN:
    case m::UPD_DATA:
    case m::UPD_END:
    case m::UPD_ABORT:
        return proto::feature::update;
    default:
        return -1;
    }
}

std::vector<uint8_t> error_body(uint8_t code, const std::string& text, const std::vector<std::string>& refused)
{
    namespace t = proto::tag::error;
    LinkWriter w;
    w.u8(t::code, code).str(t::text, text);
    for (const std::string& k : refused)
        w.str(t::refused, k);
    return w.data();
}

}

LinkSession::LinkSession(int fd, const LinkHello& hello, LinkHandler* handler, int64_t now)
    : fd_(fd), hello_(hello), handler_(handler), start_(now), last_rx_(now), last_tx_(now), anchor_(now)
{
    if (fd_ < 0)
        return;
    int fl = fcntl(fd_, F_GETFL);
    if (fl < 0 || fcntl(fd_, F_SETFL, fl | O_NONBLOCK) < 0)
        close("fcntl");
}

LinkSession::~LinkSession()
{
    if (fd_ >= 0 && closer_)
        closer_(fd_);
    if (fd_ >= 0)
        ::close(fd_);
}

void LinkSession::close(const std::string& why)
{
    if (fd_ < 0)
        return;
    if (closer_)
        closer_(fd_);
    ::close(fd_);
    fd_ = -1;
    why_ = why;
    out_.clear();
    opens_.clear();
    end_ask(LinkAnswer::Closed, 0);
    queued_ = 0;
    img_ = Image{};
}

uint16_t LinkSession::next_seq()
{
    if (++seq_ == 0)
        seq_ = 1;
    return seq_;
}

bool LinkSession::busy(int64_t now) const { return busy_since_ >= 0 && now - busy_since_ < proto::busyCapMs; }

uint32_t LinkSession::interval(int64_t now) const { return busy(now) ? hello_.busy_ms : hello_.idle_ms; }

bool LinkSession::has(uint8_t type) const
{
    int g = group_of(type);
    return g < 0 || ((hello_.features >> g) & 1);
}

bool LinkSession::app_has(uint8_t type) const
{
    int g = group_of(type);
    return g < 0 || ((app_features_ >> g) & 1);
}

bool LinkSession::app_bit(int bit) const { return (app_features_ >> bit) & 1; }

void LinkSession::end_ask(LinkAnswer::Kind k, uint8_t code, LinkBody body)
{
    if (!ask_.type)
        return;
    LinkAnswer a;
    a.type = ask_.type;
    a.seq = ask_.seq;
    a.kind = k;
    a.code = code;
    a.body = std::move(body);
    answers_.push_back(a);
    ask_ = {0, 0, 0};
}

bool LinkSession::take_answer(LinkAnswer& out)
{
    if (answers_.empty())
        return false;
    out = std::move(answers_.front());
    answers_.pop_front();
    return true;
}

bool LinkSession::can_ask(uint8_t type) const
{
    const proto::Msg* m = link_msg(type);
    return open() && m && std::string_view(m->dir) == "dev" && std::string_view(m->mode) == "req" && has(type) &&
           app_has(type);
}

uint16_t LinkSession::ask(uint8_t type, const LinkWriter& body, int64_t now)
{
    if (ask_.type || !can_ask(type) || body.size() > (size_t)proto::maxPayload)
        return 0;
    uint16_t seq = next_seq();
    ask_ = {type, seq, now + proto::requestTimeoutMs};
    send(type, proto::flagWant, seq, body.data(), now);
    return fd_ >= 0 ? seq : 0;
}

void LinkSession::mode(int64_t now)
{
    bool fast = busy(now);
    if (fast && !fast_ && anchor_ <= last_rx_)
        anchor_ = now;
    fast_ = fast;
}

void LinkSession::set_busy(bool busy, int64_t now)
{
    busy_ = busy;
    if (busy && busy_since_ < 0)
        busy_since_ = now;
    mode(now);
}

int LinkSession::tick(int64_t now)
{
    if (fd_ >= 0)
        read(now);
    if (fd_ >= 0)
        write(now);
    if (fd_ >= 0)
        timers(now);
    if (fd_ < 0)
        return -1;
    int64_t iv = interval(now);
    int64_t t = INT64_MAX;
    if (open())
        t = std::max(last_rx_, last_ping_) + iv;
    if (!closing_)
        t = std::min(t, std::max(last_rx_, anchor_) + proto::deadIntervals * iv);
    if (!open_ && !closing_)
        t = std::min(t, start_ + proto::helloTimeoutMs);
    if (!out_.empty())
        t = std::min(t, std::max(last_tx_, anchor_) + proto::deadIntervals * iv);
    if (closing_)
        t = std::min(t, closing_at_ + iv);
    if (busy_since_ >= 0 && busy(now))
        t = std::min(t, busy_since_ + proto::busyCapMs);
    for (const Open& o : opens_)
        if (o.deadline > 0)
            t = std::min(t, o.deadline);
    if (ask_.type)
        t = std::min(t, ask_.deadline);
    return (int)std::min<int64_t>(INT_MAX, std::max<int64_t>(0, t - now));
}

void LinkSession::read(int64_t now)
{
    if (closing_)
        return;
    if (read_held_) {
        char b;
        ssize_t r = ::recv(fd_, &b, 1, MSG_PEEK | MSG_DONTWAIT);
        if (r == 0)
            close("eof");
        else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)
            close("read error");
        return;
    }
    uint8_t buf[4096];
    size_t total = 0;
    while (fd_ >= 0 && !closing_ && total < kLinkReadBytes) {
        ssize_t r = ::read(fd_, buf, sizeof buf);
        if (r > 0) {
            last_rx_ = now;
            total += (size_t)r;
            reader_.feed(buf, (size_t)r);
            LinkFrame f;
            while (fd_ >= 0 && !closing_ && reader_.next(f) == 1)
                frame(f, now);
            if (fd_ >= 0 && reader_.fault() != LinkFault::None)
                close(link_fault_name(reader_.fault()));
        } else if (r == 0) {
            close("eof");
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        } else {
            close("read error");
        }
    }
}

void LinkSession::write(int64_t now)
{
    while (fd_ >= 0 && !out_.empty()) {
        Out& o = out_.front();
        ssize_t r = ::send(fd_, o.bytes.data() + o.sent, o.bytes.size() - o.sent, MSG_DONTWAIT | MSG_NOSIGNAL);
        if (r > 0) {
            o.sent += (size_t)r;
            queued_ -= (size_t)r;
            last_tx_ = now;
            if (o.sent == o.bytes.size())
                out_.pop_front();
        } else if (r < 0 && errno == EINTR) {
            continue;
        } else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            close("write error");
        }
    }
    if (fd_ >= 0 && closing_ && out_.empty())
        close("bye");
}

void LinkSession::timers(int64_t now)
{
    bool b = busy_ || !opens_.empty() || !out_.empty() || img_.active || ask_.type;
    if (!b)
        busy_since_ = -1;
    else if (busy_since_ < 0)
        busy_since_ = now;
    mode(now);
    int64_t iv = interval(now);
    if (!open_ && !closing_ && now - start_ >= proto::helloTimeoutMs)
        return close("hello timeout");
    if (!closing_ && now - std::max(last_rx_, anchor_) >= proto::deadIntervals * iv)
        return close("dead");
    if (!out_.empty() && now - std::max(last_tx_, anchor_) >= proto::deadIntervals * iv)
        return close("stalled");
    if (closing_ && now - closing_at_ >= iv)
        return close("bye");
    for (size_t k = 0; k < opens_.size() && fd_ >= 0;) {
        if (opens_[k].deadline > 0 && now >= opens_[k].deadline) {
            Open o = opens_[k];
            opens_.erase(opens_.begin() + (long)k);
            send(o.type, proto::flagReply | proto::flagError, o.seq, error_body(proto::err::busy, "busy", {}), now);
        } else {
            k++;
        }
    }
    if (fd_ < 0)
        return;
    if (ask_.type && now >= ask_.deadline)
        end_ask(LinkAnswer::Timeout, 0);
    if (open() && now - std::max(last_rx_, last_ping_) >= iv) {
        last_ping_ = now;
        send(proto::msg::PING, proto::flagWant, next_seq(), {}, now);
        write(now);
    }
}

void LinkSession::frame(LinkFrame& f, int64_t now)
{
    LinkCommand c;
    c.type = f.type;
    c.flags = f.flags;
    c.seq = f.seq;
    LinkFault r = link_decode_frame(f, c.body);
    if (r != LinkFault::None)
        return close(link_fault_name(r));
    if (!open_) {
        if (f.type == proto::msg::HELLO && c.want() && !(f.flags & (proto::flagReply | proto::flagError)))
            return hello(c, now);
        return close("before hello");
    }
    if (f.flags & (proto::flagReply | proto::flagError)) {
        if (ask_.type && f.type == ask_.type && f.seq == ask_.seq) {
            uint8_t code = f.flags & proto::flagError ? (uint8_t)c.body.u("code") : 0;
            end_ask(f.flags & proto::flagError ? LinkAnswer::Error : LinkAnswer::Reply, code, std::move(c.body));
        }
        return;
    }
    dispatch(c, now);
}

void LinkSession::hello(LinkCommand& c, int64_t now)
{
    if (c.body.u("major", UINT64_MAX) != (uint64_t)proto::major)
        return bye(proto::val::BYE_REASON::version, now);
    app_features_ = c.body.u("features");
    if (handler_)
        handler_->hello(hello_, app_features_);
    namespace t = proto::tag::HELLO_reply;
    LinkWriter w;
    w.u32(t::magic, (uint32_t)proto::magic)
        .u16(t::major, (uint16_t)proto::major)
        .u16(t::minor, (uint16_t)proto::minor)
        .str(t::board, hello_.board)
        .str(t::version, hello_.version)
        .u16(t::screenW, hello_.screen_w)
        .u16(t::screenH, hello_.screen_h)
        .u16(t::backdropW, hello_.backdrop_w)
        .u16(t::backdropH, hello_.backdrop_h)
        .u32(t::maxPayload, (uint32_t)proto::maxPayload)
        .u32(t::idleMs, hello_.idle_ms)
        .u32(t::busyMs, hello_.busy_ms)
        .u64(t::features, hello_.features)
        .u32(t::libRev, hello_.lib_rev)
        .u32(t::settingsRev, hello_.settings_rev)
        .u32(t::snapRev, hello_.snap_rev);
    for (const LinkEqSlot& e : hello_.eq_slots) {
        LinkWriter r;
        r.str(proto::tag::EqSlot::key, e.key).str(proto::tag::EqSlot::label, e.label);
        w.rec(t::eqSlot, r);
    }
    for (uint8_t b : hello_.band_types)
        w.u8(t::bandTypes, b);
    send(c.type, proto::flagReply, c.seq, w.data(), now);
    open_ = true;
    if (handler_ && fd_ >= 0)
        handler_->command(c, now);
}

void LinkSession::dispatch(LinkCommand& c, int64_t now)
{
    if (c.want())
        opens_.push_back({c.type, c.seq, 0});
    const proto::Msg* m = link_msg(c.type);
    if (!from_app(m) || !has(c.type)) {
        if (c.want())
            fail(c, proto::err::unknown, "unknown", now);
        return;
    }
    if (c.type == proto::msg::HELLO) {
        fail(c, proto::err::refused, "refused", now);
        return;
    }
    if (c.want() && c.body.unknown_enum()) {
        fail(c, proto::err::badValue, "bad value", now);
        return;
    }
    if (c.type == proto::msg::PING) {
        reply(c, LinkWriter(), now);
        return;
    }
    if (!image(c, now))
        return;
    if (handler_)
        handler_->command(c, now);
    if (fd_ < 0 || !c.want())
        return;
    for (Open& o : opens_)
        if (o.seq == c.seq && o.type == c.type) {
            if (!c.deferred)
                fail(c, proto::err::unknown, "unknown", now);
            else
                o.deadline = now + (c.type == proto::msg::IMG_END ? proto::imgEndReplyMs : proto::requestTimeoutMs);
            return;
        }
}

bool LinkSession::image(LinkCommand& c, int64_t now)
{
    switch (c.type) {
    case proto::msg::IMG_BEGIN:
        if (img_.active)
            abort_push(proto::val::ABORT_CODE::superseded, now);
        img_ = Image{};
        img_.name = c.body.str("name");
        img_.total = (uint32_t)c.body.u("total");
        img_.seq = c.seq;
        return fd_ >= 0;
    case proto::msg::IMG_DATA: {
        if (!img_.active)
            return false;
        const LinkField* d = c.body.get("data");
        if (c.body.has("offset") && c.body.u("offset") == 0)
            img_.next = 0;
        if (!c.body.has("offset") || c.body.u("offset") != img_.next || !d) {
            abort_push(proto::val::ABORT_CODE::offsetMismatch, now);
            return false;
        }
        if (d->s.size() > img_.total - img_.next) {
            abort_push(proto::val::ABORT_CODE::badValue, now);
            return false;
        }
        img_.next += (uint32_t)d->s.size();
        return true;
    }
    case proto::msg::IMG_END:
        if (!img_.active) {
            if (c.want())
                fail(c, proto::err::refused, "refused", now);
            return false;
        }
        img_.active = false;
        return true;
    default:
        return true;
    }
}

void LinkSession::abort_push(uint8_t code, int64_t now)
{
    namespace t = proto::tag::IMG_ABORT;
    LinkWriter w;
    w.str(t::name, img_.name).u32(t::offset, img_.next).u8(t::code, code);
    img_.active = false;
    img_.aborted = true;
    send(proto::msg::IMG_ABORT, 0, next_seq(), w.data(), now);
}

void LinkSession::abort_image(uint8_t code, int64_t now)
{
    if (open() && img_.active)
        abort_push(code, now);
}

void LinkSession::send(uint8_t type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& body, int64_t now)
{
    if (fd_ < 0)
        return;
    if (out_.empty())
        last_tx_ = now;
    out_.push_back({type, (flags & (proto::flagReply | proto::flagError)) != 0, link_frame(type, flags, seq, body)});
    queued_ += out_.back().bytes.size();
    if (queued_ > kLinkQueueBytes)
        close("overflow");
}

bool LinkSession::push(uint8_t type, const LinkWriter& body, int64_t now)
{
    if (!open() || !has(type) || body.size() > (size_t)proto::maxPayload)
        return false;
    if (type == proto::msg::STATE || type == proto::msg::SETTINGS) {
        for (auto it = out_.begin(); it != out_.end();) {
            if (it->type == type && !it->reply && it->sent == 0) {
                queued_ -= it->bytes.size();
                it = out_.erase(it);
            } else {
                ++it;
            }
        }
    }
    send(type, 0, next_seq(), body.data(), now);
    return fd_ >= 0;
}

bool LinkSession::answer(const LinkCommand& c, uint8_t flags, const std::vector<uint8_t>& body, int64_t now)
{
    if (!c.want() || fd_ < 0 || closing_)
        return false;
    auto it =
        std::find_if(opens_.begin(), opens_.end(), [&](const Open& o) { return o.seq == c.seq && o.type == c.type; });
    if (it == opens_.end())
        return false;
    opens_.erase(it);
    if (body.size() > (size_t)proto::maxPayload) {
        send(c.type, proto::flagReply | proto::flagError, c.seq, error_body(proto::err::tooBig, "too big", {}), now);
        return false;
    }
    if (c.type == proto::msg::IMG_BEGIN && !(flags & proto::flagError) && c.seq == img_.seq) {
        LinkBody b;
        if (link_decode("IMG_BEGIN.reply", body.data(), body.size(), b.fields) == LinkFault::None) {
            img_.next = (uint32_t)b.u("offset");
            img_.active = img_.next < img_.total;
        }
    }
    send(c.type, flags, c.seq, body, now);
    return fd_ >= 0;
}

bool LinkSession::reply(const LinkCommand& c, const LinkWriter& body, int64_t now)
{
    return answer(c, proto::flagReply, body.data(), now);
}

bool LinkSession::fail(const LinkCommand& c, uint8_t code, const std::string& text, int64_t now,
                       const std::vector<std::string>& refused)
{
    return answer(c, proto::flagReply | proto::flagError, error_body(code, text, refused), now);
}

void LinkSession::bye(uint8_t reason, int64_t now)
{
    if (fd_ < 0 || closing_)
        return;
    namespace t = proto::tag::BYE;
    LinkWriter w;
    w.u8(t::reason, reason);
    if (reason == proto::val::BYE_REASON::version)
        w.u16(t::major, (uint16_t)proto::major).u16(t::minor, (uint16_t)proto::minor);
    opens_.clear();
    end_ask(LinkAnswer::Closed, 0);
    send(proto::msg::BYE, 0, next_seq(), w.data(), now);
    closing_ = true;
    closing_at_ = now;
}

}
