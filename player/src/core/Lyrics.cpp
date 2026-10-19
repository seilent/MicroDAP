#include "Lyrics.h"

#include "Scanner.h"

#include <taglib/apefile.h>
#include <taglib/apeitem.h>
#include <taglib/apetag.h>
#include <taglib/dsdifffile.h>
#include <taglib/dsffile.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4file.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>
#include <taglib/unsynchronizedlyricsframe.h>
#include <taglib/wavfile.h>
#include <taglib/xiphcomment.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>
#include <utility>

namespace microdap {

namespace {

bool digits(QStringView s, qsizetype lo, qsizetype hi, qint64 &value)
{
    if (s.size() < lo || s.size() > hi)
        return false;
    value = 0;
    for (const QChar c : s) {
        if (c < QLatin1Char('0') || c > QLatin1Char('9'))
            return false;
        value = value * 10 + (c.unicode() - '0');
    }
    return true;
}

bool stamp(QStringView tag, qint64 &ms)
{
    const qsizetype colon = tag.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return false;
    const QStringView rest = tag.mid(colon + 1);
    qsizetype dot = 0;
    while (dot < rest.size() && rest[dot] != QLatin1Char('.') && rest[dot] != QLatin1Char(':'))
        ++dot;
    qint64 minutes = 0;
    qint64 seconds = 0;
    qint64 fraction = 0;
    if (!digits(tag.left(colon), 1, 4, minutes) || !digits(rest.left(dot), 1, 2, seconds) || seconds >= 60)
        return false;
    if (dot < rest.size()) {
        const QStringView part = rest.mid(dot + 1);
        if (!digits(part, 1, 3, fraction))
            return false;
        for (qsizetype i = part.size(); i < 3; ++i)
            fraction *= 10;
    }
    ms = (minutes * 60 + seconds) * 1000 + fraction;
    return true;
}

bool offset(QStringView tag, qint64 &ms)
{
    if (!tag.startsWith(QLatin1StringView("offset:"), Qt::CaseInsensitive))
        return false;
    QStringView value = tag.mid(7);
    while (!value.isEmpty() && value.front() == QLatin1Char(' '))
        value = value.mid(1);
    const bool negative = !value.isEmpty() && value.front() == QLatin1Char('-');
    if (!value.isEmpty() && (value.front() == QLatin1Char('-') || value.front() == QLatin1Char('+')))
        value = value.mid(1);
    qint64 n = 0;
    if (!digits(value, 1, 7, n))
        return false;
    ms = std::clamp(negative ? -n : n, -Lyrics::maxOffsetMs, Lyrics::maxOffsetMs);
    return true;
}

bool idTag(QStringView tag)
{
    const qsizetype colon = tag.indexOf(QLatin1Char(':'));
    if (colon <= 0)
        return false;
    for (const QChar c : tag.left(colon)) {
        const char16_t u = c.unicode();
        if (!((u >= 'a' && u <= 'z') || (u >= 'A' && u <= 'Z')))
            return false;
    }
    return true;
}

class Values
{
public:
    void add(const TagLib::String &value)
    {
        if (m_full)
            return;
        const QByteArray bytes = QByteArray(value.toCString(true)).trimmed();
        if (bytes.isEmpty() || bytes.size() > Lyrics::cap)
            return;
        if (m_total + bytes.size() > Lyrics::tagTotal) {
            m_full = true;
            return;
        }
        m_total += bytes.size();
        m_values.append(QString::fromUtf8(bytes));
    }

    void add(const TagLib::StringList &values)
    {
        for (const TagLib::String &value : values)
            add(value);
    }

    void id3(TagLib::ID3v2::Tag *tag)
    {
        if (!tag)
            return;
        for (TagLib::ID3v2::Frame *frame : tag->frameList("USLT")) {
            if (const auto *lyrics = dynamic_cast<TagLib::ID3v2::UnsynchronizedLyricsFrame *>(frame))
                add(lyrics->text());
        }
    }

    void xiph(TagLib::Ogg::XiphComment *comment)
    {
        if (!comment)
            return;
        const TagLib::Ogg::FieldListMap &fields = comment->fieldListMap();
        for (const char *key : {"LYRICS", "UNSYNCEDLYRICS"}) {
            if (const auto it = fields.find(key); it != fields.end())
                add(it->second);
        }
    }

    void ape(TagLib::APE::Tag *tag)
    {
        if (!tag)
            return;
        const TagLib::APE::ItemListMap &items = tag->itemListMap();
        if (const auto it = items.find("LYRICS"); it != items.end() && it->second.type() == TagLib::APE::Item::Text)
            add(it->second.values());
    }

