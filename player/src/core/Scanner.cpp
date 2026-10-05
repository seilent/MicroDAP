#include "Scanner.h"

#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/tag.h>
#include <taglib/tbytevector.h>
#include <taglib/tfile.h>
#include <taglib/tlist.h>
#include <taglib/tmap.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>
#include <taglib/tvariant.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSet>
#include <QTimeZone>

#include <optional>
#include <utility>

namespace casdap {

namespace {

struct Picture
{
    QByteArray data;
    QString mime;
};

const QSet<QString> &extensions()
{
    static const QSet<QString> set{
        QStringLiteral("flac"), QStringLiteral("mp3"),  QStringLiteral("m4a"),
        QStringLiteral("ogg"),  QStringLiteral("opus"), QStringLiteral("wav"),
    };
    return set;
}

const QStringList &folderNames()
{
    static const QStringList names{
        QStringLiteral("cover.jpg"),
        QStringLiteral("cover.png"),
        QStringLiteral("folder.jpg"),
        QStringLiteral("front.jpg"),
    };
    return names;
}

QString fromTag(const TagLib::String &s)
{
    return QString::fromUtf8(s.toCString(true)).trimmed();
}

QStringList splitArtists(const TagLib::StringList &values, const QString &albumArtist)
{
    QStringList names;
    for (const TagLib::String &value : values) {
        QString text = fromTag(value);
        if (text.isEmpty())
            continue;
        QStringList parts;
        if (!albumArtist.isEmpty() && text.compare(albumArtist, Qt::CaseInsensitive) == 0) {
            parts.append(text);
        } else {
            text.replace(QStringLiteral(" / "), QStringLiteral("; "));
            text.replace(QStringLiteral(", "), QStringLiteral("; "));
            parts = text.split(QStringLiteral("; "), Qt::SkipEmptyParts);
        }
        for (const QString &part : parts) {
            const QString name = part.trimmed();
            if (!name.isEmpty() && !names.contains(name))
                names.append(name);
        }
    }
    return names;
}

QByteArray toBytes(const TagLib::ByteVector &v)
{
    return QByteArray(v.data(), static_cast<qsizetype>(v.size()));
}

int leadingInt(const QString &s)
{
    return s.section(QLatin1Char('/'), 0, 0).trimmed().toInt();
}

FileStamp stampOf(const QFileInfo &info)
{
    return {info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch(), info.size()};
}

bool cancelled(const std::atomic_bool *cancel)
{
    return cancel && cancel->load(std::memory_order_relaxed);
}

QString rootPrefix(const QString &root)
{
    const QString path = QDir(root).absolutePath();
    return path.endsWith(QLatin1Char('/')) ? path : path + QLatin1Char('/');
}

std::optional<Picture> embeddedPicture(TagLib::File *file)
{
    const TagLib::List<TagLib::VariantMap> pictures = file->complexProperties("PICTURE");
    if (pictures.isEmpty())
        return std::nullopt;

    const TagLib::VariantMap *chosen = &pictures.front();
    for (const TagLib::VariantMap &picture : pictures) {
        if (picture.value("pictureType").toString() == "Front Cover") {
            chosen = &picture;
            break;
        }
    }

    const QByteArray data = toBytes(chosen->value("data").toByteVector());
    if (data.isEmpty())
        return std::nullopt;
    return Picture{data, fromTag(chosen->value("mimeType").toString())};
}

}

Scanner::Scanner(const QString &artDir)
    : m_artDir(QDir::cleanPath(artDir))
{
}

bool Scanner::isAudioFile(const QString &path)
{
    return extensions().contains(QFileInfo(path).suffix().toLower());
}

QString Scanner::albumKey(const QString &album, const QString &albumArtist)
{
    return album + QLatin1Char('\n') + albumArtist;
}

ScanResult Scanner::scan(const QStringList &roots, const QHash<QString, FileStamp> &snapshot,
                         const std::atomic_bool *cancel)
{
    m_art.clear();
    m_folderArt.clear();

    ScanResult result;
    QSet<QString> seen;
    QStringList prefixes;
    for (const QString &root : roots) {
        const QFileInfo rootInfo(root);
        if (!rootInfo.isDir() || !rootInfo.isReadable())
            continue;
        prefixes.append(rootPrefix(root));

        QDirIterator it(root, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (cancelled(cancel))
                return ScanResult();
            const QFileInfo info = it.nextFileInfo();
            const QString path = info.absoluteFilePath();
            if (!isAudioFile(path) || seen.contains(path))
                continue;
            seen.insert(path);

            const FileStamp stamp = stampOf(info);
            const auto known = snapshot.constFind(path);
            const bool unchanged = known != snapshot.cend() && known->mtime == stamp.mtime
                                   && known->size == stamp.size;
            if (unchanged)
                continue;

            TrackInfo track = read(info, stamp);
            if (known == snapshot.cend())
                result.added.append(std::move(track));
            else
                result.updated.append(std::move(track));
        }
    }

    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (seen.contains(it.key()))
            continue;
        for (const QString &prefix : prefixes) {
            if (it.key().startsWith(prefix)) {
                result.removed.append(it.key());
                break;
            }
        }
    }
    return result;
}

