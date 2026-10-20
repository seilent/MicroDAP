#include "ArtistStore.h"

#include "LinkArt.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>
#include <QtEndian>

#include <algorithm>
#include <cstring>

namespace microdap {

namespace {

constexpr char originMagic[] = "FDORG1";

QString hex(quint64 key)
{
    return QStringLiteral("%1").arg(key, 16, 16, QLatin1Char('0'));
}

bool sameHead(const ArtistStore::Head &a, const ArtistStore::Head &b)
{
    return a.name == b.name && a.total == b.total && a.crc == b.crc && a.w == b.w && a.h == b.h;
}

}

QString ArtistStore::defaultRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/microdap/artists");
}

quint64 ArtistStore::key(const QString &name)
{
    const QByteArray folded = name.trimmed().normalized(QString::NormalizationForm_KC).toCaseFolded().toUtf8();
    const QByteArray hash = QCryptographicHash::hash(folded, QCryptographicHash::Sha256);
    return qFromLittleEndian<quint64>(hash.constData());
}

QString ArtistStore::cutMeta(const QString &text)
{
    QByteArray bytes = text.toUtf8();
    if (bytes.size() <= metaBytes)
        return text;
    qsizetype n = metaBytes;
    while (n > 0 && (quint8(bytes.at(n)) & 0xc0) == 0x80)
        --n;
    return QString::fromUtf8(bytes.left(n));
}

bool ArtistStore::jpegFrame(const QByteArray &bytes, int &w, int &h)
{
    const auto *b = reinterpret_cast<const quint8 *>(bytes.constData());
    const qsizetype n = bytes.size();
    if (n < 4 || b[0] != 0xff || b[1] != 0xd8)
        return false;
    qsizetype i = 2;
    while (i + 4 <= n) {
        if (b[i] != 0xff)
            return false;
        if (b[i + 1] == 0xff) {
            ++i;
            continue;
        }
        const int m = b[i + 1];
        const qsizetype len = qsizetype(b[i + 2]) << 8 | b[i + 3];
        if (m == 0xc0 || m == 0xc1) {
            if (i + 10 > n)
                return false;
            h = b[i + 5] << 8 | b[i + 6];
            w = b[i + 7] << 8 | b[i + 8];
            return b[i + 9] == 1 || b[i + 9] == 3;
        }
        if ((m >= 0xc2 && m <= 0xcf && m != 0xc4 && m != 0xc8 && m != 0xcc) || m == 0xda)
            return false;
        i += 2 + len;
    }
    return false;
}

ArtistStore::ArtistStore(QString root, int side, quint32 maxBytes, quint32 chunk, qint64 cap, QObject *parent)
    : QObject(parent)
    , m_root(std::move(root))
    , m_side(side)
    , m_max(maxBytes)
    , m_chunk(std::max<quint32>(1, chunk))
    , m_cap(cap)
{
    m_originTimer.setSingleShot(true);
    m_originTimer.setInterval(originDelayMs);
    connect(&m_originTimer, &QTimer::timeout, this, &ArtistStore::flushOrigins);
    rebuild();
}

ArtistStore::~ArtistStore()
{
    flushOrigins();
}

QString ArtistStore::root() const
{
    return m_root;
}

QString ArtistStore::imagePath(quint64 key) const
{
    return m_root + QStringLiteral("/artist/") + hex(key).right(2) + QLatin1Char('/') + hex(key) + QStringLiteral(".jpg");
}

int ArtistStore::revision() const
{
    return m_revision;
}

void ArtistStore::bump()
{
    ++m_revision;
    emit changed();
}

