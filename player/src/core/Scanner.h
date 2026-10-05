#pragma once

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>

class QDir;
class QFileInfo;

namespace TagLib {
class File;
}

namespace casdap {

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
    QString artPath;
};

struct ScanResult
{
    QList<TrackInfo> added;
    QList<TrackInfo> updated;
    QStringList removed;
};

class Scanner
{
public:
    explicit Scanner(const QString &artDir);

    ScanResult scan(const QStringList &roots, const QHash<QString, FileStamp> &snapshot,
                    const std::atomic_bool *cancel = nullptr);

    static bool isAudioFile(const QString &path);
    static QString albumKey(const QString &album, const QString &albumArtist);

private:
    TrackInfo read(const QFileInfo &info, const FileStamp &stamp);
    QString resolveArt(TagLib::File *file, const QString &key, const QDir &dir);
    QString writeArt(const QByteArray &data, const QString &mime);
    QString folderArt(const QDir &dir);

    QString m_artDir;
    QHash<QString, QString> m_art;
    QHash<QString, QString> m_folderArt;
};

}
