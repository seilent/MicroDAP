#include "Cue.h"

#include "Scanner.h"

#include <QFile>
#include <QRegularExpression>
#include <QStringView>

#include <utility>

namespace microdap {

namespace {

constexpr qsizetype maxEntries = 256;
constexpr quint64 maxSeconds = 4000000;
constexpr int cueBlockType = 5;
constexpr qint64 cueBlockCap = 1 << 20;
constexpr int id3TagCap = 4;

struct Raw
{
    CueTrack track;
    bool audio = true;
    bool indexed = false;
};

QStringView word(QStringView &s)
{
    while (!s.isEmpty() && (s.front() == QLatin1Char(' ') || s.front() == QLatin1Char('\t')))
        s = s.mid(1);
    if (s.isEmpty())
        return {};
    if (s.front() == QLatin1Char('"')) {
        const qsizetype end = s.indexOf(QLatin1Char('"'), 1);
        const QStringView w = end < 0 ? s.mid(1) : s.mid(1, end - 1);
        s = end < 0 ? QStringView() : s.mid(end + 1);
        return w;
    }
    qsizetype end = 0;
    while (end < s.size() && s.at(end) != QLatin1Char(' ') && s.at(end) != QLatin1Char('\t'))
        ++end;
    const QStringView w = s.left(end);
    s = s.mid(end);
    return w;
}

QString value(QStringView s)
{
    const QStringView t = s.trimmed();
    if (!t.isEmpty() && t.front() == QLatin1Char('"')) {
        const qsizetype end = t.indexOf(QLatin1Char('"'), 1);
        return (end < 0 ? t.mid(1) : t.mid(1, end - 1)).toString();
    }
    return t.toString();
}

bool same(QStringView a, const char *b)
{
    return a.compare(QLatin1StringView(b), Qt::CaseInsensitive) == 0;
}

bool digits(QStringView s, qsizetype most, int &v)
{
    if (s.isEmpty() || s.size() > most)
        return false;
    v = 0;
    for (const QChar c : s) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
        v = v * 10 + (c.unicode() - '0');
    }
    return true;
}

bool stamp(QStringView s, int &frames)
{
    const qsizetype a = s.indexOf(QLatin1Char(':'));
    const qsizetype b = a < 0 ? -1 : s.indexOf(QLatin1Char(':'), a + 1);
    int m = 0;
    int sec = 0;
    int f = 0;
    if (b < 0 || !digits(s.left(a), 4, m) || !digits(s.mid(a + 1, b - a - 1), 2, sec) || !digits(s.mid(b + 1), 2, f)
        || sec >= 60 || f >= cueFramesPerSecond)
        return false;
    frames = (m * 60 + sec) * cueFramesPerSecond + f;
    return true;
}

quint64 be64(const char *p)
{
    quint64 v = 0;
    for (int i = 0; i < 8; ++i)
        v = v << 8 | quint8(p[i]);
    return v;
}

bool settle(QList<Raw> &raw, CueSheet &out)
{
    QList<Raw> kept;
    bool seen[100] = {};
    for (Raw &r : raw) {
        if (!r.indexed || r.track.number < 1 || r.track.number > 99 || seen[r.track.number]
            || (!kept.isEmpty() && r.track.startFrame <= kept.constLast().track.startFrame))
            continue;
        seen[r.track.number] = true;
        kept.append(std::move(r));
    }
    out.tracks.clear();
    for (qsizetype i = 0; i < kept.size(); ++i) {
        kept[i].track.endFrame = i + 1 < kept.size() ? kept.at(i + 1).track.startFrame : 0;
        if (kept.at(i).audio)
            out.tracks.append(std::move(kept[i].track));
    }
    return !out.tracks.isEmpty();
}

}

std::optional<double> leadingDecimal(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^[+-]?([0-9]+([.,][0-9]*)?|[.,][0-9]+)"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double v = match.captured(0).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? std::optional<double>(v) : std::nullopt;
}

std::optional<double> gainOf(std::optional<double> db)
{
    if (!db || *db < -gainLimitDb || *db > gainLimitDb)
        return std::nullopt;
    return db;
}

std::optional<double> peakOf(std::optional<double> peak)
{
    return peak && *peak > 0 ? peak : std::nullopt;
}

int yearOf(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("[0-9]{4}(?![0-9])"));
    const QRegularExpressionMatch match = pattern.match(text);
    return match.hasMatch() ? match.captured(0).toInt() : 0;
}

qint64 cueFrameMs(int frames)
{
    return (qint64(frames) * 1000 + 37) / cueFramesPerSecond;
}

GstClockTime cueStartNs(int frames)
{
    return gst_util_uint64_scale_ceil(guint64(frames), GST_SECOND, cueFramesPerSecond);
}

GstClockTime cueStopNs(int frames)
{
    return gst_util_uint64_scale(guint64(frames), GST_SECOND, cueFramesPerSecond);
}