void ArtistStore::rebuild()
{
    m_entries.clear();
    QDirIterator it(m_root + QStringLiteral("/artist"), {QStringLiteral("*.jpg")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo jpg = it.nextFileInfo();
        bool ok = false;
        const quint64 k = jpg.completeBaseName().toULongLong(&ok, 16);
        if (!ok || jpg.completeBaseName().size() != 16)
            continue;
        Entry e;
        e.key = k;
        e.size = quint32(jpg.size());
        e.mtime = jpg.lastModified().toMSecsSinceEpoch();
        QFile info(jpg.absolutePath() + QLatin1Char('/') + jpg.completeBaseName() + QStringLiteral(".inf"));
        if (info.open(QIODevice::ReadOnly) && info.size() < 16384) {
            const QJsonObject o = QJsonDocument::fromJson(info.readAll()).object();
            if (quint32(o.value(QStringLiteral("size")).toDouble()) == e.size) {
                e.name = o.value(QStringLiteral("name")).toString();
                e.mbid = o.value(QStringLiteral("mbid")).toString();
                e.source = o.value(QStringLiteral("source")).toString();
                e.credit = o.value(QStringLiteral("credit")).toString();
                e.crc = quint32(o.value(QStringLiteral("crc")).toDouble());
                e.wrong = o.value(QStringLiteral("wrong")).toBool();
                const int fx = o.value(QStringLiteral("focusX")).toInt(-1);
                const int fy = o.value(QStringLiteral("focusY")).toInt(-1);
                if (fx >= 0 && fx <= focusScale && fy >= 0 && fy <= focusScale) {
                    e.focusX = fx;
                    e.focusY = fy;
                }
            }
        }
        m_entries.append(e);
    }
    std::sort(m_entries.begin(), m_entries.end(), [](const Entry &a, const Entry &b) { return a.key < b.key; });
    loadOrigins();
}

void ArtistStore::loadOrigins()
{
    m_origins.clear();
    QFile f(m_root + QStringLiteral("/origin.bin"));
    if (!f.open(QIODevice::ReadOnly))
        return;
    const QByteArray b = f.readAll();
    if (b.size() < 10 || std::memcmp(b.constData(), originMagic, 6) != 0)
        return;
    const quint32 n = qFromLittleEndian<quint32>(b.constData() + 6);
    if (qsizetype(10 + qsizetype(n) * 9) != b.size())
        return;
    for (quint32 i = 0; i < n; ++i) {
        const quint64 k = qFromLittleEndian<quint64>(b.constData() + 10 + qsizetype(i) * 9);
        const quint8 bits = quint8(b.at(10 + qsizetype(i) * 9 + 8)) & (originLibrary | originPhone);
        const bool stored = std::any_of(m_entries.cbegin(), m_entries.cend(), [k](const Entry &e) { return e.key == k; });
        if (stored && bits)
            m_origins.append({k, bits});
        else
            m_originsDirty = true;
    }
    std::sort(m_origins.begin(), m_origins.end());
}

bool ArtistStore::flushOrigins()
{
    m_originTimer.stop();
    if (!m_originsDirty)
        return true;
    QByteArray b(originMagic, 6);
    const quint32 n = quint32(m_origins.size());
    b.resize(10 + qsizetype(n) * 9);
    qToLittleEndian<quint32>(n, b.data() + 6);
    for (quint32 i = 0; i < n; ++i) {
        qToLittleEndian<quint64>(m_origins.at(i).first, b.data() + 10 + qsizetype(i) * 9);
        b[10 + qsizetype(i) * 9 + 8] = char(m_origins.at(i).second);
    }
    QSaveFile save(m_root + QStringLiteral("/origin.bin"));
    if (!QDir().mkpath(m_root) || !save.open(QIODevice::WriteOnly) || save.write(b) != b.size() || !save.commit()) {
        qWarning().noquote() << "microdap art: origin write failed:" << save.errorString();
        return false;
    }
    m_originsDirty = false;
    return true;
}

quint8 ArtistStore::origin(quint64 key) const
{
    for (const auto &o : m_origins) {
        if (o.first == key)
            return o.second;
    }
    return 0;
}

void ArtistStore::addOrigin(quint64 key, quint8 bits)
{
    if (!bits)
        return;
    for (auto &o : m_origins) {
        if (o.first == key) {
            if ((o.second | bits) == o.second)
                return;
            o.second |= bits;
            m_originsDirty = true;
            m_originTimer.start();
            return;
        }
    }
    m_origins.append({key, bits});
    std::sort(m_origins.begin(), m_origins.end());
    m_originsDirty = true;
    m_originTimer.start();
}

void ArtistStore::dropOrigin(quint64 key)
{
    for (qsizetype i = 0; i < m_origins.size(); ++i) {
        if (m_origins.at(i).first == key) {
            m_origins.removeAt(i);
            m_originsDirty = true;
            m_originTimer.start();
            return;
        }
    }
}

bool ArtistStore::writeInfo(const Entry &e) const
{
    QJsonObject o{{QStringLiteral("name"), e.name},   {QStringLiteral("mbid"), e.mbid},
                  {QStringLiteral("source"), e.source}, {QStringLiteral("credit"), e.credit},
                  {QStringLiteral("crc"), double(e.crc)}, {QStringLiteral("size"), double(e.size)},
                  {QStringLiteral("wrong"), e.wrong}};
    if (e.focusX >= 0 && e.focusY >= 0) {
        o.insert(QStringLiteral("focusX"), e.focusX);
        o.insert(QStringLiteral("focusY"), e.focusY);
    }
    const QString jpg = imagePath(e.key);
    QSaveFile save(jpg.left(jpg.size() - 4) + QStringLiteral(".inf"));
    const QByteArray bytes = QJsonDocument(o).toJson(QJsonDocument::Compact);
    return save.open(QIODevice::WriteOnly) && save.write(bytes) == bytes.size() && save.commit();
}

ArtistStore::Part *ArtistStore::part(quint64 key)
{
    for (Part &p : m_parts) {
        if (p.key == key)
            return &p;
    }
    return nullptr;
}

void ArtistStore::dropPart(quint64 key)
{
    if (m_active && m_cur == key)
        m_active = false;
    m_parts.erase(std::remove_if(m_parts.begin(), m_parts.end(), [key](const Part &p) { return p.key == key; }),
                  m_parts.end());
}

bool ArtistStore::pushing() const
{
    return m_active;
}

ArtistStore::Code ArtistStore::begin(const Head &in, quint32 &offset, quint32 &prefixCrc, quint32 *push)
{
    offset = 0;
    prefixCrc = 0;
    m_active = false;
    ++m_push;
    if (push)
        *push = m_push;
    Head h = in;
    h.source = cutMeta(in.source);
    h.credit = cutMeta(in.credit);
    h.mbid = cutMeta(in.mbid);
    if (h.focusX < 0 || h.focusX > focusScale || h.focusY < 0 || h.focusY > focusScale) {
        h.focusX = -1;
        h.focusY = -1;
    }
    if (h.name.trimmed().isEmpty() || h.name.toUtf8().size() > nameBytes || h.total == 0)
        return Code::BadValue;
    if (h.total > m_max)
        return Code::TooBig;
    if (h.w != m_side || h.h != m_side)
        return Code::BadValue;
    const quint64 k = key(h.name);
    for (Entry &e : m_entries) {
        if (e.key != k)
            continue;
        addOrigin(k, h.origin);
        if (e.crc == h.crc && e.size == h.total) {
            if (e.wrong != h.wrong || e.source != h.source || e.credit != h.credit || e.focusX != h.focusX
                || e.focusY != h.focusY) {
                Entry n = e;
                n.wrong = h.wrong;
                n.source = h.source;
                n.credit = h.credit;
                n.focusX = h.focusX;
                n.focusY = h.focusY;
                if (writeInfo(n))
                    e = n;
            }
            offset = h.total;
            prefixCrc = h.crc;
            return Code::Ok;
        }
    }
    Part *p = part(k);
    if (p && !sameHead(p->head, h)) {
        dropPart(k);
        p = nullptr;
    }
    if (!p) {
        Part np;
        np.key = k;
        m_parts.append(np);
        p = &m_parts.last();
    }
    p->head = h;
    p->seq = ++m_seq;
    quint32 whole = quint32(p->data.size()) / m_chunk * m_chunk;
    if (whole >= h.total)
        whole = (h.total - 1) / m_chunk * m_chunk;
    if (whole != quint32(p->data.size())) {
        p->data.truncate(whole);
        p->crc = linkCrc32(p->data);
    }
    m_cur = k;
    m_active = true;
    while (m_parts.size() > maxParts) {
        const auto old = std::min_element(m_parts.cbegin(), m_parts.cend(),
                                          [](const Part &a, const Part &b) { return a.seq < b.seq; });
        dropPart(old->key);
    }
    p = part(k);
    if (!p)
        return Code::Busy;
    offset = quint32(p->data.size());
    prefixCrc = p->data.isEmpty() ? 0 : p->crc;
    return Code::Ok;
}

ArtistStore::Code ArtistStore::data(quint32 offset, const QByteArray &bytes, quint32 push)
{
    if (push && push != m_push)
        return Code::Busy;
    Part *p = m_active ? part(m_cur) : nullptr;
    if (!p)
        return Code::Busy;
    if (offset == 0 && !p->data.isEmpty()) {
        p->data.clear();
        p->crc = 0;
    }
    if (offset != quint32(p->data.size()))
        return Code::WriteError;
    if (quint64(p->data.size()) + quint64(bytes.size()) > p->head.total)
        return Code::BadValue;
    p->data += bytes;
    p->crc = linkCrc32(bytes, p->crc);
    return Code::Ok;
}

ArtistStore::Code ArtistStore::end(quint32 crc, quint32 push)
{
    if (push && push != m_push)
        return Code::Refused;
    Part *p = m_active ? part(m_cur) : nullptr;
    if (!p)
        return Code::Refused;
    const quint64 k = p->key;
    m_active = false;
    if (quint32(p->data.size()) < p->head.total)
        return Code::BadValue;
    if (crc != p->head.crc || p->crc != p->head.crc) {
        dropPart(k);
        return Code::Crc;
    }
    int w = 0;
    int h = 0;
    if (!jpegFrame(p->data, w, h) || w != m_side || h != m_side) {
        dropPart(k);
        return Code::BadValue;
    }
    const QString jpg = imagePath(k);
    if (!QDir().mkpath(QFileInfo(jpg).absolutePath()))
        return Code::Busy;
    QSaveFile save(jpg);
    if (!save.open(QIODevice::WriteOnly) || save.write(p->data) != p->data.size() || !save.commit())
        return save.error() == QFileDevice::ResourceError ? Code::NoSpace : Code::Busy;
    Entry e;
    e.key = k;
    e.name = p->head.name;
    e.mbid = p->head.mbid;
    e.source = p->head.source;
    e.credit = p->head.credit;
    e.crc = p->head.crc;
    e.size = p->head.total;
    e.wrong = p->head.wrong;
    e.focusX = p->head.focusX;
    e.focusY = p->head.focusY;
    e.mtime = QDateTime::currentMSecsSinceEpoch();
    if (!writeInfo(e)) {
        dropPart(k);
        dropImage(k);
        bump();
        return Code::NoSpace;
    }
    const quint8 bits = p->head.origin;
    dropPart(k);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [k](const Entry &x) { return x.key == k; }),
                    m_entries.end());
    m_entries.insert(std::lower_bound(m_entries.begin(), m_entries.end(), e,
                                      [](const Entry &a, const Entry &b) { return a.key < b.key; }),
                     e);
    addOrigin(k, bits);
    if (m_originsDirty)
        flushOrigins();
    evict(k);
    bump();
    return Code::Ok;
}