    void mp4(TagLib::MP4::Tag *tag)
    {
        if (tag && tag->contains("\251lyr"))
            add(tag->item("\251lyr").toStringList());
    }

    QStringList take() { return std::move(m_values); }

private:
    QStringList m_values;
    qsizetype m_total = 0;
    bool m_full = false;
};

}

Lyrics::Lyrics(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

Lyrics::~Lyrics()
{
    m_pool.clear();
    m_pool.waitForDone();
}

LyricsText Lyrics::parse(const QString &text)
{
    LyricsText out;
    QStringList plain;
    qint64 shift = 0;
    qsizetype kept = 0;
    QList<qint64> stamps;
    const qsizetype n = text.size();
    qsizetype a = 0;
    while (a < n) {
        qsizetype b = a;
        while (b < n && text[b] != QLatin1Char('\r') && text[b] != QLatin1Char('\n'))
            ++b;
        const QStringView line = QStringView(text).mid(a, b - a);
        a = b + 1 < n && text[b] == QLatin1Char('\r') && text[b + 1] == QLatin1Char('\n') ? b + 2 : b + 1;

        stamps.clear();
        qsizetype i = 0;
        while (i < line.size() && (line[i] == QLatin1Char(' ') || line[i] == QLatin1Char('\t')))
            ++i;
        int tags = 0;
        int ids = 0;
        while (i < line.size() && line[i] == QLatin1Char('[')) {
            const qsizetype end = line.indexOf(QLatin1Char(']'), i);
            if (end < 0)
                break;
            const QStringView tag = line.mid(i + 1, end - i - 1);
            qint64 value = 0;
            if (stamp(tag, value))
                stamps.append(value);
            else if (offset(tag, value))
                shift = value;
            i = end + 1;
            ++tags;
            ids += idTag(tag) ? 1 : 0;
        }
        if (stamps.isEmpty()) {
            const QStringView rest = line.mid(i).trimmed();
            if ((!rest.isEmpty() || ids < tags || tags == 0) && plain.size() < maxLines)
                plain.append((tags ? line.trimmed() : rest).toString());
            continue;
        }
        const QString words = line.mid(i).trimmed().toString();
        const qsizetype bytes = words.toUtf8().size();
        for (const qint64 value : std::as_const(stamps)) {
            if (out.lines.size() >= maxLines || kept + bytes > cap)
                break;
            kept += bytes;
            out.lines.append({value, words});
        }
    }
    for (LyricLine &line : out.lines)
        line.ms = std::clamp(line.ms - shift, qint64(0), maxStampMs);
    std::stable_sort(out.lines.begin(), out.lines.end(),
                     [](const LyricLine &x, const LyricLine &y) { return x.ms < y.ms; });
    if (out.lines.isEmpty()) {
        while (!plain.isEmpty() && plain.constLast().isEmpty())
            plain.removeLast();
        while (!plain.isEmpty() && plain.constFirst().isEmpty())
            plain.removeFirst();
        out.plain = plain;
    }
    return out;
}

LyricsText Lyrics::parseBytes(const QByteArray &bytes)
{
    if (bytes.size() > cap || bytes.startsWith("\xFF\xFE") || bytes.startsWith("\xFE\xFF") || bytes.contains('\0'))
        return {};
    return parse(Scanner::decodePlaylist(bytes, false));
}

QString Lyrics::besidePath(const QString &audio)
{
    const QFileInfo info(audio);
    const QString name = info.completeBaseName() + QStringLiteral(".lrc");
    const QString exact = info.absolutePath() + QLatin1Char('/') + name;
    if (QFileInfo(exact).isFile())
        return exact;
    const QStringList entries = QDir(info.absolutePath()).entryList(QDir::Files | QDir::Hidden);
    for (const QString &entry : entries) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0)
            return info.absolutePath() + QLatin1Char('/') + entry;
    }
    return {};
}

LyricsText Lyrics::readFile(const QString &path, LyricsText::Source source)
{
    QFile file(path);
    if (path.isEmpty() || !file.open(QIODevice::ReadOnly) || file.size() > cap)
        return {};
    const QByteArray bytes = file.read(cap + 1);
    LyricsText text = parseBytes(bytes);
    if (!text.empty())
        text.source = source;
    return text;
}

LyricsText Lyrics::readBeside(const QString &audio)
{
    return readFile(besidePath(audio), LyricsText::Beside);
}

