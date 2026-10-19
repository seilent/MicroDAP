#include "link/frame.h"
#include "link/proto.h"

#include <QCryptographicHash>
#include <QFile>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace fd;

namespace {

using Bytes = std::vector<uint8_t>;
using Lines = std::vector<std::pair<std::string, std::string>>;

struct Vec {
    std::string name, from, re, act;
    Bytes bytes;
    std::vector<size_t> reads;
    Lines dec;
};

QString specPath(const char *file)
{
    return QStringLiteral(MICRODAP_FLATDAP_LINK "/") + QLatin1String(file);
}

QByteArray readSpec(const char *file)
{
    QFile f(specPath(file));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return f.readAll();
}

std::vector<std::string> linesOf(const char *file)
{
    std::vector<std::string> out;
    for (const QByteArray &line : readSpec(file).split('\n'))
        if (!line.isEmpty())
            out.push_back(line.toStdString());
    return out;
}

std::string esc(const std::string &s)
{
    std::string o;
    for (char c : s) {
        if (c == '\\')
            o += "\\\\";
        else if (c == '\n')
            o += "\\n";
        else if (c == '\t')
            o += "\\t";
        else
            o += c;
    }
    return o;
}

std::string hexOf(const std::string &s)
{
    return QByteArray::fromStdString(s).toHex().toStdString();
}

std::string enumName(const char *kind, uint64_t v)
{
    for (const proto::EnumValue &e : proto::kEnums)
        if (uint64_t(e.value) == v && !std::strcmp(e.kind, kind))
            return e.name;
    return std::to_string(v);
}

void printFields(const std::vector<LinkField> &fields, const std::string &prefix, Lines &out)
{
    std::map<std::string, int> idx;
    for (const LinkField &f : fields) {
        if (!f.def) {
            out.push_back({prefix + "@unknown[" + std::to_string(idx["@unknown"]++) + "]", std::to_string(f.tag)});
            continue;
        }
        std::string p = prefix + f.def->field;
        if (f.def->list)
            p += "[" + std::to_string(idx[f.def->field]++) + "]";
        switch (f.def->kind) {
        case proto::Kind::Rec:
            printFields(f.rec, p + ".", out);
            break;
        case proto::Kind::Bool:
            out.push_back({p, f.u ? "true" : "false"});
            break;
        case proto::Kind::Str:
            out.push_back({p, esc(f.s)});
            break;
        case proto::Kind::Bytes:
            out.push_back({p, hexOf(f.s)});
            break;
        case proto::Kind::Enum:
            out.push_back({p, enumName(f.def->type, f.u)});
            break;
        case proto::Kind::I32:
        case proto::Kind::I64:
            out.push_back({p, std::to_string(f.i)});
            break;
        default:
            out.push_back({p, std::to_string(f.u)});
            break;
        }
    }
}

std::string typeWord(uint8_t type)
{
    if (const proto::Msg *m = link_msg(type))
        return m->name;
    return QStringLiteral("0x%1").arg(type, 2, 16, QLatin1Char('0')).toStdString();
}

void headerLines(const LinkFrame &f, const std::string &p, Lines &out)
{
    out.push_back({p + "len", std::to_string(f.len)});
    out.push_back({p + "type", typeWord(f.type)});
    out.push_back({p + "flags", QStringLiteral("0x%1").arg(f.flags, 2, 16, QLatin1Char('0')).toStdString()});
    out.push_back({p + "seq", std::to_string(f.seq)});
}

std::vector<Vec> loadVectors()
{
    std::vector<Vec> out;
    std::map<std::string, size_t> at;
    for (const std::string &line : linesOf("vectors.txt")) {
        size_t a = line.find(' ');
        size_t b = a == std::string::npos ? a : line.find(' ', a + 1);
        if (b == std::string::npos)
            continue;
        std::string kind = line.substr(0, a);
        std::string name = line.substr(a + 1, b - a - 1);
        std::string rest = line.substr(b + 1);
        if (kind == "vec") {
            at[name] = out.size();
            out.push_back(Vec{name, rest, {}, {}, {}, {}, {}});
            continue;
        }
        if (!at.count(name))
            continue;
        Vec &v = out[at[name]];
        if (kind == "hex") {
            QByteArray h = QByteArray::fromHex(QByteArray::fromStdString(rest));
            v.bytes.insert(v.bytes.end(), h.begin(), h.end());
        } else if (kind == "reads") {
            for (const QByteArray &r : QByteArray::fromStdString(rest).split(','))
                v.reads.push_back(size_t(r.toUInt()));
        } else if (kind == "re") {
            v.re = rest;
        } else if (kind == "act") {
            v.act = rest;
        } else if (kind == "dec") {
            size_t c = rest.find(' ');
            v.dec.push_back(c == std::string::npos ? std::make_pair(rest, std::string())
                                                   : std::make_pair(rest.substr(0, c), rest.substr(c + 1)));
        }
    }
    return out;
}

struct Result {
    Lines dec;
    std::string act = "accept";
    bool reencoded = true;
    bool leftover = false;
};

Result decodeVector(const Vec &v)
{
    Result res;
    LinkReader rd;
    bool closed = false;
    int fi = 0;
    size_t pos = 0;
    std::vector<size_t> reads = v.reads.empty() ? std::vector<size_t>{v.bytes.size()} : v.reads;
    for (size_t r : reads) {
        if (closed)
            break;
        rd.feed(v.bytes.data() + pos, r);
        pos += r;
        LinkFrame f;
        int k;
        while ((k = rd.next(f)) == 1) {
            std::string p = std::to_string(fi++) + ".";
            headerLines(f, p, res.dec);
            LinkBody body;
            LinkFault fault = link_decode_frame(f, body);
            printFields(body.fields, p + "body.", res.dec);
            if (fault != LinkFault::None) {
                res.dec.push_back({p + "close", link_fault_name(fault)});
                res.act = "close";
                closed = true;
                break;
            }
            bool reply = f.flags & (proto::flagReply | proto::flagError);
            if (!reply && !link_msg(f.type)) {
                res.act = f.flags & proto::flagWant ? "reply" : "skip";
            } else if (!reply && (f.flags & proto::flagWant) && body.unknown_enum()) {
                res.act = "reply";
            } else if (link_msg(f.type)) {
                Bytes again;
                link_encode(body.fields, again);
                res.reencoded = res.reencoded && again == f.body;
            }
        }
        if (k < 0) {
            headerLines(f, std::to_string(fi) + ".", res.dec);
            res.dec.push_back({std::to_string(fi) + ".close", link_fault_name(rd.fault())});
            res.act = "close";
            closed = true;
        }
    }
    if (!closed)
        res.leftover = rd.pending() != 0 || pos != v.bytes.size();
    return res;
}

}

