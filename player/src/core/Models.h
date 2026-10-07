#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QString>
#include <QVariantMap>

namespace casdap {

class Library;

class LibraryModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        ArtPathRole,
        SectionRole,
        KindRole,
    };
    Q_ENUM(Role)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE int indexOf(qint64 id) const;
    Q_INVOKABLE void ensureLoaded();
    bool isLoaded() const;

public slots:
    void reload();
    void refresh();

protected:
    struct Entry
    {
        qint64 id = 0;
        QString title;
        QString subtitle;
        QString artPath;
        QString section;
        QString sortKey;
        QString kind;
    };

    LibraryModel(Library *library, const QByteArray &titleRole, QObject *parent);

    virtual QList<Entry> load() const = 0;
    Library *library() const;
    QList<Entry> &rows();

    static void classify(Entry &entry, bool ignoreThe);
    static void sortEntries(QList<Entry> &entries);
    static QString countLabel(int count, const char *singular, const char *plural);

private:
    Library *m_library;
    QByteArray m_titleRole;
    QList<Entry> m_entries;
    bool m_loaded = false;
};

class ArtistsModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit ArtistsModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;
};

class AlbumsModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit AlbumsModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;
};

class SongsModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit SongsModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;
};

class GenresModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit GenresModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;
};

class HistoryModel : public LibraryModel
{
    Q_OBJECT
    Q_PROPERTY(Mode mode READ mode WRITE setMode NOTIFY modeChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)

public:
    enum Mode {
        Recent,
        New,
    };
    Q_ENUM(Mode)

    explicit HistoryModel(Library *library, QObject *parent = nullptr);
    HistoryModel(Library *library, Mode mode, int limit, QObject *parent = nullptr);

    Mode mode() const;
    void setMode(Mode mode);
    int limit() const;
    void setLimit(int limit);

signals:
    void modeChanged();
    void limitChanged();

protected:
    QList<Entry> load() const override;

private:
    void onPlayed(qint64 trackId);
    bool readTrack(qint64 trackId, Entry &entry) const;

    Mode m_mode = Recent;
    int m_limit = 0;
};

class PinsModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit PinsModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;

private:
    bool loadAlbum(Entry &entry) const;
    bool loadArtist(Entry &entry) const;
    bool loadSong(Entry &entry) const;
    bool loadPlaylist(Entry &entry) const;
};

class PlaylistsModel : public LibraryModel
{
    Q_OBJECT

public:
    explicit PlaylistsModel(Library *library, QObject *parent = nullptr);

protected:
    QList<Entry> load() const override;
};

}