void ArtistStore::dropImage(quint64 k)
{
    const QString jpg = imagePath(k);
    QFile::remove(jpg);
    QFile::remove(jpg.left(jpg.size() - 4) + QStringLiteral(".inf"));
    dropOrigin(k);
    m_entries.erase(std::remove_if(m_entries.begin(), m_entries.end(), [k](const Entry &x) { return x.key == k; }),
                    m_entries.end());
}

void ArtistStore::evict(quint64 keep)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    while (qint64(bytes()) > m_cap) {
        const Entry *oldest = nullptr;
        for (const Entry &e : std::as_const(m_entries)) {
            if (e.key != keep && (!oldest || std::min(e.mtime, now) < std::min(oldest->mtime, now)))
                oldest = &e;
        }
        if (!oldest)
            return;
        dropImage(oldest->key);
    }
}

bool ArtistStore::remove(const QString &name)
{
    const quint64 k = key(name);
    dropPart(k);
    const bool had = std::any_of(m_entries.cbegin(), m_entries.cend(), [k](const Entry &e) { return e.key == k; });
    dropImage(k);
    if (had)
        bump();
    return true;
}

bool ArtistStore::tag(const QString &name, quint8 bits)
{
    const quint64 k = key(name);
    if (!std::any_of(m_entries.cbegin(), m_entries.cend(), [k](const Entry &e) { return e.key == k; }))
        return false;
    addOrigin(k, bits);
    return true;
}