TrackInfo Scanner::read(const QFileInfo &info, const FileStamp &stamp)
{
    TrackInfo track;
    track.path = info.absoluteFilePath();
    track.mtime = stamp.mtime;
    track.size = stamp.size;

    const QByteArray name = QFile::encodeName(track.path);
    TagLib::FileRef ref(name.constData(), true, TagLib::AudioProperties::Average);
    const TagLib::Tag *tag = ref.isNull() ? nullptr : ref.tag();
    TagLib::StringList artistValues;
    if (!ref.isNull()) {
        if (tag) {
            track.title = fromTag(tag->title());
            track.album = fromTag(tag->album());
            track.genre = fromTag(tag->genre());
            track.trackNo = static_cast<int>(tag->track());
            track.year = static_cast<int>(tag->year());
        }
        if (const TagLib::AudioProperties *audio = ref.audioProperties())
            track.durationMs = audio->lengthInMilliseconds();
        if (TagLib::File *file = ref.file()) {
            TagLib::PropertyMap map = file->properties();
            if (map.contains("ARTISTS") && !map["ARTISTS"].isEmpty())
                artistValues = map["ARTISTS"];
            else if (map.contains("ARTIST"))
                artistValues = map["ARTIST"];
            if (map.contains("ALBUMARTIST") && !map["ALBUMARTIST"].isEmpty())
                track.albumArtist = fromTag(map["ALBUMARTIST"].front());
            if (map.contains("DISCNUMBER") && !map["DISCNUMBER"].isEmpty())
                track.discNo = leadingInt(fromTag(map["DISCNUMBER"].front()));
        }
    }

    track.artists = splitArtists(artistValues, track.albumArtist);
    if (track.artists.isEmpty() && tag)
        track.artists = splitArtists(TagLib::StringList(tag->artist()), track.albumArtist);
    if (track.artists.isEmpty())
        track.artists.append(QStringLiteral("unknown artist"));
    track.artist = track.artists.join(QStringLiteral(", "));

    if (track.title.isEmpty())
        track.title = info.completeBaseName();
    if (track.album.isEmpty())
        track.album = QStringLiteral("unknown album");
    if (track.albumArtist.isEmpty())
        track.albumArtist = track.artist;

    track.artPath = resolveArt(ref.isNull() ? nullptr : ref.file(),
                               albumKey(track.album, track.albumArtist), info.dir());
    return track;
}

QString Scanner::resolveArt(TagLib::File *file, const QString &key, const QDir &dir)
{
    if (const auto it = m_art.constFind(key); it != m_art.cend())
        return *it;

    QString path;
    if (file) {
        if (const std::optional<Picture> picture = embeddedPicture(file))
            path = writeArt(picture->data, picture->mime);
    }
    if (path.isEmpty())
        path = folderArt(dir);
    if (!path.isEmpty())
        m_art.insert(key, path);
    return path;
}

QString Scanner::writeArt(const QByteArray &data, const QString &mime)
{
    if (data.isEmpty())
        return {};
    const bool png = mime.compare(QLatin1StringView("image/png"), Qt::CaseInsensitive) == 0;
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex());
    const QString path = m_artDir + QLatin1Char('/') + hash
                         + (png ? QStringLiteral(".png") : QStringLiteral(".jpg"));
    if (QFileInfo::exists(path))
        return path;
    if (!QDir().mkpath(m_artDir))
        return {};
    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return {};
    out.write(data);
    if (!out.commit())
        return {};
    return path;
}

QString Scanner::folderArt(const QDir &dir)
{
    const QString dirPath = dir.absolutePath();
    if (const auto it = m_folderArt.constFind(dirPath); it != m_folderArt.cend())
        return *it;

    QHash<QString, QString> lower;
    const QStringList entries = dir.entryList(QDir::Files | QDir::Readable);
    for (const QString &entry : entries)
        lower.insert(entry.toLower(), entry);

    QString found;
    for (const QString &candidate : folderNames()) {
        if (const auto hit = lower.constFind(candidate); hit != lower.cend()) {
            found = dir.absoluteFilePath(*hit);
            break;
        }
    }
    m_folderArt.insert(dirPath, found);
    return found;
}

}
