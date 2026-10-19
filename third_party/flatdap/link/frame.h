#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "proto.h"

namespace fd {

enum class LinkFault { None, Oversize, ReservedFlags, TlvOverrun, Width, Bool, Duplicate, ErrorWithoutReply, BadMagic };

const char* link_fault_name(LinkFault f);
const char* link_err_text(uint8_t code);

struct LinkField {
    uint8_t tag = 0;
    const proto::Tag* def = nullptr;
    uint64_t u = 0;
    int64_t i = 0;
    std::string s;
    std::vector<LinkField> rec;
    bool known = true;
};

struct LinkBody {
    std::vector<LinkField> fields;

    const LinkField* get(std::string_view field) const;
    std::vector<const LinkField*> all(std::string_view field) const;
    uint64_t u(std::string_view field, uint64_t def = 0) const;
    int64_t i(std::string_view field, int64_t def = 0) const;
    std::string str(std::string_view field) const;
    bool has(std::string_view field) const { return get(field) != nullptr; }
    bool unknown_enum() const;
};

struct LinkFrame {
    uint16_t len = 0;
    uint8_t type = 0, flags = 0;
    uint16_t seq = 0;
    std::vector<uint8_t> body;
};

class LinkWriter {
public:
    LinkWriter& u8(uint8_t tag, uint8_t v) { return put(tag, &v, 1); }
    LinkWriter& u16(uint8_t tag, uint16_t v);
    LinkWriter& u32(uint8_t tag, uint32_t v);
    LinkWriter& u64(uint8_t tag, uint64_t v);
    LinkWriter& i32(uint8_t tag, int32_t v) { return u32(tag, (uint32_t)v); }
    LinkWriter& i64(uint8_t tag, int64_t v) { return u64(tag, (uint64_t)v); }
    LinkWriter& flag(uint8_t tag, bool v) { return u8(tag, v ? 1 : 0); }
    LinkWriter& str(uint8_t tag, std::string_view v) { return put(tag, v.data(), v.size()); }
    LinkWriter& bytes(uint8_t tag, const void* p, size_t n) { return put(tag, p, n); }
    LinkWriter& rec(uint8_t tag, const LinkWriter& w) { return put(tag, w.data_.data(), w.data_.size()); }

    const std::vector<uint8_t>& data() const { return data_; }
    size_t size() const { return data_.size(); }

private:
    LinkWriter& put(uint8_t tag, const void* p, size_t n);

    std::vector<uint8_t> data_;
};

class LinkReader {
public:
    void feed(const uint8_t* p, size_t n);
    int next(LinkFrame& f);
    LinkFault fault() const { return fault_; }
    size_t pending() const { return buf_.size() - at_; }

private:
    std::vector<uint8_t> buf_;
    size_t at_ = 0;
    LinkFault fault_ = LinkFault::None;
};

const proto::Msg* link_msg(uint8_t type);
std::string link_scope(uint8_t type, uint8_t flags);
LinkFault link_decode(std::string_view scope, const uint8_t* p, size_t n, std::vector<LinkField>& out);
LinkFault link_decode_frame(const LinkFrame& f, LinkBody& out);
void link_encode(const std::vector<LinkField>& fields, std::vector<uint8_t>& out);
std::vector<uint8_t> link_frame(uint8_t type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& body);

}