int ArtistStore::prune(const QSet<quint64> &keep)
{
    QList<quint64> gone;
    for (const Entry &e : std::as_const(m_entries)) {
        if (!e.name.trimmed().isEmpty() && !keep.contains(e.key) && origin(e.key) == originLibrary)
            gone.append(e.key);
    }
    for (quint64 k : std::as_const(gone))
        dropImage(k);
    if (!gone.isEmpty())
        bump();
    return int(gone.size());
}

QList<ArtistStore::Entry> ArtistStore::listed() const
{
    QList<Entry> out = m_entries;
    for (Entry &e : out)
        e.origin = origin(e.key);
    return out;
}

quint64 ArtistStore::bytes() const
{
    quint64 n = 0;
    for (const Entry &e : m_entries)
        n += e.size;
    return n;
}

qint64 ArtistStore::cap() const
{
    return m_cap;
}

void ArtistStore::touch(quint64 k)
{
    const QDateTime now = QDateTime::currentDateTime();
    if (now.date().year() < 2025)
        return;
    for (Entry &e : m_entries) {
        if (e.key != k)
            continue;
        QFile f(imagePath(k));
        if (f.open(QIODevice::ReadWrite))
            f.setFileTime(now, QFileDevice::FileModificationTime);
        e.mtime = now.toMSecsSinceEpoch();
        return;
    }
}

