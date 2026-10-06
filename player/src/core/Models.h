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

public slots:
    void reload();

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

    static void classify(Entry &entry, bool ignoreThe);
    static void sortEntries(QList<Entry> &entries);
    static QString countLabel(int count, const char *singular, const char *plural);

private:
    Library *m_library;
    QByteArray m_titleRole;
    QList<Entry> m_entries;
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

public:
    enum Mode {
        Recent,
        New,
    };
    Q_ENUM(Mode)

    explicit HistoryModel(Library *library, QObject *parent = nullptr);

    Mode mode() const;
    void setMode(Mode mode);

signals:
    void modeChanged();

protected:
    QList<Entry> load() const override;

private:
    Mode m_mode = Recent;
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

}
