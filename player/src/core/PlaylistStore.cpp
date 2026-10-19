#include "PlaylistStore.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStringList>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace microdap {

namespace {

constexpr const char *cueDirective = "#FLATDAP-CUE:";

QByteArray trim(const QByteArray &s)
{
    qsizetype a = 0;
    qsizetype b = s.size();
    while (a < b && static_cast<unsigned char>(s.at(a)) <= ' ')
        ++a;
    while (b > a && static_cast<unsigned char>(s.at(b - 1)) <= ' ')
        --b;
    return s.mid(a, b - a);
}

QByteArray fold(QByteArray s)
{
    for (char &c : s) {
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
    }
    return s;
}

bool extinf(const QByteArray &line)
{
    return fold(line.left(7)) == "#extinf";
}

QString lead(const QString &s)
{
    return !s.isEmpty() && (s.at(0) == QLatin1Char('#') || s.at(0).unicode() <= ' ') ? QStringLiteral("./") + s : s;
}

QByteArray signature(const QString &dir)
{
    QByteArray out;
    const QFileInfoList files =
        QDir(dir).entryInfoList(QDir::Files | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &file : files) {
        out += file.fileName().toUtf8() + '\n' + QByteArray::number(file.lastModified().toMSecsSinceEpoch()) + ' '
               + QByteArray::number(file.size()) + '\n';
    }
    return out;
}

QString findIn(const QString &dir, const QString &name, QDir::Filters filters)
{
    const QStringList names = QDir(dir).entryList(filters | QDir::NoDotAndDotDot, QDir::Name);
    for (const QString &candidate : names) {
        if (candidate.compare(name, Qt::CaseInsensitive) == 0)
            return QDir(dir).filePath(candidate);
    }
    return QDir(dir).filePath(name);
}

}

int PlaylistStore::cueOf(const QByteArray &line)
{
    const qsizetype n = static_cast<qsizetype>(std::strlen(cueDirective));
    if (line.size() <= n || fold(line.left(n)) != fold(cueDirective))
        return 0;
    const QByteArray value = trim(line.mid(n));
    if (value.isEmpty() || value.size() > 2
        || !std::all_of(value.cbegin(), value.cend(), [](char c) { return c >= '0' && c <= '9'; }))
        return 0;
    return value.toInt();
}

QString PlaylistStore::entryPath(QByteArray line, const QString &dir)
{
    if (line.size() > 7 && fold(line.left(7)) == "file://") {
        line = line.mid(7);
        if (line.startsWith("localhost"))
            line = line.mid(9);
        line = QByteArray::fromPercentEncoding(line);
    }
    line.replace('\\', '/');
    if (line.isEmpty())
        return {};
    const QString path = QString::fromUtf8(line);
    return QDir::cleanPath(QDir::isAbsolutePath(path) ? path : dir + QLatin1Char('/') + path);
}

M3u PlaylistStore::parse(const QByteArray &raw, const QString &dir)
{
    M3u m;
    const qsizetype skip = raw.startsWith("\xEF\xBB\xBF") ? 3 : 0;
    m.bom = raw.left(skip);
    const QByteArray text = raw.mid(skip);
    const qsizetype nl = text.indexOf('\n');
    if (nl > 0 && text.at(nl - 1) == '\r')
        m.eol = "\r\n";
    QByteArray group;
    int cue = 0;
    const auto flush = [&m, &group] {
        qsizetype a = 0;
        while (a < group.size()) {
            qsizetype b = group.indexOf('\n', a);
            b = b < 0 ? group.size() : b + 1;
            m.items.append({group.mid(a, b - a)});
            a = b;
        }
        group.clear();
    };
    qsizetype a = 0;
    while (a < text.size()) {
        qsizetype b = text.indexOf('\n', a);
        b = b < 0 ? text.size() : b + 1;
        const QByteArray rawLine = text.mid(a, b - a);
        a = b;
        const QByteArray line = trim(rawLine);
        if (line.isEmpty()) {
            flush();
            cue = 0;
            m.items.append({rawLine});
        } else if (line.at(0) == '#') {
            const int c = cueOf(line);
            const bool info = extinf(line);
            if (info)
                flush();
            if (!group.isEmpty() || info || c)
                group += rawLine;
            else
                m.items.append({rawLine});
            if (c)
                cue = c;
        } else {
            m.items.append({group + rawLine, entryPath(line, dir), true, cue});
            cue = 0;
            group.clear();
        }
    }
    flush();
    return m;
}

QByteArray PlaylistStore::bytes(const M3u &doc)
{
    QByteArray out = doc.bom;
    for (const M3uItem &item : doc.items)
        out += item.text;
    return out;
}

QString PlaylistStore::relative(const QString &path, const QString &dir)
{
    return lead(QDir(dir).relativeFilePath(path));
}

qsizetype PlaylistStore::unit(const M3u &doc, qsizetype i)
{
    qsizetype at = i;
    for (qsizetype k = i - 1; k >= 0; --k) {
        const QByteArray line = trim(doc.items.at(k).text);
        if (doc.items.at(k).entry || line.isEmpty() || line.at(0) != '#')
            break;
        if (extinf(line) || cueOf(line))
            at = k;
    }
    return at;
}