QString ArtistStore::photoArtist(const QStringList &names) const
{
    for (const QString &name : names) {
        const quint64 k = key(name);
        for (const Entry &e : m_entries) {
            if (e.key == k)
                return name;
        }
    }
    return {};
}

QUrl ArtistStore::photoFor(const QStringList &names) const
{
    const QString name = photoArtist(names);
    if (name.isEmpty())
        return {};
    const quint64 k = key(name);
    QUrl url = QUrl::fromLocalFile(imagePath(k));
    for (const Entry &e : m_entries) {
        if (e.key == k)
            url.setQuery(QStringLiteral("v=%1").arg(e.crc));
    }
    return url;
}

QPointF ArtistStore::photoFocus(const QStringList &names) const
{
    const QString name = photoArtist(names);
    const quint64 k = name.isEmpty() ? 0 : key(name);
    for (const Entry &e : m_entries) {
        if (e.key == k && e.focusX >= 0)
            return {double(e.focusX) / focusScale, double(e.focusY) / focusScale};
    }
    return {0.5, 0.5};
}

void ArtistStore::touchName(const QString &name)
{
    if (!name.isEmpty())
        touch(key(name));
}

QVariantMap ArtistStore::photoInfo(const QString &name) const
{
    const quint64 k = key(name);
    for (const Entry &e : m_entries) {
        if (e.key == k)
            return {{QStringLiteral("name"), e.name},       {QStringLiteral("mbid"), e.mbid},
                    {QStringLiteral("source"), e.source},   {QStringLiteral("credit"), e.credit},
                    {QStringLiteral("crc"), double(e.crc)}, {QStringLiteral("wrong"), e.wrong},
                    {QStringLiteral("phone"), (origin(k) & originPhone) != 0}};
    }
    return {};
}

}
