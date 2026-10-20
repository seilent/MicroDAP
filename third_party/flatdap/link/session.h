#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>

#include "frame.h"

namespace fd {

constexpr size_t kLinkQueueBytes = 65536;
constexpr size_t kLinkReadBytes = 65536;

struct LinkEqSlot {
    std::string key, label;
};

struct LinkHello {
    std::string board, version;
    uint16_t screen_w = 0, screen_h = 0, backdrop_w = 0, backdrop_h = 0;
    uint32_t idle_ms = (uint32_t)proto::keepaliveIdleMs, busy_ms = (uint32_t)proto::keepaliveBusyMs;
    uint64_t features = 0;
    uint32_t lib_rev = 0, settings_rev = 0, snap_rev = 0;
    std::vector<LinkEqSlot> eq_slots;
    std::vector<uint8_t> band_types;
};

struct LinkCommand {
    uint8_t type = 0, flags = 0;
    uint16_t seq = 0;
    LinkBody body;
    bool deferred = false;

    bool want() const { return flags & proto::flagWant; }
};

struct LinkAnswer {
    enum Kind : uint8_t { Reply, Error, Timeout, Closed };
    uint8_t type = 0;
    uint16_t seq = 0;
    Kind kind = Reply;
    uint8_t code = 0;
    LinkBody body;
};

class LinkHandler {
public:
    virtual ~LinkHandler() {}
    virtual void hello(LinkHello&, uint64_t) {}
    virtual void command(LinkCommand& c, int64_t now) = 0;
};

class LinkSession {
public:
    LinkSession(int fd, const LinkHello& hello, LinkHandler* handler, int64_t now);
    ~LinkSession();
    LinkSession(const LinkSession&) = delete;
    LinkSession& operator=(const LinkSession&) = delete;

    int tick(int64_t now);
    bool want_write() const { return !out_.empty(); }
    bool reading() const { return !read_held_; }
    void hold_read(bool held) { read_held_ = held; }
    bool open() const { return open_ && !closing_ && fd_ >= 0; }
    bool closed() const { return fd_ < 0; }
    const std::string& why() const { return why_; }
    int fd() const { return fd_; }
    size_t queued() const { return queued_; }
    uint32_t interval(int64_t now) const;

    bool push(uint8_t type, const LinkWriter& body, int64_t now);
    uint16_t ask(uint8_t type, const LinkWriter& body, int64_t now);
    bool asking() const { return ask_.type != 0; }
    bool app_has(uint8_t type) const;
    bool app_bit(int bit) const;
    bool can_ask(uint8_t type) const;
    bool take_answer(LinkAnswer& out);
    bool reply(const LinkCommand& c, const LinkWriter& body, int64_t now);
    bool fail(const LinkCommand& c, uint8_t code, const std::string& text, int64_t now,
              const std::vector<std::string>& refused = {});
    void bye(uint8_t reason, int64_t now);
    void abort_image(uint8_t code, int64_t now);
    void set_busy(bool busy, int64_t now);
    void close(const std::string& why);
    void on_close(std::function<void(int)> f) { closer_ = std::move(f); }

private:
    struct Out {
        uint8_t type;
        bool reply;
        std::vector<uint8_t> bytes;
        size_t sent = 0;
    };
    struct Open {
        uint8_t type;
        uint16_t seq;
        int64_t deadline;
    };
    struct Image {
        bool active = false, aborted = false;
        std::string name;
        uint32_t total = 0, next = 0;
        uint16_t seq = 0;
    };

    void read(int64_t now);
    void write(int64_t now);
    void timers(int64_t now);
    void frame(LinkFrame& f, int64_t now);
    void hello(LinkCommand& c, int64_t now);
    void dispatch(LinkCommand& c, int64_t now);
    bool image(LinkCommand& c, int64_t now);
    void send(uint8_t type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& body, int64_t now);
    bool answer(const LinkCommand& c, uint8_t flags, const std::vector<uint8_t>& body, int64_t now);
    uint16_t next_seq();
    bool busy(int64_t now) const;
    bool has(uint8_t type) const;
    void mode(int64_t now);
    void abort_push(uint8_t code, int64_t now);
    void end_ask(LinkAnswer::Kind k, uint8_t code, LinkBody body = {});

    int fd_;
    LinkHello hello_;
    LinkHandler* handler_;
    LinkReader reader_;
    std::deque<Out> out_;
    std::vector<Open> opens_;
    Open ask_ = {0, 0, 0};
    std::deque<LinkAnswer> answers_;
    uint64_t app_features_ = 0;
    Image img_;
    size_t queued_ = 0;
    uint16_t seq_ = 0;
    bool open_ = false, closing_ = false, busy_ = false, fast_ = false, read_held_ = false;
    int64_t start_, last_rx_, last_ping_ = -1, last_tx_, anchor_, closing_at_ = 0, busy_since_ = -1;
    std::string why_;
    std::function<void(int)> closer_;
};

}