qsizetype PlaylistStore::insert(M3u &doc, qsizetype before, const QString &path, const QString &dir, int cue)
{
    if (doc.items.isEmpty() && doc.bom.isEmpty())
        doc.items.append({"#EXTM3U" + doc.eol});
    qsizetype at = doc.items.size();
    qsizetype n = 0;
    for (qsizetype i = 0; i < doc.items.size(); ++i) {
        if (doc.items.at(i).entry && n++ == before) {
            at = i;
            break;
        }
    }
    if (at == doc.items.size() && at > 0) {
        QByteArray &last = doc.items[at - 1].text;
        if (!last.isEmpty() && !last.endsWith('\n'))
            last += doc.eol;
    }
    at = unit(doc, at);
    M3uItem item;
    if (cue)
        item.text = cueDirective + QByteArray::number(cue) + doc.eol;
    item.text += relative(path, dir).toUtf8() + doc.eol;
    item.path = path;
    item.entry = true;
    item.cue = cue;
    doc.items.insert(at, item);
    return at;
}

void PlaylistStore::erase(M3u &doc, qsizetype i)
{
    const qsizetype from = unit(doc, i);
    doc.items.remove(from, i - from + 1);
}

int PlaylistStore::remove(M3u &doc, const QString &path)
{
    int n = 0;
    for (qsizetype i = doc.items.size() - 1; i >= 0; --i) {
        if (doc.items.at(i).entry && doc.items.at(i).path == path) {
            const qsizetype from = unit(doc, i);
            erase(doc, i);
            i = from;
            ++n;
        }
    }
    return n;
}

void PlaylistStore::swap(M3u &doc, qsizetype a, qsizetype b)
{
    if (a == b)
        return;
    if (a > b)
        std::swap(a, b);
    for (qsizetype i : {a, b}) {
        QByteArray &text = doc.items[i].text;
        if (!text.isEmpty() && !text.endsWith('\n'))
            text += doc.eol;
    }
    const qsizetype sa = unit(doc, a);
    const qsizetype sb = unit(doc, b);
    QList<M3uItem> out = doc.items.mid(0, sa);
    out += doc.items.mid(sb, b + 1 - sb);
    out += doc.items.mid(a + 1, sb - a - 1);
    out += doc.items.mid(sa, a + 1 - sa);
    out += doc.items.mid(b + 1);
    doc.items = std::move(out);
}

QString PlaylistStore::playlistsDir(const QString &root)
{
    return findIn(root, QStringLiteral("Playlists"), QDir::Dirs);
}

QString PlaylistStore::findFile(const QString &dir, const QString &name)
{
    return findIn(dir, name, QDir::Files | QDir::Hidden);
}

PlaylistStore::Loaded PlaylistStore::read(const QString &path)
{
    Loaded loaded;
    const QString dir = QFileInfo(path).absolutePath();
    const QStringList spares{path + QStringLiteral(".tmp"), path + QStringLiteral(".new")};
    QString source = path;
    if (!QFileInfo::exists(path)) {
        source.clear();
        for (const QString &spare : spares) {
            if (QFileInfo::exists(spare)) {
                source = spare;
                break;
            }
        }
        if (source.isEmpty())
            return loaded;
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "microdap playlists:" << source << file.errorString();
        loaded.state = State::Failed;
        return loaded;
    }
    const QByteArray raw = file.read(maxBytes + 1);
    if (raw.size() > maxBytes || file.error() != QFileDevice::NoError) {
        qWarning() << "microdap playlists:" << source << (raw.size() > maxBytes ? QStringLiteral("over 1 MB")
                                                                                : file.errorString());
        loaded.state = State::Failed;
        return loaded;
    }
    file.close();
    loaded.doc = parse(raw, dir);
    loaded.state = State::Read;
    if (source != path && !write(path, loaded.doc))
        return loaded;
    for (const QString &spare : spares) {
        if (QFileInfo::exists(spare))
            QFile::remove(spare);
    }
    return loaded;
}

bool PlaylistStore::write(const QString &path, const M3u &doc)
{
    const QString dir = QFileInfo(path).absolutePath();
    if (!QDir().mkpath(dir)) {
        qWarning() << "microdap playlists: cannot create" << dir;
        return false;
    }
    const QByteArray before = signature(dir);
    const QString spare = path + QStringLiteral(".new");
    QFile file(spare);
    const QByteArray data = bytes(doc);
    const bool written = file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size()
                         && file.flush() && ::fsync(file.handle()) == 0;
    const QString writeError = file.errorString();
    file.close();
    const bool renamed =
        written && ::rename(QFile::encodeName(spare).constData(), QFile::encodeName(path).constData()) == 0;
    if (!renamed) {
        qWarning() << "microdap playlists: cannot write" << path << (written ? qt_error_string(errno) : writeError);
        QFile::remove(spare);
        return false;
    }
    const int fd = ::open(QFile::encodeName(dir).constData(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd >= 0) {
        ::fsync(fd);
        ::close(fd);
    }
    const QByteArray after = signature(dir);
    const auto settled = m_settled.constFind(dir);
    if (settled == m_settled.cend() || *settled == before) {
        m_written.insert(dir, after);
        m_settled.insert(dir, after);
    } else {
        m_written.remove(dir);
    }
    return true;
}

PlaylistStore::Edit PlaylistStore::edit(const QString &path, const Change &change)
{
    Loaded loaded = read(path);
    if (loaded.state == State::Failed)
        return Edit::Unreadable;
    if (!change(loaded.doc))
        return Edit::Done;
    return write(path, loaded.doc) ? Edit::Done : Edit::Unwritten;
}

void PlaylistStore::settle(const QString &dir)
{
    const QString clean = QDir::cleanPath(dir);
    m_settled.insert(clean, signature(clean));
}

bool PlaylistStore::ownChange(const QString &dir) const
{
    const auto it = m_written.constFind(QDir::cleanPath(dir));
    return it != m_written.cend() && *it == signature(dir);
}

}
