#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <optional>

class QDir;
class QFileInfo;

namespace microdap {

struct FileStamp
{
    qint64 mtime = 0;
    qint64 size = 0;
};

struct TrackInfo
{
    QString path;
    qint64 mtime = 0;
    qint64 size = 0;
    QString title;
    QString artist;
    QStringList artists;
    QString albumArtist;
    QString album;
    QString genre;
    int trackNo = 0;
    int discNo = 0;
    int year = 0;
    int durationMs = 0;
    int sampleRate = 0;
    int bitDepth = 0;
    QString artPath;
    QString trackArtPath;
    std::optional<double> trackGain;
    std::optional<double> albumGain;
    std::optional<double> trackPeak;
    std::optional<double> albumPeak;
    bool hasLyrics = false;
};

struct PlaylistFile
{
    QString source;
    QString name;
    QStringList entries;
    QStringList tails;
    qint64 mtime = -1;
    qint64 size = -1;
    bool readable = true;
};

struct ScanResult
{
    QList<TrackInfo> added;
    QList<TrackInfo> updated;
    QStringList removed;
    QList<PlaylistFile> playlists;
    QStringList keptPlaylists;
    QStringList removedPlaylists;
    QStringList directories;
    bool complete = false;
};

class Scanner
{
public:
    using MountCheck = bool (*)(const QString &path);
    using DeviceCheck = QString (*)(const QString &mount);

    explicit Scanner(const QString &artDir);

    ScanResult scan(const QStringList &roots, const QHash<QString, FileStamp> &snapshot,
                    const std::atomic_bool *cancel = nullptr,
                    const QHash<QString, FileStamp> &playlistStamps = {});

    static bool isAudioFile(const QString &path);
    static bool isPlaylistFile(const QString &path);
    static bool isFavoritesFile(const QString &path);
    static QString albumKey(const QString &album, const QString &albumArtist);
    static QString mediaBase();
    static FileStamp stampOf(const QFileInfo &info);
    static PlaylistFile readPlaylist(const QString &path);
    static QString decodePlaylist(const QByteArray &bytes, bool utf8Only);
    static bool isMountPoint(const QString &dir);
    static QString mountOf(const QString &root);
    static void setMountCheck(MountCheck check);
    static QString deviceOf(const QString &mount);
    static void setDeviceCheck(DeviceCheck check);

private:
    bool removable(const QString &path, const QStringList &present, const QStringList &absent);
    bool onAbsentMedia(const QString &path);
    bool mounted(const QString &dir);

    TrackInfo read(const QFileInfo &info, const FileStamp &stamp);
    QString resolveArt(const QString &trackArt, const QString &key, const QDir &dir);
    QString writeArt(const QByteArray &data, const QString &mime);
    QString folderArt(const QDir &dir);
    const QHash<QString, QString> &listing(const QDir &dir);

    QString m_artDir;
    QHash<QString, QString> m_art;
    QHash<QString, QString> m_folderArt;
    QHash<QString, QHash<QString, QString>> m_listing;
    QHash<QString, bool> m_mounts;
};

}
