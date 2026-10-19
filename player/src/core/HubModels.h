#pragma once

#include <QAbstractListModel>
#include <QByteArray>
#include <QHash>
#include <QList>
#include <QMetaObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <functional>

class QFont;

namespace microdap {

class Library;

class HubSource
{
public:
    void attach(QAbstractItemModel *model);
    QAbstractItemModel *model() const;
    int count() const;
    QVariant id(int row) const;
    QString title(int row) const;
    QString subtitle(int row) const;
    QString artPath(int row) const;
    QString letter(int row) const;
    void ensureLoaded() const;

    static QString letterOf(const QString &title);

private:
    QVariant value(int row, int role) const;

    QPointer<QAbstractItemModel> m_model;
    int m_idRole = -1;
    int m_titleRole = -1;
    int m_nameRole = -1;
    int m_subtitleRole = -1;
    int m_artPathRole = -1;
    int m_sectionRole = -1;
};

class SectionedModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(QString kind READ kind WRITE setKind NOTIFY kindChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        LetterRole,
        IdRole,
        TitleRole,
        SubtitleRole,
        ArtPathRole,
        NumberRole,
        SourceRowRole,
    };
    Q_ENUM(Role)

    explicit SectionedModel(QObject *parent = nullptr);

    QAbstractItemModel *source() const;
    void setSource(QAbstractItemModel *source);
    QString kind() const;
    void setKind(const QString &kind);
    bool active() const;
    void setActive(bool active);
    int count() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool isFocusable(int row) const;
    Q_INVOKABLE int nearestFocusable(int from, int dir) const;
    Q_INVOKABLE QStringList letters() const;
    Q_INVOKABLE int rowOfLetter(const QString &letter) const;
    Q_INVOKABLE int firstRowOfLetter(const QString &letter) const;
    Q_INVOKABLE int rowForSource(int sourceRow) const;

signals:
    void sourceChanged();
    void kindChanged();
    void activeChanged();
    void countChanged();

private:
    struct Row
    {
        int source = -1;
        int group = 0;
    };

    struct Group
    {
        QString letter;
        int divider = 0;
        int size = 0;
    };

    void rebuild();
    void build();
    void reindex();
    void connectSource();
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onRowsRemoved(const QModelIndex &parent, int first, int last);
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    void emitNumbersFrom(int row);

    HubSource m_source;
    QString m_kind = QStringLiteral("row");
    bool m_active = false;
    int m_lastCount = 0;
    QList<Row> m_rows;
    QList<Group> m_groups;
    QStringList m_letters;
    QHash<QString, int> m_letterGroup;
    QList<int> m_sourceToRow;
    QList<QMetaObject::Connection> m_connections;
};

class AlbumGridModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QAbstractItemModel *source READ source WRITE setSource NOTIFY sourceChanged)
    Q_PROPERTY(int columns READ columns WRITE setColumns NOTIFY columnsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        CellsRole = Qt::UserRole + 1,
    };
    Q_ENUM(Role)

    explicit AlbumGridModel(QObject *parent = nullptr);

    QAbstractItemModel *source() const;
    void setSource(QAbstractItemModel *source);
    int columns() const;
    void setColumns(int columns);
    bool active() const;
    void setActive(bool active);
    int count() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantList cells(int row) const;
    Q_INVOKABLE QVariantMap cellAt(int row, int col) const;
    Q_INVOKABLE int cellCount(int row) const;
    Q_INVOKABLE QStringList letters() const;
    Q_INVOKABLE QVariantMap cellOfLetter(const QString &letter) const;
    Q_INVOKABLE int rowOfLetter(const QString &letter) const;
    Q_INVOKABLE QVariantMap cellOfAlbum(qint64 id) const;
    Q_INVOKABLE int rowOfAlbum(qint64 id) const;

signals:
    void sourceChanged();
    void columnsChanged();
    void activeChanged();
    void countChanged();

private:
    struct Cell
    {
        int source = -1;
        QString letter;
    };

    void rebuild();
    void build();
    void connectSource();
    void onDataChanged(const QModelIndex &topLeft, const QModelIndex &bottomRight);
    QVariantMap cellMap(int cell) const;
    QVariantMap position(int cell) const;
    int gridRows() const;

    HubSource m_source;
    int m_columns = 3;
    bool m_active = false;
    int m_lastCount = 0;
    QList<Cell> m_cells;
    QStringList m_letters;
    QHash<QString, int> m_letterCell;
    QList<int> m_sourceToCell;
    QHash<qint64, int> m_idToCell;
    QList<QMetaObject::Connection> m_connections;
};

class FolderModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QStringList roots READ roots WRITE setRoots NOTIFY rootsChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int folder READ folder NOTIFY folderChanged)

public:
    enum Role {
        KindRole = Qt::UserRole + 1,
        LetterRole,
        IdRole,
        TitleRole,
        SubtitleRole,
        ArtPathRole,
        NumberRole,
        SourceRowRole,
    };
    Q_ENUM(Role)

    using Advance = std::function<qreal(const QString &)>;

    explicit FolderModel(Library *library, QObject *parent = nullptr);

    QStringList roots() const;
    void setRoots(const QStringList &roots);
    bool active() const;
    void setActive(bool active);
    int count() const;
    int folder() const;

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariantMap get(int row) const;
    Q_INVOKABLE bool isFocusable(int row) const;
    Q_INVOKABLE int nearestFocusable(int from, int dir) const;
    Q_INVOKABLE bool openFolder(int id);
    Q_INVOKABLE int up();
    Q_INVOKABLE QVariantList tracks(int id) const;
    Q_INVOKABLE QString name(int id) const;
    Q_INVOKABLE QString pathLine(const QFont &font, qreal width) const;
    QStringList pathParts() const;

    static QString fitPath(const QStringList &parts, const Advance &advance, qreal width);
    static bool folderLess(const QString &a, const QString &b);
    static QString rootLabel(const QString &root);

signals:
    void rootsChanged();
    void activeChanged();
    void countChanged();
    void folderChanged();

private:
    struct Song
    {
        qint64 id = -1;
        QString file;
        QString title;
        QString artist;
    };

    struct Dir
    {
        QString name;
        QString path;
        int parent = -1;
        QList<int> dirs;
        QList<Song> songs;
        int total = 0;
    };

    struct Row
    {
        QString kind;
        qint64 id = -1;
        QString title;
        QString subtitle;
        QString number;
    };

    void rebuild();
    void build();
    void fillRows();
    void collect(int dir, QVariantList &out) const;
    Row folderRow(int dir) const;
    void setOpen(int dir);

    Library *m_library = nullptr;
    QStringList m_roots;
    bool m_active = false;
    bool m_built = false;
    int m_open = -1;
    QString m_openPath;
    QList<Dir> m_dirs;
    QList<int> m_top;
    QList<Row> m_rows;
};

}