bool parseCue(const QByteArray &bytes, CueSheet &out)
{
    out = CueSheet{};
    const qsizetype bom = bytes.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    if (bytes.size() - bom > cueCap)
        return false;
    const QString text = Scanner::decodePlaylist(bytes, false);
    QList<Raw> raw;
    const QList<QStringView> lines = QStringView(text).split(QRegularExpression(QStringLiteral("[\r\n]")));
    for (QStringView rest : lines) {
        const QStringView cmd = word(rest);
        Raw *cur = raw.isEmpty() ? nullptr : &raw.last();
        if (same(cmd, "FILE")) {
            if (out.files++ == 0)
                out.file = word(rest).toString();
        } else if (same(cmd, "TRACK")) {
            if (raw.size() >= maxEntries)
                break;
            int nn = 0;
            Raw r;
            r.track.number = digits(word(rest), 3, nn) ? nn : -1;
            r.audio = same(word(rest), "AUDIO");
            raw.append(std::move(r));
        } else if (same(cmd, "INDEX")) {
            int nn = 0;
            int frames = 0;
            if (cur && !cur->indexed && digits(word(rest), 3, nn) && nn == 1 && stamp(word(rest), frames)) {
                cur->track.startFrame = frames;
                cur->indexed = true;
            }
        } else if (same(cmd, "TITLE")) {
            (cur ? cur->track.title : out.title) = value(rest);
        } else if (same(cmd, "PERFORMER")) {
            (cur ? cur->track.performer : out.performer) = value(rest);
        } else if (same(cmd, "REM")) {
            const QStringView key = word(rest);
            const QString v = value(rest);
            if (cur) {
                if (same(key, "REPLAYGAIN_TRACK_GAIN"))
                    cur->track.gain = gainOf(leadingDecimal(v));
                else if (same(key, "REPLAYGAIN_TRACK_PEAK"))
                    cur->track.peak = peakOf(leadingDecimal(v));
            } else if (same(key, "GENRE")) {
                out.genre = v;
            } else if (same(key, "DATE")) {
                out.year = yearOf(v);
            } else if (same(key, "DISCNUMBER")) {
                int d = 0;
                out.disc = digits(v, 3, d) ? d : 0;
            } else if (same(key, "REPLAYGAIN_ALBUM_GAIN")) {
                out.albumGain = gainOf(leadingDecimal(v));
            } else if (same(key, "REPLAYGAIN_ALBUM_PEAK")) {
                out.albumPeak = peakOf(leadingDecimal(v));
            }
        }
    }
    return settle(raw, out);
}

bool parseCueBlock(const QByteArray &block, int rate, CueSheet &out)
{
    out = CueSheet{};
    out.files = 1;
    if (rate <= 0 || block.size() < 396)
        return false;
    const char *p = block.constData();
    const qsizetype n = block.size();
    qsizetype q = 395;
    const int count = quint8(p[q++]);
    QList<Raw> raw;
    for (int i = 0; i < count && q + 36 <= n; ++i) {
        const quint64 offset = be64(p + q);
        Raw r;
        r.track.number = quint8(p[q + 8]);
        r.audio = !(quint8(p[q + 21]) & 0x80);
        const int points = quint8(p[q + 35]);
        q += 36;
        for (int k = 0; k < points && q + 12 <= n; ++k, q += 12) {
            const quint64 at = offset + be64(p + q);
            if (quint8(p[q + 8]) != 1 || r.indexed || at < offset || at / quint64(rate) >= maxSeconds)
                continue;
            r.track.startFrame = int((at * cueFramesPerSecond + quint64(rate) / 2) / quint64(rate));
            r.indexed = true;
        }
        raw.append(std::move(r));
    }
    return settle(raw, out);
}

QByteArray flacCueBlock(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    for (int tags = 0; tags < id3TagCap; ++tags) {
        const qint64 at = file.pos();
        const QByteArray id3 = file.read(10);
        if (id3.size() != 10 || !id3.startsWith("ID3") || quint8(id3.at(3)) < 2 || quint8(id3.at(3)) > 4) {
            file.seek(at);
            break;
        }
        const qint64 size = qint64(quint8(id3.at(6)) & 0x7f) << 21 | qint64(quint8(id3.at(7)) & 0x7f) << 14
                            | qint64(quint8(id3.at(8)) & 0x7f) << 7 | qint64(quint8(id3.at(9)) & 0x7f);
        if (!file.seek(at + 10 + size + ((quint8(id3.at(5)) & 0x10) ? 10 : 0)))
            return {};
    }
    if (file.read(4) != "fLaC")
        return {};
    for (;;) {
        const QByteArray header = file.read(4);
        if (header.size() != 4)
            return {};
        const bool last = quint8(header.at(0)) & 0x80;
        const int type = quint8(header.at(0)) & 0x7f;
        const qint64 length = qint64(quint8(header.at(1))) << 16 | qint64(quint8(header.at(2))) << 8
                              | qint64(quint8(header.at(3)));
        if (type == cueBlockType)
            return length <= cueBlockCap ? file.read(length) : QByteArray();
        if (last || !file.seek(file.pos() + length))
            return {};
    }
}

}
