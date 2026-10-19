#include "Scanner.h"

#include "PlaylistStore.h"

#include <taglib/apeproperties.h>
#include <taglib/audioproperties.h>
#include <taglib/dsdiffproperties.h>
#include <taglib/dsfproperties.h>
#include <taglib/fileref.h>
#include <taglib/flacproperties.h>
#include <taglib/mp4properties.h>
#include <taglib/opusfile.h>
#include <taglib/tag.h>
#include <taglib/tbytevector.h>
#include <taglib/tfile.h>
#include <taglib/tlist.h>
#include <taglib/tmap.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>
#include <taglib/tvariant.h>
#include <taglib/wavproperties.h>

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QStorageInfo>
#include <QStringDecoder>
#include <QTimeZone>
#include <QUrl>

#include <algorithm>
#include <optional>
#include <utility>

namespace microdap {

namespace {

struct Picture
{
    QByteArray data;
    QString mime;
};

const QSet<QString> &extensions()
{
    static const QSet<QString> set{
        QStringLiteral("flac"), QStringLiteral("mp3"), QStringLiteral("m4a"),  QStringLiteral("mp4"),
        QStringLiteral("ogg"),  QStringLiteral("oga"), QStringLiteral("opus"), QStringLiteral("wav"),
        QStringLiteral("ape"),  QStringLiteral("dsf"), QStringLiteral("dff"),
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

int bitDepthOf(const TagLib::AudioProperties *audio)
{
    if (const auto *flac = dynamic_cast<const TagLib::FLAC::Properties *>(audio))
        return flac->bitsPerSample();
    if (const auto *mp4 = dynamic_cast<const TagLib::MP4::Properties *>(audio))
        return mp4->bitsPerSample();
    if (const auto *wav = dynamic_cast<const TagLib::RIFF::WAV::Properties *>(audio))
        return wav->bitsPerSample();
    if (const auto *ape = dynamic_cast<const TagLib::APE::Properties *>(audio))
        return ape->bitsPerSample();
    if (const auto *dsf = dynamic_cast<const TagLib::DSF::Properties *>(audio))
        return dsf->bitsPerSample();
    if (const auto *dff = dynamic_cast<const TagLib::DSDIFF::Properties *>(audio))
        return dff->bitsPerSample();
    return 0;
}

constexpr double gainLimitDb = 51;
constexpr double r128Offset = 5;

std::optional<double> leadingDecimal(TagLib::PropertyMap &map, const char *key)
{
    if (!map.contains(key) || map[key].isEmpty())
        return std::nullopt;
    static const QRegularExpression pattern(QStringLiteral("^[+-]?([0-9]+([.,][0-9]*)?|[.,][0-9]+)"));
    const QRegularExpressionMatch match = pattern.match(fromTag(map[key].front()));
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double value = match.captured(0).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

std::optional<double> gainOf(std::optional<double> db)
{
    if (!db || *db < -gainLimitDb || *db > gainLimitDb)
        return std::nullopt;
    return db;
}

std::optional<double> gainOf(TagLib::PropertyMap &map, const char *replayGain, const char *r128, bool opus)
{
    if (opus) {
        if (const std::optional<double> q78 = leadingDecimal(map, r128))
            return gainOf(*q78 / 256 + r128Offset);
    }
    return gainOf(leadingDecimal(map, replayGain));
}

std::optional<double> peakOf(TagLib::PropertyMap &map, const char *key)
{
    const std::optional<double> peak = leadingDecimal(map, key);
    return peak && *peak > 0 ? peak : std::nullopt;
}

bool hasLyricsTag(TagLib::PropertyMap &map)
{
    for (const char *key : {"LYRICS", "UNSYNCEDLYRICS"}) {
        if (map.contains(key) && !map[key].isEmpty() && !map[key].front().isEmpty())
            return true;
    }
    return false;
}

std::atomic<Scanner::MountCheck> mountCheck{nullptr};
std::atomic<Scanner::DeviceCheck> deviceCheck{nullptr};

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

bool Scanner::isPlaylistFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    return suffix == QLatin1StringView("m3u") || suffix == QLatin1StringView("m3u8");
}

QString Scanner::albumKey(const QString &album, const QString &albumArtist)
{
    return album + QLatin1Char('\n') + albumArtist;
}

QString Scanner::mediaBase()
{
    return qEnvironmentVariable("MICRODAP_MEDIA_ROOT", QStringLiteral("/run/media"));
}

FileStamp Scanner::stampOf(const QFileInfo &info)
{
    return {info.lastModified(QTimeZone::UTC).toMSecsSinceEpoch(), info.size()};
}

bool Scanner::isMountPoint(const QString &dir)
{
    if (const MountCheck check = mountCheck.load())
        return check(dir);
    const QString path = QDir::cleanPath(dir);
    if (!QFileInfo(path).isDir())
        return false;
    const QStorageInfo storage(path);
    return storage.isValid() && QDir::cleanPath(storage.rootPath()) == path;
}

void Scanner::setMountCheck(MountCheck check)
{
    mountCheck.store(check);
}

QString Scanner::deviceOf(const QString &mount)
{
    if (const DeviceCheck check = deviceCheck.load())
        return check(mount);
    return QString::fromLocal8Bit(QStorageInfo(mount).device());
}

void Scanner::setDeviceCheck(DeviceCheck check)
{
    deviceCheck.store(check);
}

QString Scanner::decodePlaylist(const QByteArray &bytes, bool utf8Only)
{
    QByteArray data = bytes;
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    if (utf8Only)
        return QString::fromUtf8(data);
    QStringDecoder decoder(QStringDecoder::Utf8);
    const QString text = decoder(data);
    return decoder.hasError() ? QString::fromLatin1(data) : text;
}

PlaylistFile Scanner::readPlaylist(const QString &path)
{
    const QFileInfo info(path);
    PlaylistFile playlist;
    playlist.source = info.absoluteFilePath();
    playlist.name = info.completeBaseName();
    const FileStamp stamp = stampOf(info);
    playlist.mtime = stamp.mtime;
    playlist.size = stamp.size;

    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        playlist.readable = false;
        return playlist;
    }
    const bool utf8Only = info.suffix().compare(QLatin1StringView("m3u8"), Qt::CaseInsensitive) == 0;
    const QString text = decodePlaylist(file.readAll(), utf8Only);
    const QString folder = info.absolutePath();
    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        QString entry = line;
        if (entry.startsWith(QLatin1StringView("file:"), Qt::CaseInsensitive))
            entry = QUrl(entry).toLocalFile();
        entry.replace(QLatin1Char('\\'), QLatin1Char('/'));
        if (entry.isEmpty()) {
            playlist.entries.append(QString());
            continue;
        }
        if (!QDir::isAbsolutePath(entry))
            entry = folder + QLatin1Char('/') + entry;
        playlist.entries.append(QDir::cleanPath(entry));
    }
    return playlist;
}

bool Scanner::mounted(const QString &dir)
{
    if (const auto it = m_mounts.constFind(dir); it != m_mounts.cend())
        return *it;
    const bool mount = isMountPoint(dir);
    m_mounts.insert(dir, mount);
    return mount;
}

bool Scanner::onAbsentMedia(const QString &path)
{
    const QString base = rootPrefix(mediaBase());
    if (!path.startsWith(base))
        return false;
    QString dir = QDir::cleanPath(QFileInfo(path).absolutePath());
    while (dir.startsWith(base)) {
        if (mounted(dir))
            return false;
        const QString parent = QFileInfo(dir).path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return true;
}

QString Scanner::mountOf(const QString &root)
{
    const QString base = rootPrefix(mediaBase());
    QString dir = QDir::cleanPath(QDir(root).absolutePath());
    while (dir.startsWith(base)) {
        if (isMountPoint(dir))
            return dir;
        const QString parent = QFileInfo(dir).path();
        if (parent == dir)
            break;
        dir = parent;
    }
    return {};
}

bool Scanner::removable(const QString &path, const QStringList &present, const QStringList &absent)
{
    const auto under = [&path](const QStringList &prefixes) {
        return std::any_of(prefixes.cbegin(), prefixes.cend(),
                           [&path](const QString &prefix) { return path.startsWith(prefix); });
    };
    if (onAbsentMedia(path))
        return false;
    return under(present) || !under(absent);
}

ScanResult Scanner::scan(const QStringList &roots, const QHash<QString, FileStamp> &snapshot,
                         const std::atomic_bool *cancel,
                         const QHash<QString, FileStamp> &playlistStamps)
{
    m_art.clear();
    m_folderArt.clear();
    m_listing.clear();
    m_mounts.clear();

    struct Walked
    {
        QString root;
        QString mount;
        QString device;
    };

    ScanResult result;
    QList<Walked> walkedRoots;
    QSet<QString> seen;
    QSet<QString> seenPlaylists;
    QSet<QString> walked;
    QStringList present;
    QStringList absent;
    const auto takePlaylist = [&](const QFileInfo &info) {
        const QString path = info.absoluteFilePath();
        if (seenPlaylists.contains(path))
            return;
        seenPlaylists.insert(path);
        const FileStamp stamp = stampOf(info);
        const auto known = playlistStamps.constFind(path);
        if (known != playlistStamps.cend() && known->mtime == stamp.mtime && known->size == stamp.size)
            result.keptPlaylists.append(path);
        else
            result.playlists.append(readPlaylist(path));
    };
    for (const QString &root : roots) {
        if (root.isEmpty())
            continue;
        const QFileInfo rootInfo(root);
        if (!rootInfo.isDir() || !rootInfo.isReadable()) {
            absent.append(rootPrefix(root));
            continue;
        }
        present.append(rootPrefix(root));
        const QString mount = mountOf(root);
        walkedRoots.append({root, mount,
                            mount.isEmpty() ? QString::fromLocal8Bit(QStorageInfo(root).device()) : deviceOf(mount)});

        QStringList queue{QDir(root).absolutePath()};
        for (qsizetype next = 0; next < queue.size(); ++next) {
            const QString dirPath = queue.at(next);
            if (walked.contains(dirPath))
                continue;
            walked.insert(dirPath);
            result.directories.append(dirPath);

            const QFileInfoList entries = QDir(dirPath).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
            for (const QFileInfo &info : entries) {
                if (cancelled(cancel))
                    return ScanResult();
                const QString path = info.absoluteFilePath();
                if (info.isDir()) {
                    if (!info.isSymLink() && info.isReadable())
                        queue.append(path);
                    continue;
                }
                if (isPlaylistFile(path)) {
                    takePlaylist(info);
                    continue;
                }
                if (!info.isReadable() || !isAudioFile(path) || seen.contains(path))
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

        const QString lists = mount.isEmpty() ? QString() : PlaylistStore::playlistsDir(mount);
        if (lists.isEmpty() || walked.contains(lists) || !QFileInfo(lists).isDir())
            continue;
        walked.insert(lists);
        result.directories.append(lists);
        const QFileInfoList files = QDir(lists).entryInfoList(QDir::Files | QDir::Readable, QDir::Name);
        for (const QFileInfo &info : files) {
            if (cancelled(cancel))
                return ScanResult();
            if (isPlaylistFile(info.absoluteFilePath()))
                takePlaylist(info);
        }
    }

    for (const Walked &walkedRoot : std::as_const(walkedRoots)) {
        const bool same = walkedRoot.mount.isEmpty()
                              ? QString::fromLocal8Bit(QStorageInfo(walkedRoot.root).device()) == walkedRoot.device
                              : isMountPoint(walkedRoot.mount) && QFileInfo::exists(walkedRoot.device)
                                    && deviceOf(walkedRoot.mount) == walkedRoot.device;
        if (same && QFileInfo(walkedRoot.root).isDir())
            continue;
        present.removeAll(rootPrefix(walkedRoot.root));
        absent.append(rootPrefix(walkedRoot.root));
    }
    for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
        if (!seen.contains(it.key()) && removable(it.key(), present, absent))
            result.removed.append(it.key());
    }
    for (auto it = playlistStamps.cbegin(); it != playlistStamps.cend(); ++it) {
        if (!seenPlaylists.contains(it.key()) && removable(it.key(), present, absent))
            result.removedPlaylists.append(it.key());
    }
    if (!result.added.isEmpty()) {
        for (const QString &source : std::as_const(result.keptPlaylists)) {
            if (cancelled(cancel))
                return ScanResult();
            result.playlists.append(readPlaylist(source));
        }
        result.keptPlaylists.clear();
    }
    result.complete = true;
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
        if (const TagLib::AudioProperties *audio = ref.audioProperties()) {
            track.durationMs = audio->lengthInMilliseconds();
            track.sampleRate = audio->sampleRate();
            track.bitDepth = bitDepthOf(audio);
        }
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
            const bool opus = dynamic_cast<TagLib::Ogg::Opus::File *>(file) != nullptr;
            track.trackGain = gainOf(map, "REPLAYGAIN_TRACK_GAIN", "R128_TRACK_GAIN", opus);
            track.albumGain = gainOf(map, "REPLAYGAIN_ALBUM_GAIN", "R128_ALBUM_GAIN", opus);
            track.trackPeak = peakOf(map, "REPLAYGAIN_TRACK_PEAK");
            track.albumPeak = peakOf(map, "REPLAYGAIN_ALBUM_PEAK");
            track.hasLyrics = hasLyricsTag(map);
            if (const std::optional<Picture> picture = embeddedPicture(file))
                track.trackArtPath = writeArt(picture->data, picture->mime);
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

    if (!track.hasLyrics)
        track.hasLyrics = listing(info.dir()).contains((info.completeBaseName() + QStringLiteral(".lrc")).toLower());
    track.artPath = resolveArt(track.trackArtPath, albumKey(track.album, track.albumArtist), info.dir());
    return track;
}

QString Scanner::resolveArt(const QString &trackArt, const QString &key, const QDir &dir)
{
    if (const auto it = m_art.constFind(key); it != m_art.cend())
        return *it;

    QString path = trackArt;
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

    const QHash<QString, QString> &lower = listing(dir);
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

const QHash<QString, QString> &Scanner::listing(const QDir &dir)
{
    const QString dirPath = dir.absolutePath();
    if (const auto it = m_listing.constFind(dirPath); it != m_listing.cend())
        return *it;
    QHash<QString, QString> lower;
    const QStringList entries = dir.entryList(QDir::Files | QDir::Readable);
    for (const QString &entry : entries)
        lower.insert(entry.toLower(), entry);
    return *m_listing.insert(dirPath, lower);
}

}