QStringList Lyrics::tagValues(const QString &audio)
{
    TagLib::FileRef ref(QFile::encodeName(audio).constData(), false);
    TagLib::File *file = ref.file();
    if (!file || !file->isValid())
        return {};
    Values values;
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        values.id3(mpeg->ID3v2Tag());
        values.ape(mpeg->APETag());
    } else if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file)) {
        values.xiph(flac->xiphComment());
    } else if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file)) {
        values.mp4(mp4->tag());
    } else if (auto *ape = dynamic_cast<TagLib::APE::File *>(file)) {
        values.ape(ape->APETag());
    } else if (auto *wav = dynamic_cast<TagLib::RIFF::WAV::File *>(file)) {
        values.id3(wav->ID3v2Tag());
    } else if (auto *dsf = dynamic_cast<TagLib::DSF::File *>(file)) {
        values.id3(dsf->tag());
    } else if (auto *dff = dynamic_cast<TagLib::DSDIFF::File *>(file)) {
        values.id3(dff->ID3v2Tag());
    } else {
        values.xiph(dynamic_cast<TagLib::Ogg::XiphComment *>(file->tag()));
    }
    return values.take();
}

LyricsText Lyrics::readTags(const QString &audio)
{
    QList<LyricsText> found;
    for (const QString &value : tagValues(audio)) {
        LyricsText text = parse(value);
        if (text.empty())
            continue;
        text.source = LyricsText::Tag;
        if (text.timed())
            return text;
        found.append(text);
    }
    return pick(found);
}

LyricsText Lyrics::pick(const QList<LyricsText> &sources)
{
    for (const LyricsText &text : sources) {
        if (text.timed())
            return text;
    }
    for (const LyricsText &text : sources) {
        if (!text.plain.isEmpty())
            return text;
    }
    return {};
}

LyricsText Lyrics::load(const QString &audio, const QString &storeFile)
{
    QList<LyricsText> found{readBeside(audio)};
    if (found.constLast().timed())
        return found.constLast();
    found.append(readTags(audio));
    if (found.constLast().timed())
        return found.constLast();
    if (!storeFile.isEmpty())
        found.append(readFile(storeFile, LyricsText::Store));
    return pick(found);
}

int Lyrics::lineAt(const QList<LyricLine> &lines, qint64 ms)
{
    const auto it = std::upper_bound(lines.cbegin(), lines.cend(), ms,
                                     [](qint64 value, const LyricLine &line) { return value < line.ms; });
    return int(it - lines.cbegin()) - 1;
}

bool Lyrics::pending() const
{
    return m_shown != m_current;
}

QVariantList Lyrics::lines() const
{
    QVariantList out;
    out.reserve(m_text.lines.size());
    for (const LyricLine &line : m_text.lines)
        out.append(QVariantMap{{QStringLiteral("ms"), line.ms}, {QStringLiteral("text"), line.text}});
    return out;
}

QStringList Lyrics::plain() const
{
    return m_text.plain;
}

LyricsText Lyrics::current() const
{
    return m_text;
}

int Lyrics::lineAt(qint64 ms) const
{
    return lineAt(m_text.lines, ms);
}

void Lyrics::setTracks(const QString &current, const QString &next)
{
    const bool moved = current != m_current;
    m_current = current;
    m_next = next;
    for (auto it = m_cache.begin(); it != m_cache.end();) {
        if (it.key() == current || it.key() == next)
            ++it;
        else
            it = m_cache.erase(it);
    }
    request(current);
    request(next);
    if (!publish() && moved)
        emit changed();
}

void Lyrics::refresh()
{
    m_cache.clear();
    m_pending.clear();
    request(m_current);
    request(m_next);
}

void Lyrics::waitForDone()
{
    m_pool.waitForDone();
}

void Lyrics::request(const QString &path)
{
    if (path.isEmpty() || m_cache.contains(path) || m_pending.contains(path))
        return;
    const int generation = ++m_generation;
    m_pending.insert(path, generation);
    m_pool.start([this, path, generation] {
        const LyricsText text = load(path);
        QMetaObject::invokeMethod(
            this, [this, path, generation, text] { loaded(path, generation, text); }, Qt::QueuedConnection);
    });
}

void Lyrics::loaded(const QString &path, int generation, const LyricsText &text)
{
    if (m_pending.value(path, -1) != generation)
        return;
    m_pending.remove(path);
    if (path != m_current && path != m_next)
        return;
    m_cache.insert(path, text);
    if (path == m_current)
        publish();
}

bool Lyrics::publish()
{
    QString shown;
    LyricsText text;
    if (const auto hit = m_cache.constFind(m_current); hit != m_cache.cend()) {
        shown = m_current;
        text = *hit;
    } else if (!m_current.isEmpty() && m_shown == m_current) {
        return false;
    }
    if (shown == m_shown && text.lines == m_text.lines && text.plain == m_text.plain)
        return false;
    m_shown = shown;
    m_text = text;
    emit changed();
    return true;
}

}