class LinkProtoTest : public QObject
{
    Q_OBJECT

private slots:
    void lockMatchesSpec();
    void vectorsRoundTrip();
    void everyMessageHasAVector();
    void repliesMatchTheirRequest();
};

void LinkProtoTest::lockMatchesSpec()
{
    QList<QByteArray> lock = readSpec("proto.lock").trimmed().split(' ');
    QCOMPARE(lock.size(), 3);
    QCOMPARE(lock[0].toInt(), int(proto::major));
    QCOMPARE(lock[1].toInt(), int(proto::minor));
    QCOMPARE(lock[2], QCryptographicHash::hash(readSpec("proto.txt"), QCryptographicHash::Sha256).toHex());
}

void LinkProtoTest::vectorsRoundTrip()
{
    std::vector<Vec> vs = loadVectors();
    QVERIFY(vs.size() >= 60);
    for (const Vec &v : vs) {
        Result r = decodeVector(v);
        QVERIFY2(r.reencoded, v.name.c_str());
        QVERIFY2(!r.leftover, v.name.c_str());
        QVERIFY2(r.act == v.act, (v.name + ": act " + r.act + ", want " + v.act).c_str());
        for (size_t i = 0; i < std::max(r.dec.size(), v.dec.size()); ++i) {
            std::string got = i < r.dec.size() ? r.dec[i].first + " " + r.dec[i].second : "(none)";
            std::string want = i < v.dec.size() ? v.dec[i].first + " " + v.dec[i].second : "(none)";
            QVERIFY2(got == want, (v.name + ": got '" + got + "' want '" + want + "'").c_str());
        }
    }
}

void LinkProtoTest::everyMessageHasAVector()
{
    std::map<std::string, bool> covered;
    for (const Vec &v : loadVectors())
        if (v.bytes.size() >= 3)
            if (const proto::Msg *m = link_msg(v.bytes[2]))
                covered[m->name] = true;
    for (const proto::Msg &m : proto::kMsgs)
        QVERIFY2(covered[m.name], m.name);
}

void LinkProtoTest::repliesMatchTheirRequest()
{
    std::vector<Vec> vs = loadVectors();
    for (const Vec &v : vs) {
        if (v.re.empty())
            continue;
        const Vec *q = nullptr;
        for (const Vec &o : vs)
            if (o.name == v.re)
                q = &o;
        QVERIFY2(q && q->bytes.size() >= 6 && v.bytes.size() >= 6, v.name.c_str());
        QCOMPARE(v.bytes[2], q->bytes[2]);
        QCOMPARE(v.bytes[4], q->bytes[4]);
        QCOMPARE(v.bytes[5], q->bytes[5]);
        QVERIFY2(v.bytes[3] & proto::flagReply, v.name.c_str());
    }
}

QTEST_GUILESS_MAIN(LinkProtoTest)
#include "tst_linkproto.moc"
