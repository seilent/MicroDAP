#include "frame.h"

#include <cstring>

namespace fd {

namespace {

uint64_t le(const uint8_t* p, size_t n)
{
    uint64_t v = 0;
    for (size_t k = 0; k < n; k++)
        v |= (uint64_t)p[k] << (8 * k);
    return v;
}

void put_le(std::vector<uint8_t>& out, uint64_t v, size_t n)
{
    for (size_t k = 0; k < n; k++)
        out.push_back(uint8_t(v >> (8 * k)));
}

size_t width(proto::Kind k)
{
    switch (k) {
    case proto::Kind::U8:
    case proto::Kind::Bool:
    case proto::Kind::Enum:
        return 1;
    case proto::Kind::U16:
        return 2;
    case proto::Kind::U32:
    case proto::Kind::I32:
        return 4;
    case proto::Kind::U64:
    case proto::Kind::I64:
        return 8;
    default:
        return 0;
    }
}

const proto::Tag* find_tag(std::string_view scope, uint8_t num)
{
    for (const proto::Tag& t : proto::kTags)
        if (t.num == num && scope == t.scope)
            return &t;
    return nullptr;
}

bool enum_known(const char* kind, uint64_t v)
{
    for (const proto::EnumValue& e : proto::kEnums)
        if ((uint64_t)e.value == v && !strcmp(e.kind, kind))
            return true;
    return false;
}

bool any_unknown(const std::vector<LinkField>& fields)
{
    for (const LinkField& f : fields)
        if (!f.known || any_unknown(f.rec))
            return true;
    return false;
}

}

const char* link_fault_name(LinkFault f)
{
    switch (f) {
    case LinkFault::None:
        return "none";
    case LinkFault::Oversize:
        return "oversize";
    case LinkFault::ReservedFlags:
        return "reservedFlags";
    case LinkFault::TlvOverrun:
        return "tlvOverrun";
    case LinkFault::Width:
        return "width";
    case LinkFault::Bool:
        return "bool";
    case LinkFault::Duplicate:
        return "duplicate";
    case LinkFault::ErrorWithoutReply:
        return "errorWithoutReply";
    case LinkFault::BadMagic:
        return "badMagic";
    }
    return "?";
}

const LinkField* LinkBody::get(std::string_view field) const
{
    for (const LinkField& f : fields)
        if (f.def && field == f.def->field)
            return &f;
    return nullptr;
}

std::vector<const LinkField*> LinkBody::all(std::string_view field) const
{
    std::vector<const LinkField*> out;
    for (const LinkField& f : fields)
        if (f.def && field == f.def->field)
            out.push_back(&f);
    return out;
}

uint64_t LinkBody::u(std::string_view field, uint64_t def) const
{
    const LinkField* f = get(field);
    return f ? f->u : def;
}

int64_t LinkBody::i(std::string_view field, int64_t def) const
{
    const LinkField* f = get(field);
    return f ? f->i : def;
}

std::string LinkBody::str(std::string_view field) const
{
    const LinkField* f = get(field);
    return f ? f->s : std::string();
}

bool LinkBody::unknown_enum() const { return any_unknown(fields); }

LinkWriter& LinkWriter::u16(uint8_t tag, uint16_t v)
{
    uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
    return put(tag, b, 2);
}

LinkWriter& LinkWriter::u32(uint8_t tag, uint32_t v)
{
    uint8_t b[4];
    for (int k = 0; k < 4; k++)
        b[k] = uint8_t(v >> (8 * k));
    return put(tag, b, 4);
}

LinkWriter& LinkWriter::u64(uint8_t tag, uint64_t v)
{
    uint8_t b[8];
    for (int k = 0; k < 8; k++)
        b[k] = uint8_t(v >> (8 * k));
    return put(tag, b, 8);
}

LinkWriter& LinkWriter::put(uint8_t tag, const void* p, size_t n)
{
    data_.push_back(tag);
    put_le(data_, n, 2);
    const uint8_t* b = (const uint8_t*)p;
    data_.insert(data_.end(), b, b + n);
    return *this;
}

void LinkReader::feed(const uint8_t* p, size_t n)
{
    if (at_ > 0 && at_ * 2 >= buf_.size()) {
        buf_.erase(buf_.begin(), buf_.begin() + (long)at_);
        at_ = 0;
    }
    buf_.insert(buf_.end(), p, p + n);
}

int LinkReader::next(LinkFrame& f)
{
    if (fault_ != LinkFault::None)
        return -1;
    if (pending() < (size_t)proto::headerBytes)
        return 0;
    const uint8_t* h = buf_.data() + at_;
    f.len = (uint16_t)le(h, 2);
    f.type = h[2];
    f.flags = h[3];
    f.seq = (uint16_t)le(h + 4, 2);
    if (f.len > proto::maxPayload) {
        fault_ = LinkFault::Oversize;
        return -1;
    }
    if (f.flags & proto::flagReserved) {
        fault_ = LinkFault::ReservedFlags;
        return -1;
    }
    if (pending() < (size_t)proto::headerBytes + f.len)
        return 0;
    f.body.assign(h + proto::headerBytes, h + proto::headerBytes + f.len);
    at_ += (size_t)proto::headerBytes + f.len;
    if (at_ == buf_.size()) {
        buf_.clear();
        at_ = 0;
    }
    return 1;
}

const char* link_err_text(uint8_t code)
{
    namespace e = proto::err;
    switch (code) {
    case e::unknown:
        return "unknown";
    case e::badValue:
        return "bad value";
    case e::refused:
        return "refused";
    case e::tooBig:
        return "too big";
    case e::crc:
        return "crc";
    case e::staleRev:
        return "stale rev";
    case e::noLibrary:
        return "no library";
    case e::noSpace:
        return "no space";
    case e::notFound:
        return "not found";
    case e::readOnly:
        return "read only";
    case e::exists:
        return "exists";
    default:
        return "busy";
    }
}

const proto::Msg* link_msg(uint8_t type)
{
    for (const proto::Msg& m : proto::kMsgs)
        if (m.id == type)
            return &m;
    return nullptr;
}

std::string link_scope(uint8_t type, uint8_t flags)
{
    if (flags & proto::flagError)
        return "error";
    const proto::Msg* m = link_msg(type);
    if (!m)
        return "";
    return flags & proto::flagReply ? std::string(m->name) + ".reply" : m->name;
}

LinkFault link_decode(std::string_view scope, const uint8_t* p, size_t n, std::vector<LinkField>& out)
{
    size_t at = 0;
    while (at < n) {
        if (n - at < (size_t)proto::tlvHeaderBytes)
            return LinkFault::TlvOverrun;
        LinkField f;
        f.tag = p[at];
        size_t len = (size_t)le(p + at + 1, 2);
        at += (size_t)proto::tlvHeaderBytes;
        if (n - at < len)
            return LinkFault::TlvOverrun;
        const uint8_t* v = p + at;
        at += len;
        f.def = find_tag(scope, f.tag);
        if (!f.def) {
            f.s.assign((const char*)v, len);
            out.push_back(std::move(f));
            continue;
        }
        if (!f.def->list)
            for (const LinkField& o : out)
                if (o.def == f.def)
                    return LinkFault::Duplicate;
        proto::Kind k = f.def->kind;
        size_t w = width(k);
        if (w && len != w)
            return LinkFault::Width;
        switch (k) {
        case proto::Kind::Bool:
            if (v[0] > 1)
                return LinkFault::Bool;
            f.u = v[0];
            break;
        case proto::Kind::Enum:
            f.u = v[0];
            f.known = enum_known(f.def->type, f.u);
            break;
        case proto::Kind::I32:
            f.i = (int32_t)(uint32_t)le(v, 4);
            break;
        case proto::Kind::I64:
            f.i = (int64_t)le(v, 8);
            break;
        case proto::Kind::Str:
        case proto::Kind::Bytes:
            f.s.assign((const char*)v, len);
            break;
        case proto::Kind::Rec: {
            LinkFault r = link_decode(f.def->type, v, len, f.rec);
            if (r != LinkFault::None)
                return r;
            break;
        }
        default:
            f.u = le(v, w);
            break;
        }
        out.push_back(std::move(f));
    }
    return LinkFault::None;
}

LinkFault link_decode_frame(const LinkFrame& f, LinkBody& out)
{
    out.fields.clear();
    if ((f.flags & proto::flagError) && !(f.flags & proto::flagReply))
        return LinkFault::ErrorWithoutReply;
    bool reply = f.flags & (proto::flagReply | proto::flagError);
    if (!link_msg(f.type) && !reply)
        return LinkFault::None;
    LinkFault r = link_decode(link_scope(f.type, f.flags), f.body.data(), f.body.size(), out.fields);
    if (r != LinkFault::None)
        return r;
    if (f.type == proto::msg::HELLO && !reply && out.u("magic", 0) != (uint64_t)proto::magic)
        return LinkFault::BadMagic;
    return LinkFault::None;
}

void link_encode(const std::vector<LinkField>& fields, std::vector<uint8_t>& out)
{
    for (const LinkField& f : fields) {
        std::vector<uint8_t> v;
        if (!f.def) {
            v.assign(f.s.begin(), f.s.end());
        } else {
            proto::Kind k = f.def->kind;
            if (k == proto::Kind::Rec)
                link_encode(f.rec, v);
            else if (k == proto::Kind::Str || k == proto::Kind::Bytes)
                v.assign(f.s.begin(), f.s.end());
            else if (k == proto::Kind::I32 || k == proto::Kind::I64)
                put_le(v, (uint64_t)f.i, width(k));
            else
                put_le(v, f.u, width(k));
        }
        out.push_back(f.tag);
        put_le(out, v.size(), 2);
        out.insert(out.end(), v.begin(), v.end());
    }
}

std::vector<uint8_t> link_frame(uint8_t type, uint8_t flags, uint16_t seq, const std::vector<uint8_t>& body)
{
    std::vector<uint8_t> out;
    out.reserve((size_t)proto::headerBytes + body.size());
    put_le(out, body.size(), 2);
    out.push_back(type);
    out.push_back(flags);
    put_le(out, seq, 2);
    out.insert(out.end(), body.begin(), body.end());
    return out;
}

}
