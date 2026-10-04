#include "Scanner.h"

#include <taglib/attachedpictureframe.h>
#include <taglib/audioproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/id3v2tag.h>
#include <taglib/mp4coverart.h>
#include <taglib/mp4file.h>
#include <taglib/mp4item.h>
#include <taglib/mp4tag.h>
#include <taglib/mpegfile.h>
#include <taglib/tag.h>
#include <taglib/tbytevector.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

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

std::optional<Picture> embeddedPicture(TagLib::File *file)
{
    if (auto *flac = dynamic_cast<TagLib::FLAC::File *>(file)) {
        const TagLib::List<TagLib::FLAC::Picture *> pictures = flac->pictureList();
        if (pictures.isEmpty())
            return std::nullopt;
        const TagLib::FLAC::Picture *picture = pictures.front();
        return Picture{toBytes(picture->data()), fromTag(picture->mimeType())};
    }
    if (auto *mpeg = dynamic_cast<TagLib::MPEG::File *>(file)) {
        TagLib::ID3v2::Tag *tag = mpeg->ID3v2Tag();
        if (!tag)
            return std::nullopt;
        const TagLib::ID3v2::FrameList frames = tag->frameList("APIC");
        for (TagLib::ID3v2::Frame *frame : frames) {
            if (auto *apic = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(frame))
                return Picture{toBytes(apic->picture()), fromTag(apic->mimeType())};
        }
        return std::nullopt;
    }
    if (auto *mp4 = dynamic_cast<TagLib::MP4::File *>(file)) {
        TagLib::MP4::Tag *tag = mp4->tag();
        if (!tag || !tag->contains("covr"))
            return std::nullopt;
        const TagLib::MP4::CoverArtList covers = tag->item("covr").toCoverArtList();
        if (covers.isEmpty())
            return std::nullopt;
        const TagLib::MP4::CoverArt &cover = covers.front();
        const bool png = cover.format() == TagLib::MP4::CoverArt::PNG;
        return Picture{toBytes(cover.data()),
                       png ? QStringLiteral("image/png") : QStringLiteral("image/jpeg")};
    }
    return std::nullopt;
}

}

Scanner::Scanner(const QString &artDir)
    : m_artDir(artDir)
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

ScanResult Scanner::scan(const QStringList &roots, const QHash<QString, FileStamp> &snapshot)
{
    m_art.clear();
    m_folderArt.clear();

    ScanResult result;
    QSet<QString> seen;
    for (const QString &root : roots) {
        QDirIterator it(root, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
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
        if (!seen.contains(it.key()))
            result.removed.append(it.key());
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
    if (!ref.isNull()) {
        if (const TagLib::Tag *tag = ref.tag()) {
            track.title = fromTag(tag->title());
            track.artist = fromTag(tag->artist());
            track.album = fromTag(tag->album());
            track.genre = fromTag(tag->genre());
            track.trackNo = static_cast<int>(tag->track());
            track.year = static_cast<int>(tag->year());
        }
        if (const TagLib::AudioProperties *audio = ref.audioProperties())
            track.durationMs = audio->lengthInMilliseconds();
        if (TagLib::File *file = ref.file()) {
            TagLib::PropertyMap map = file->properties();
            if (map.contains("ALBUMARTIST") && !map["ALBUMARTIST"].isEmpty())
                track.albumArtist = fromTag(map["ALBUMARTIST"].front());
            if (map.contains("DISCNUMBER") && !map["DISCNUMBER"].isEmpty())
                track.discNo = leadingInt(fromTag(map["DISCNUMBER"].front()));
        }
    }

    if (track.title.isEmpty())
        track.title = info.completeBaseName();
    if (track.artist.isEmpty())
        track.artist = QStringLiteral("unknown artist");
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
            path = writeArt(key, picture->data, picture->mime);
    }
    if (path.isEmpty())
        path = folderArt(dir);
    if (!path.isEmpty())
        m_art.insert(key, path);
    return path;
}

QString Scanner::writeArt(const QString &key, const QByteArray &data, const QString &mime)
{
    if (data.isEmpty())
        return {};
    const bool png = mime.compare(QLatin1StringView("image/png"), Qt::CaseInsensitive) == 0;
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
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
