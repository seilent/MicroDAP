#include "HubModels.h"
#include "Library.h"
#include "Scanner.h"
#include "SectionKey.h"

#include <QAbstractListModel>
#include <QDir>
#include <QElapsedTimer>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace microdap;

namespace {

struct Item
{
    qint64 id = 0;
    QString title;
    QString subtitle;
    QString artPath;
    QString section;
};

class FakeSource : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        SubtitleRole,
        ArtPathRole,
        SectionRole,
    };

    explicit FakeSource(QList<Item> items = {}, QByteArray titleRole = QByteArrayLiteral("title"))
        : m_items(std::move(items))
        , m_titleRole(std::move(titleRole))
    {
    }

    int rowCount(const QModelIndex &parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : static_cast<int>(m_items.size());
    }

    QVariant data(const QModelIndex &index, int role) const override
    {
        ++dataCalls;
        if (!index.isValid() || index.row() >= m_items.size())
            return {};
        const Item &item = m_items.at(index.row());
        switch (role) {
        case IdRole:
            return item.id;
        case TitleRole:
            return item.title;
        case SubtitleRole:
            return item.subtitle;
        case ArtPathRole:
            return item.artPath;
        case SectionRole:
            return item.section;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {IdRole, QByteArrayLiteral("id")},
            {TitleRole, m_titleRole},
            {SubtitleRole, QByteArrayLiteral("subtitle")},
            {ArtPathRole, QByteArrayLiteral("artPath")},
            {SectionRole, QByteArrayLiteral("section")},
        };
    }

    Q_INVOKABLE void ensureLoaded()
    {
        ++loads;
    }

    void insert(int row, const QList<Item> &items)
    {
        beginInsertRows(QModelIndex(), row, row + static_cast<int>(items.size()) - 1);
        for (qsizetype i = 0; i < items.size(); ++i)
            m_items.insert(row + i, items.at(i));
        endInsertRows();
    }

    void remove(int row, int count)
    {
        beginRemoveRows(QModelIndex(), row, row + count - 1);
        m_items.remove(row, count);
        endRemoveRows();
    }

    void set(int row, const Item &item)
    {
        m_items[row] = item;
        emit dataChanged(index(row), index(row));
    }

    void reset(const QList<Item> &items)
    {
        beginResetModel();
        m_items = items;
        endResetModel();
    }

    const QList<Item> &items() const
    {
        return m_items;
    }

    mutable int dataCalls = 0;
    int loads = 0;

private:
    QList<Item> m_items;
    QByteArray m_titleRole;
};

Item item(qint64 id, const QString &title, const QString &section)
{
    return {id, title, QStringLiteral("sub %1").arg(id), QStringLiteral("/art/%1.jpg").arg(id), section};
}

QList<Item> sample()
{
    return {
        item(10, QStringLiteral("1st"), QStringLiteral("#")),
        item(11, QStringLiteral("2nd"), QStringLiteral("#")),
        item(12, QStringLiteral("alpha"), QStringLiteral("A")),
        item(13, QStringLiteral("apple"), QStringLiteral("A")),
        item(14, QStringLiteral("beta"), QStringLiteral("B")),
    };
}

QString jsLetterOf(const QString &title)
{
    const QString key = title.trimmed().toLower();
    if (key.isEmpty())
        return QStringLiteral("#");
    const QChar c = key.at(0);
    return c >= u'a' && c <= u'z' ? QString(c) : QStringLiteral("#");
}

QString jsLetter(const Item &item)
{
    return item.section.isEmpty() ? jsLetterOf(item.title) : item.section.toLower();
}

QList<QVariantMap> jsBuildList(const QList<Item> &items, const QString &kind)
{
    QList<QVariantMap> rows;
    QString last;
    for (qsizetype i = 0; i < items.size(); ++i) {
        const Item &e = items.at(i);
        const QString letter = jsLetter(e);
        if (letter != last) {
            rows.append({{QStringLiteral("kind"), QStringLiteral("divider")},
                         {QStringLiteral("letter"), letter},
                         {QStringLiteral("id"), qint64(-1)},
                         {QStringLiteral("title"), letter},
                         {QStringLiteral("subtitle"), QString()},
                         {QStringLiteral("artPath"), QString()},
                         {QStringLiteral("sourceRow"), -1}});
            last = letter;
        }
        rows.append({{QStringLiteral("kind"), kind},
                     {QStringLiteral("letter"), letter},
                     {QStringLiteral("id"), e.id},
                     {QStringLiteral("title"), e.title},
                     {QStringLiteral("subtitle"), e.subtitle},
                     {QStringLiteral("artPath"), e.artPath},
                     {QStringLiteral("row"), static_cast<int>(i)},
                     {QStringLiteral("number"), QString::number(i + 1)},
                     {QStringLiteral("sourceRow"), static_cast<int>(i)}});
    }
    return rows;
}

QStringList kinds(const SectionedModel &model)
{
    QStringList out;
    for (int r = 0; r < model.rowCount(); ++r)
        out.append(model.data(model.index(r), SectionedModel::KindRole).toString());
    return out;
}

QString gridShape(const AlbumGridModel &model)
{
    QStringList rows;
    for (int r = 0; r < model.rowCount(); ++r) {
        QStringList cells;
        for (const QVariant &v : model.data(model.index(r), AlbumGridModel::CellsRole).toList()) {
            const QVariantMap cell = v.toMap();
            cells.append(cell.value(QStringLiteral("kind")).toString() == QLatin1StringView("letter")
                             ? QStringLiteral("L") + cell.value(QStringLiteral("letter")).toString()
                             : cell.value(QStringLiteral("id")).toString());
        }
        rows.append(cells.join(QLatin1Char(' ')));
    }
    return rows.join(QStringLiteral(" | "));
}

QList<Item> synthetic(int n)
{
    QList<Item> items;
    items.reserve(n);
    const int hashes = n / 20;
    for (int i = 0; i < hashes; ++i)
        items.append(item(i + 1, QStringLiteral("%1 title").arg(i), QStringLiteral("#")));
    const int rest = n - hashes;
    for (int l = 0; l < 26; ++l) {
        const int from = rest * l / 26;
        const int to = rest * (l + 1) / 26;
        const QChar c(char16_t(u'a' + l));
        for (int i = from; i < to; ++i)
            items.append(item(hashes + i + 1, QStringLiteral("%1 title %2").arg(c).arg(i), QString(c.toUpper())));
    }
    return items;
}

template<typename Model>
qint64 medianBuild(Model &model)
{
    QList<qint64> runs;
    for (int i = 0; i < 5; ++i) {
        model.setActive(false);
        QElapsedTimer timer;
        timer.start();
        model.setActive(true);
        runs.append(timer.nsecsElapsed());
    }
    std::sort(runs.begin(), runs.end());
    return runs.at(2) / 1000000;
}

bool oneLevelMount(const QString &dir)
{
    return QDir::cleanPath(dir) == QStringLiteral("/media-test/CARDC");
}

bool insertTrack(Library &library, qint64 id, const QString &path, const QString &title)
{
    QSqlQuery query(library.database());
    query.prepare(QStringLiteral("INSERT INTO tracks(id, path, mtime, size, title, artist, album_artist, album_id, "
                                 "added) VALUES(?, ?, 0, 0, ?, 'Band', 'Band', 1, 0)"));
    query.addBindValue(id);
    query.addBindValue(path);
    query.addBindValue(title);
    return query.exec();
}

QStringList kinds(const FolderModel &model)
{
    QStringList out;
    for (int row = 0; row < model.count(); ++row)
        out.append(model.get(row).value(QStringLiteral("kind")).toString());
    return out;
}

QStringList titles(const FolderModel &model)
{
    QStringList out;
    for (int row = 0; row < model.count(); ++row)
        out.append(model.get(row).value(QStringLiteral("title")).toString());
    return out;
}

}

class HubModelsTest : public QObject
{
    Q_OBJECT

private slots:
    void foldersOrder();
    void foldersRoots();
    void foldersPathElision();
    void lazyUntilActive();
    void dividersAndNumbers();
    void matchesJsBuilder();
    void sectionFallback();
    void nameRole();
    void invokables();
    void insertIntoGroup();
    void insertAtGroupStart();
    void insertNewLetterResets();
    void removeFromGroup();
    void removeLastOfGroupResets();
    void dataChangedForwards();
    void sourceResetRebuilds();
    void inactiveDropsRows();
    void gridLazy();
    void gridPackingThree();
    void gridPackingSix();
    void gridColumnsReset();
    void gridInsertRepacks();
    void gridDataChanged();
    void sectionKeyFolding();
    void sectionKeyGroups();
    void sectionKeyOrder();
    void sectionLetterList();
    void kanaLetters();
    void gridPackingKana();
    void scaleSongs();
    void scaleAlbums();
    void scaleLookups();
};

void HubModelsTest::foldersOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Library library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")));
    QVERIFY(library.isOpen());
    QSqlQuery album(library.database());
    QVERIFY(album.exec(QStringLiteral("INSERT INTO albums(id, title, artist, added) VALUES(1, 'A', 'Band', 0)")));
    const QString music = QDir::cleanPath(QStandardPaths::writableLocation(QStandardPaths::MusicLocation));
    QVERIFY(insertTrack(library, 1, music + QStringLiteral("/b/02 Two.flac"), QStringLiteral("Two")));
    QVERIFY(insertTrack(library, 2, music + QStringLiteral("/B/01 One.flac"), QStringLiteral("One")));
    QVERIFY(insertTrack(library, 3, music + QStringLiteral("/a/x.flac"), QStringLiteral("X")));
    QVERIFY(insertTrack(library, 4, music + QStringLiteral("/z.flac"), QStringLiteral("Z")));
    QVERIFY(insertTrack(library, 5, music + QStringLiteral("/a/sub/y.flac"), QStringLiteral("Y")));
    QVERIFY(insertTrack(library, 6, music + QStringLiteral("/a/W.flac"), QStringLiteral("W")));
    QVERIFY(insertTrack(library, 7, QStringLiteral("/elsewhere/q.flac"), QStringLiteral("Q")));

    FolderModel model(&library);
    model.setRoots({music});
    QCOMPARE(model.count(), 0);
    QSignalSpy count(&model, &FolderModel::countChanged);
    model.setActive(true);
    QCOMPARE(count.count(), 1);
    QCOMPARE(model.folder(), -1);
    QCOMPARE(kinds(model), (QStringList{QStringLiteral("folder")}));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("internal")}));
    QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("6 songs"));
    QVERIFY(model.isFocusable(0));
    const int internal = model.get(0).value(QStringLiteral("id")).toInt();

    QVERIFY(model.openFolder(internal));
    QCOMPARE(model.folder(), internal);
    QCOMPARE(kinds(model), (QStringList{QStringLiteral("path"), QStringLiteral("folder"), QStringLiteral("folder"),
                                        QStringLiteral("folder"), QStringLiteral("song")}));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("internal"), QStringLiteral("a"), QStringLiteral("B"),
                                         QStringLiteral("b"), QStringLiteral("Z")}));
    QCOMPARE(model.get(1).value(QStringLiteral("subtitle")).toString(), QStringLiteral("3 songs"));
    QCOMPARE(model.get(2).value(QStringLiteral("subtitle")).toString(), QStringLiteral("1 song"));
    QCOMPARE(model.get(4).value(QStringLiteral("number")).toString(), QStringLiteral("1"));
    QCOMPARE(model.get(4).value(QStringLiteral("id")).toLongLong(), 4);
    QVERIFY(!model.isFocusable(0));
    QCOMPARE(model.nearestFocusable(0, 1), 1);
    QCOMPARE(model.nearestFocusable(0, -1), 1);
    QCOMPARE(model.tracks(internal), (QVariantList{qint64(5), qint64(6), qint64(3), qint64(2), qint64(1), qint64(4)}));
    QCOMPARE(model.name(internal), QStringLiteral("internal"));

    const int a = model.get(1).value(QStringLiteral("id")).toInt();
    QVERIFY(model.openFolder(a));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("internal / a"), QStringLiteral("sub"), QStringLiteral("W"),
                                         QStringLiteral("X")}));
    QCOMPARE(model.get(2).value(QStringLiteral("number")).toString(), QStringLiteral("1"));
    QCOMPARE(model.get(3).value(QStringLiteral("number")).toString(), QStringLiteral("2"));
    QCOMPARE(model.pathParts(), (QStringList{QStringLiteral("internal"), QStringLiteral("a")}));

    QVERIFY(insertTrack(library, 8, music + QStringLiteral("/a/v.flac"), QStringLiteral("V")));
    emit library.tracksChanged();
    QCOMPARE(model.pathParts(), (QStringList{QStringLiteral("internal"), QStringLiteral("a")}));
    QCOMPARE(model.count(), 5);

    QCOMPARE(model.up(), 1);
    QCOMPARE(model.folder(), internal);
    QCOMPARE(model.up(), 0);
    QCOMPARE(model.folder(), -1);
    QCOMPARE(model.up(), -1);

    QVERIFY(model.openFolder(a));
    QSqlQuery drop(library.database());
    QVERIFY(drop.exec(QStringLiteral("DELETE FROM tracks WHERE path LIKE '%/a/%'")));
    emit library.tracksChanged();
    QCOMPARE(model.pathParts(), (QStringList{QStringLiteral("internal")}));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("internal"), QStringLiteral("B"), QStringLiteral("b"),
                                         QStringLiteral("Z")}));
}

void HubModelsTest::foldersRoots()
{
    qputenv("MICRODAP_MEDIA_ROOT", "/media-test");
    Scanner::setMountCheck(oneLevelMount);
    const auto restore = qScopeGuard([] {
        Scanner::setMountCheck(nullptr);
        qunsetenv("MICRODAP_MEDIA_ROOT");
    });
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Library library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")));
    QVERIFY(library.isOpen());
    QSqlQuery album(library.database());
    QVERIFY(album.exec(QStringLiteral("INSERT INTO albums(id, title, artist, added) VALUES(1, 'A', 'Band', 0)")));
    QVERIFY(insertTrack(library, 1, QStringLiteral("/media-test/u/CARDB/Music/Live/1.flac"), QStringLiteral("One")));
    QVERIFY(insertTrack(library, 2, QStringLiteral("/media-test/u/CARDA/Music/2.flac"), QStringLiteral("Two")));
    QVERIFY(insertTrack(library, 3, QStringLiteral("/media-test/CARDC/roms/music/3.flac"), QStringLiteral("Three")));
    QVERIFY(insertTrack(library, 4, QStringLiteral("/data/tunes/4.flac"), QStringLiteral("Four")));

    QCOMPARE(FolderModel::rootLabel(QStringLiteral("/media-test/u/CARDA/Music")), QStringLiteral("CARDA Music"));
    QCOMPARE(FolderModel::rootLabel(QStringLiteral("/media-test/CARDC/roms/music")),
             QStringLiteral("CARDC roms/music"));
    QCOMPARE(FolderModel::rootLabel(QStringLiteral("/data/tunes")), QStringLiteral("tunes"));

    FolderModel model(&library);
    model.setActive(true);
    QCOMPARE(model.count(), 0);
    model.setRoots({QStringLiteral("/media-test/u/CARDA/Music"), QStringLiteral("/media-test/u/CARDB/Music/"),
                    QStringLiteral("/media-test/CARDC/roms/music"), QStringLiteral("/media-test/u/CARDD/Music"),
                    QStringLiteral("/data/tunes")});
    QCOMPARE(titles(model), (QStringList{QStringLiteral("CARDA Music"), QStringLiteral("CARDB Music"),
                                         QStringLiteral("CARDC roms/music"), QStringLiteral("tunes")}));
    QVERIFY(model.openFolder(model.get(1).value(QStringLiteral("id")).toInt()));
    QCOMPARE(titles(model), (QStringList{QStringLiteral("CARDB Music"), QStringLiteral("Live")}));
    QVERIFY(model.openFolder(model.get(1).value(QStringLiteral("id")).toInt()));
    QCOMPARE(model.pathParts(), (QStringList{QStringLiteral("CARDB Music"), QStringLiteral("Live")}));
    QCOMPARE(model.tracks(model.folder()), (QVariantList{qint64(1)}));
}

void HubModelsTest::foldersPathElision()
{
    const FolderModel::Advance advance = [](const QString &text) { return qreal(text.size()); };
    const QStringList parts{QStringLiteral("CARDA Music"), QStringLiteral("Live"), QStringLiteral("Encore Sessions"),
                            QStringLiteral("Bonus")};
    QCOMPARE(FolderModel::fitPath(parts, advance, 100), QStringLiteral("CARDA Music / Live / Encore Sessions / Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 44), QStringLiteral("CARDA Music / Live / Encore Sessions / Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 43), QStringLiteral("... / Live / Encore Sessions / Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 30), QStringLiteral("... / Encore Sessions / Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 11), QStringLiteral("... / Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 10), QStringLiteral("Bonus"));
    QCOMPARE(FolderModel::fitPath(parts, advance, 2), QStringLiteral("Bonus"));
    QCOMPARE(FolderModel::fitPath({QStringLiteral("internal")}, advance, 2), QStringLiteral("internal"));
    QCOMPARE(FolderModel::fitPath({}, advance, 100), QString());
    QVERIFY(FolderModel::folderLess(QStringLiteral("B"), QStringLiteral("b")));
    QVERIFY(!FolderModel::folderLess(QStringLiteral("b"), QStringLiteral("B")));
    QVERIFY(FolderModel::folderLess(QStringLiteral("a"), QStringLiteral("B")));
    QVERIFY(FolderModel::folderLess(QStringLiteral("B"), QStringLiteral("c")));
}

void HubModelsTest::lazyUntilActive()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(source.dataCalls, 0);
    QCOMPARE(source.loads, 0);
    QSignalSpy count(&model, &SectionedModel::countChanged);
    model.setActive(true);
    QCOMPARE(source.loads, 1);
    QCOMPARE(model.rowCount(), 8);
    QCOMPARE(model.count(), 8);
    QCOMPARE(count.count(), 1);
}

void HubModelsTest::dividersAndNumbers()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setKind(QStringLiteral("song"));
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(kinds(model), QStringList({QStringLiteral("divider"), QStringLiteral("song"), QStringLiteral("song"),
                                        QStringLiteral("divider"), QStringLiteral("song"), QStringLiteral("song"),
                                        QStringLiteral("divider"), QStringLiteral("song")}));
    const auto at = [&model](int row, int role) { return model.data(model.index(row), role); };
    QCOMPARE(at(0, SectionedModel::LetterRole).toString(), QStringLiteral("#"));
    QCOMPARE(at(0, SectionedModel::TitleRole).toString(), QStringLiteral("#"));
    QCOMPARE(at(0, SectionedModel::IdRole).toLongLong(), -1);
    QCOMPARE(at(0, SectionedModel::SourceRowRole).toInt(), -1);
    QCOMPARE(at(3, SectionedModel::TitleRole).toString(), QStringLiteral("a"));
    QCOMPARE(at(6, SectionedModel::LetterRole).toString(), QStringLiteral("b"));
    QCOMPARE(at(4, SectionedModel::TitleRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(at(4, SectionedModel::SubtitleRole).toString(), QStringLiteral("sub 12"));
    QCOMPARE(at(4, SectionedModel::ArtPathRole).toString(), QStringLiteral("/art/12.jpg"));
    QCOMPARE(at(4, SectionedModel::IdRole).toLongLong(), 12);
    QCOMPARE(at(4, SectionedModel::LetterRole).toString(), QStringLiteral("a"));
    QCOMPARE(at(1, SectionedModel::NumberRole).toString(), QStringLiteral("1"));
    QCOMPARE(at(4, SectionedModel::NumberRole).toString(), QStringLiteral("3"));
    QCOMPARE(at(7, SectionedModel::NumberRole).toString(), QStringLiteral("5"));
    QCOMPARE(at(7, SectionedModel::SourceRowRole).toInt(), 4);
    const QHash<int, QByteArray> names = model.roleNames();
    QCOMPARE(names.value(SectionedModel::KindRole), QByteArrayLiteral("kind"));
    QCOMPARE(names.value(SectionedModel::NumberRole), QByteArrayLiteral("number"));
    QCOMPARE(names.value(SectionedModel::SourceRowRole), QByteArrayLiteral("sourceRow"));
}

void HubModelsTest::matchesJsBuilder()
{
    QList<Item> items = sample();
    items.append(item(15, QStringLiteral("zed"), QString()));
    items.append(item(16, QStringLiteral("  "), QString()));
    items.append(item(17, QStringLiteral("again"), QStringLiteral("A")));
    FakeSource source(items);
    SectionedModel model;
    model.setKind(QStringLiteral("artist"));
    model.setSource(&source);
    model.setActive(true);
    const QList<QVariantMap> expected = jsBuildList(items, QStringLiteral("artist"));
    QCOMPARE(model.rowCount(), expected.size());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("b"),
                                           QStringLiteral("z"), QStringLiteral("#"), QStringLiteral("a")}));
}

void HubModelsTest::sectionFallback()
{
    FakeSource source({item(1, QStringLiteral("  The x"), QString()), item(2, QStringLiteral("\u00c9clair"), QString()),
                       item(3, QString(), QString()), item(4, QStringLiteral("Zoo"), QStringLiteral("Z"))});
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("t"), QStringLiteral("#"), QStringLiteral("z")}));
    QCOMPARE(model.get(1).value(QStringLiteral("kind")).toString(), QStringLiteral("row"));
}

void HubModelsTest::nameRole()
{
    FakeSource source(sample(), QByteArrayLiteral("name"));
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(model.data(model.index(4), SectionedModel::TitleRole).toString(), QStringLiteral("alpha"));
    QCOMPARE(model.get(4).value(QStringLiteral("title")).toString(), QStringLiteral("alpha"));
}

void HubModelsTest::invokables()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    QCOMPARE(model.letters(), QStringList());
    QCOMPARE(model.rowOfLetter(QStringLiteral("a")), -1);
    model.setActive(true);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("b")}));
    QCOMPARE(model.rowOfLetter(QStringLiteral("a")), 3);
    QCOMPARE(model.rowOfLetter(QStringLiteral("q")), -1);
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("b")), 7);
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("q")), -1);
    QCOMPARE(model.rowForSource(0), 1);
    QCOMPARE(model.rowForSource(4), 7);
    QCOMPARE(model.rowForSource(5), -1);
    QCOMPARE(model.nearestFocusable(0, 1), 0);
    QCOMPARE(model.nearestFocusable(3, 1), 3);
    QCOMPARE(model.nearestFocusable(3, -1), 3);
    QCOMPARE(model.nearestFocusable(6, 1), 6);
    QCOMPARE(model.nearestFocusable(8, 1), 7);
    QCOMPARE(model.nearestFocusable(-1, -1), 0);
    QCOMPARE(model.nearestFocusable(-1, 1), -1);
    QCOMPARE(model.nearestFocusable(9, 1), -1);
    QVERIFY(model.isFocusable(0));
    QVERIFY(model.isFocusable(1));
    QVERIFY(model.isFocusable(7));
    QVERIFY(!model.isFocusable(8));
    QVERIFY(!model.isFocusable(-1));
    QCOMPARE(model.get(0).value(QStringLiteral("kind")).toString(), QStringLiteral("divider"));
    QVERIFY(model.get(99).isEmpty());
}

void HubModelsTest::insertIntoGroup()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setKind(QStringLiteral("song"));
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    source.insert(3, {item(20, QStringLiteral("amber"), QStringLiteral("A")),
                      item(21, QStringLiteral("anchor"), QStringLiteral("A"))});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 5);
    QCOMPARE(inserted.at(0).at(2).toInt(), 6);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 7);
    QCOMPARE(model.count(), 10);
    const QList<QVariantMap> expected = jsBuildList(source.items(), QStringLiteral("song"));
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
    QCOMPARE(model.rowForSource(6), 9);
    QCOMPARE(model.rowOfLetter(QStringLiteral("b")), 8);
}

void HubModelsTest::insertAtGroupStart()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.insert(4, {item(22, QStringLiteral("bark"), QStringLiteral("B"))});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 7);
    const QList<QVariantMap> expected = jsBuildList(source.items(), QStringLiteral("row"));
    QCOMPARE(model.rowCount(), expected.size());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
    source.insert(0, {item(23, QStringLiteral("0th"), QStringLiteral("#"))});
    QCOMPARE(reset.count(), 0);
    QCOMPARE(inserted.count(), 2);
    QCOMPARE(inserted.at(1).at(1).toInt(), 1);
    QCOMPARE(model.get(1).value(QStringLiteral("id")).toLongLong(), 23);
    QCOMPARE(model.get(2).value(QStringLiteral("number")).toString(), QStringLiteral("2"));
}

void HubModelsTest::insertNewLetterResets()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.insert(5, {item(24, QStringLiteral("cat"), QStringLiteral("C"))});
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.letters().constLast(), QStringLiteral("c"));
    QCOMPARE(model.count(), 10);
    source.insert(4, {item(25, QStringLiteral("bb"), QStringLiteral("B")), item(26, QStringLiteral("ab"), QStringLiteral("A"))});
    QCOMPARE(reset.count(), 2);
    const QList<QVariantMap> expected = jsBuildList(source.items(), QStringLiteral("row"));
    QCOMPARE(model.rowCount(), expected.size());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
}

void HubModelsTest::removeFromGroup()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy count(&model, &SectionedModel::countChanged);
    source.remove(2, 1);
    QCOMPARE(reset.count(), 0);
    QCOMPARE(removed.count(), 1);
    QCOMPARE(removed.at(0).at(1).toInt(), 4);
    QCOMPARE(removed.at(0).at(2).toInt(), 4);
    QCOMPARE(count.count(), 1);
    const QList<QVariantMap> expected = jsBuildList(source.items(), QStringLiteral("row"));
    QCOMPARE(model.rowCount(), expected.size());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
    QCOMPARE(model.rowForSource(3), 6);
}

void HubModelsTest::removeLastOfGroupResets()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.remove(4, 1);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a")}));
    QCOMPARE(model.count(), 6);
    source.remove(1, 2);
    QCOMPARE(reset.count(), 2);
    const QList<QVariantMap> expected = jsBuildList(source.items(), QStringLiteral("row"));
    QCOMPARE(model.rowCount(), expected.size());
    for (int r = 0; r < model.rowCount(); ++r)
        QCOMPARE(model.get(r), expected.at(r));
}

void HubModelsTest::dataChangedForwards()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.set(3, item(13, QStringLiteral("avocado"), QStringLiteral("A")));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 5);
    QCOMPARE(model.data(model.index(5), SectionedModel::TitleRole).toString(), QStringLiteral("avocado"));
    source.set(3, item(13, QStringLiteral("zebra"), QStringLiteral("Z")));
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("z"),
                                           QStringLiteral("b")}));
}

void HubModelsTest::sourceResetRebuilds()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.reset({item(1, QStringLiteral("x"), QStringLiteral("X"))});
    QCOMPARE(reset.count(), 1);
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("x")}));
}

void HubModelsTest::inactiveDropsRows()
{
    FakeSource source(sample());
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    model.setActive(false);
    QCOMPARE(model.count(), 0);
    source.dataCalls = 0;
    source.insert(0, {item(30, QStringLiteral("0"), QStringLiteral("#"))});
    source.reset(sample());
    QCOMPARE(source.dataCalls, 0);
    QCOMPARE(model.count(), 0);
}

void HubModelsTest::gridLazy()
{
    FakeSource source(sample());
    AlbumGridModel model;
    model.setSource(&source);
    QCOMPARE(model.rowCount(), 0);
    QCOMPARE(source.dataCalls, 0);
    QCOMPARE(source.loads, 0);
    model.setActive(true);
    QCOMPARE(source.loads, 1);
    QCOMPARE(model.count(), 3);
}

void HubModelsTest::gridPackingThree()
{
    QList<Item> items = sample();
    items.insert(4, item(15, QStringLiteral("avid"), QStringLiteral("A")));
    FakeSource source(items);
    AlbumGridModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(model.columns(), 3);
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 | La 12 13 | 15 Lb 14"));
    const QVariantMap letter = model.cellAt(2, 1);
    QCOMPARE(letter.value(QStringLiteral("kind")).toString(), QStringLiteral("letter"));
    QCOMPARE(letter.value(QStringLiteral("letter")).toString(), QStringLiteral("b"));
    QCOMPARE(letter.value(QStringLiteral("title")).toString(), QStringLiteral("b"));
    QCOMPARE(letter.value(QStringLiteral("id")).toLongLong(), -1);
    const QVariantMap album = model.cellAt(1, 1);
    QCOMPARE(album.value(QStringLiteral("kind")).toString(), QStringLiteral("album"));
    QCOMPARE(album.value(QStringLiteral("id")).toLongLong(), 12);
    QCOMPARE(album.value(QStringLiteral("title")).toString(), QStringLiteral("alpha"));
    QCOMPARE(album.value(QStringLiteral("subtitle")).toString(), QStringLiteral("sub 12"));
    QCOMPARE(album.value(QStringLiteral("artPath")).toString(), QStringLiteral("/art/12.jpg"));
    QCOMPARE(album.value(QStringLiteral("letter")).toString(), QStringLiteral("a"));
    QVERIFY(model.cellAt(2, 3).isEmpty());
    QVERIFY(model.cellAt(3, 0).isEmpty());
    QCOMPARE(model.cellCount(2), 3);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("b")}));
    const QVariantMap b = model.cellOfLetter(QStringLiteral("b"));
    QCOMPARE(b.value(QStringLiteral("row")).toInt(), 2);
    QCOMPARE(b.value(QStringLiteral("col")).toInt(), 1);
    QCOMPARE(model.rowOfLetter(QStringLiteral("a")), 1);
    QCOMPARE(model.rowOfLetter(QStringLiteral("q")), -1);
    QCOMPARE(model.cellOfLetter(QStringLiteral("q")).value(QStringLiteral("row")).toInt(), -1);
    QCOMPARE(model.rowOfAlbum(14), 2);
    QCOMPARE(model.cellOfAlbum(14).value(QStringLiteral("col")).toInt(), 2);
    QCOMPARE(model.rowOfAlbum(99), -1);
    QCOMPARE(model.roleNames().value(AlbumGridModel::CellsRole), QByteArrayLiteral("cells"));
}

void HubModelsTest::gridPackingSix()
{
    QList<Item> items = sample();
    items.insert(4, item(15, QStringLiteral("avid"), QStringLiteral("A")));
    FakeSource source(items);
    AlbumGridModel model;
    model.setColumns(6);
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 La 12 13 | 15 Lb 14"));
    QCOMPARE(model.count(), 2);
    QCOMPARE(model.cellCount(1), 3);
    QVERIFY(model.cellAt(1, 3).isEmpty());
    QCOMPARE(model.rowOfAlbum(13), 0);
    QCOMPARE(model.cellOfLetter(QStringLiteral("b")).value(QStringLiteral("col")).toInt(), 1);
}

void HubModelsTest::gridColumnsReset()
{
    FakeSource source(sample());
    AlbumGridModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    QSignalSpy count(&model, &AlbumGridModel::countChanged);
    model.setColumns(6);
    QCOMPARE(reset.count(), 1);
    QCOMPARE(count.count(), 1);
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 La 12 13 | Lb 14"));
    model.setColumns(0);
    QCOMPARE(model.columns(), 1);
    QCOMPARE(model.count(), 8);
}

void HubModelsTest::gridInsertRepacks()
{
    FakeSource source(sample());
    AlbumGridModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 | La 12 13 | Lb 14"));
    source.insert(2, {item(16, QStringLiteral("3rd"), QStringLiteral("#"))});
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 | 16 La 12 | 13 Lb 14"));
    QCOMPARE(model.rowOfAlbum(16), 1);
    source.remove(5, 1);
    QCOMPARE(gridShape(model), QStringLiteral("L# 10 11 | 16 La 12 | 13"));
    QCOMPARE(model.rowOfAlbum(14), -1);
}

void HubModelsTest::gridDataChanged()
{
    FakeSource source(sample());
    AlbumGridModel model;
    model.setSource(&source);
    model.setActive(true);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);
    QSignalSpy reset(&model, &QAbstractItemModel::modelReset);
    source.set(3, item(13, QStringLiteral("avocado"), QStringLiteral("A")));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(changed.at(0).at(0).toModelIndex().row(), 1);
    QCOMPARE(model.cellAt(1, 2).value(QStringLiteral("title")).toString(), QStringLiteral("avocado"));
    source.set(3, item(13, QStringLiteral("cat"), QStringLiteral("C")));
    QCOMPARE(reset.count(), 1);
}

void HubModelsTest::sectionKeyFolding()
{
    QCOMPARE(foldKana(QStringLiteral("ガ")), QStringLiteral("か"));
    QCOMPARE(foldKana(QStringLiteral("ゃゅょっ")), QStringLiteral("やゆよつ"));
    QCOMPARE(foldKana(QStringLiteral("パピプペポ")), QStringLiteral("はひふへほ"));
    QCOMPARE(foldKana(QStringLiteral("ふるーり")), QStringLiteral("ふるり"));
    QCOMPARE(foldKana(QStringLiteral("ヴ")), QStringLiteral("う"));
    QCOMPARE(foldKana(QStringLiteral("ヷヲン")), QStringLiteral("わをん"));
    QCOMPARE(foldKana(QStringLiteral("みょみょ")), QStringLiteral("みよみよ"));
    QCOMPARE(sectionKey(QStringLiteral("ガラス")).letter, QStringLiteral("か"));
    QCOMPARE(sectionKey(QStringLiteral("ゃ")).letter, QStringLiteral("や"));
    QCOMPARE(sectionKey(QStringLiteral("ぱぴぷ")).letter, QStringLiteral("は"));
    QCOMPARE(sectionKey(QStringLiteral("ヴィジョン")).letter, QStringLiteral("あ"));
    QCOMPARE(sectionKey(QStringLiteral("ーあ")).letter, QStringLiteral("あ"));
    QCOMPARE(sectionKey(QStringLiteral("ゝさ")).letter, QStringLiteral("さ"));
    QCOMPARE(sectionKey(QStringLiteral("ｶﾞﾗｽ")).letter, QStringLiteral("か"));
    QCOMPARE(sectionKey(QStringLiteral("ゐ")).letter, QStringLiteral("わ"));
    QCOMPARE(sectionKey(QStringLiteral("ゑ")).letter, QStringLiteral("わ"));
    QCOMPARE(sectionKey(QStringLiteral("ヲ")).letter, QStringLiteral("わ"));
    QCOMPARE(sectionKey(QStringLiteral("ん")).letter, QStringLiteral("わ"));
    QCOMPARE(sectionKey(QStringLiteral("ヨノ")).letter, QStringLiteral("や"));
    QCOMPARE(sectionKey(QStringLiteral("マドロミ")).letter, QStringLiteral("ま"));
    QCOMPARE(sectionKey(QStringLiteral("わかばやし")).letter, QStringLiteral("わ"));
    QCOMPARE(sectionKey(QStringLiteral("ふるーり")).letter, QStringLiteral("は"));
    QCOMPARE(sectionKey(QStringLiteral("ナ")).letter, QStringLiteral("な"));
    QCOMPARE(sectionKey(QStringLiteral("ちゃ")).letter, QStringLiteral("た"));
    QCOMPARE(sectionKey(QStringLiteral("ライト")).letter, QStringLiteral("ら"));
}

void HubModelsTest::sectionKeyGroups()
{
    QCOMPARE(sectionKey(QStringLiteral("香椎モイミ")).letter, QStringLiteral("漢"));
    QCOMPARE(sectionKey(QStringLiteral("皇美緒奈")).letter, QStringLiteral("漢"));
    QCOMPARE(sectionKey(QStringLiteral("芥田レンリ")).letter, QStringLiteral("漢"));
    QCOMPARE(sectionKey(QStringLiteral("3 Doors Down")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("!!!")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("★Star")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("「かぎ」")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("\u00c9clair")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("ー")).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QString()).letter, QStringLiteral("#"));
    QCOMPARE(sectionKey(QStringLiteral("  Zoo")).letter, QStringLiteral("z"));
    QCOMPARE(sectionKey(QStringLiteral("\uff21bc")).letter, QStringLiteral("a"));
    QCOMPARE(sectionKey(QStringLiteral("The Band"), true).letter, QStringLiteral("b"));
    QCOMPARE(sectionKey(QStringLiteral("The Band")).letter, QStringLiteral("t"));
    QCOMPARE(HubSource::letterOf(QStringLiteral("ガラス")), QStringLiteral("か"));
    QCOMPARE(HubSource::letterOf(QStringLiteral("香椎")), QStringLiteral("漢"));
}

void HubModelsTest::sectionKeyOrder()
{
    const QStringList expected = {
        QStringLiteral("!!!"), QStringLiteral("1st"), QStringLiteral("alpha"), QStringLiteral("Beta"),
        QStringLiteral("zed"), QStringLiteral("あい"), QStringLiteral("アオ"), QStringLiteral("かき"),
        QStringLiteral("ガキ"), QStringLiteral("きく"), QStringLiteral("ふるーり"), QStringLiteral("マドロミ"),
        QStringLiteral("みょみょ"), QStringLiteral("ヨノ"), QStringLiteral("わかばやし"), QStringLiteral("ん"),
        QStringLiteral("一"), QStringLiteral("香椎モイミ"),
    };
    QStringList sorted = expected;
    std::reverse(sorted.begin(), sorted.end());
    std::stable_sort(sorted.begin(), sorted.end(), [](const QString &a, const QString &b) {
        return sectionKey(a).sortKey < sectionKey(b).sortKey;
    });
    QCOMPARE(sorted, expected);
}

void HubModelsTest::sectionLetterList()
{
    const QStringList &letters = sectionLetters();
    QCOMPARE(letters.size(), 38);
    QCOMPARE(letters.first(), QStringLiteral("#"));
    QCOMPARE(letters.at(1), QStringLiteral("a"));
    QCOMPARE(letters.at(26), QStringLiteral("z"));
    QCOMPARE(letters.mid(27).join(QString()), QStringLiteral("あかさたなはまやらわ漢"));
}

void HubModelsTest::kanaLetters()
{
    const QStringList titles = {
        QStringLiteral("1st"), QStringLiteral("alpha"), QStringLiteral("Beta"), QStringLiteral("zed"),
        QStringLiteral("あい"), QStringLiteral("ガキ"), QStringLiteral("マドロミ"), QStringLiteral("ヨノ"),
        QStringLiteral("わかばやし"), QStringLiteral("ん"), QStringLiteral("香椎モイミ"),
    };
    QList<Item> items;
    for (qsizetype i = 0; i < titles.size(); ++i)
        items.append(item(i + 1, titles.at(i), QString()));
    FakeSource source(items);
    SectionedModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("b"),
                                           QStringLiteral("z"), QStringLiteral("あ"), QStringLiteral("か"),
                                           QStringLiteral("ま"), QStringLiteral("や"), QStringLiteral("わ"),
                                           QStringLiteral("漢")}));
    QCOMPARE(model.rowOfLetter(QStringLiteral("か")), 10);
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("か")), 11);
    QCOMPARE(model.get(11).value(QStringLiteral("title")).toString(), QStringLiteral("ガキ"));
    QCOMPARE(model.rowOfLetter(QStringLiteral("わ")), 16);
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("わ")), 17);
    QCOMPARE(model.get(18).value(QStringLiteral("title")).toString(), QStringLiteral("ん"));
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("漢")), 20);
    QCOMPARE(model.get(19).value(QStringLiteral("kind")).toString(), QStringLiteral("divider"));
    QCOMPARE(model.get(19).value(QStringLiteral("title")).toString(), QStringLiteral("漢"));
    QCOMPARE(model.rowOfLetter(QStringLiteral("さ")), -1);
    QCOMPARE(model.count(), 21);
}

void HubModelsTest::gridPackingKana()
{
    FakeSource source({item(1, QStringLiteral("1st"), QString()), item(2, QStringLiteral("alpha"), QString()),
                       item(3, QStringLiteral("かき"), QString()), item(4, QStringLiteral("ガキ"), QString()),
                       item(5, QStringLiteral("一"), QString())});
    AlbumGridModel model;
    model.setSource(&source);
    model.setActive(true);
    QCOMPARE(gridShape(model), QStringLiteral("L# 1 La | 2 Lか 3 | 4 L漢 5"));
    QCOMPARE(model.letters(), QStringList({QStringLiteral("#"), QStringLiteral("a"), QStringLiteral("か"),
                                           QStringLiteral("漢")}));
    const QVariantMap ka = model.cellOfLetter(QStringLiteral("か"));
    QCOMPARE(ka.value(QStringLiteral("row")).toInt(), 1);
    QCOMPARE(ka.value(QStringLiteral("col")).toInt(), 1);
    QCOMPARE(model.rowOfLetter(QStringLiteral("漢")), 2);
    QCOMPARE(model.cellAt(2, 1).value(QStringLiteral("title")).toString(), QStringLiteral("漢"));
    QCOMPARE(model.cellAt(2, 1).value(QStringLiteral("kind")).toString(), QStringLiteral("letter"));
    model.setColumns(6);
    QCOMPARE(gridShape(model), QStringLiteral("L# 1 La 2 Lか 3 | 4 L漢 5"));
    QCOMPARE(model.cellOfAlbum(5).value(QStringLiteral("col")).toInt(), 2);
}

void HubModelsTest::scaleSongs()
{
    FakeSource source(synthetic(8400));
    SectionedModel model;
    model.setKind(QStringLiteral("song"));
    model.setSource(&source);
    const qint64 ms = medianBuild(model);
    qInfo("microdap perf: sectioned songs 8400 %lld ms", ms);
    QCOMPARE(model.count(), 8400 + 27);
    QCOMPARE(model.letters().size(), 27);
    QVERIFY2(ms <= 20, qPrintable(QStringLiteral("%1 ms").arg(ms)));
}

void HubModelsTest::scaleAlbums()
{
    FakeSource source(synthetic(1200));
    AlbumGridModel model;
    model.setSource(&source);
    for (int columns : {3, 6}) {
        model.setColumns(columns);
        const qint64 ms = medianBuild(model);
        qInfo("microdap perf: album grid 1200 at %d columns %lld ms", columns, ms);
        QCOMPARE(model.count(), (1200 + 27 + columns - 1) / columns);
        QVERIFY2(ms <= 10, qPrintable(QStringLiteral("%1 ms").arg(ms)));
    }
}

void HubModelsTest::scaleLookups()
{
    FakeSource source(synthetic(8400));
    SectionedModel model;
    model.setKind(QStringLiteral("song"));
    model.setSource(&source);
    model.setActive(true);
    const QStringList letters = model.letters();
    QCOMPARE(letters.size(), 27);
    const int n = model.count();
    const int calls = 10000;
    qint64 sum = 0;
    QElapsedTimer timer;
    timer.start();
    for (int i = 0; i < calls; ++i)
        sum += model.rowOfLetter(letters.at(i % letters.size()));
    const qint64 rowUs = timer.nsecsElapsed() / 1000;
    timer.restart();
    for (int i = 0; i < calls; ++i)
        sum += model.firstRowOfLetter(letters.at(i % letters.size()));
    const qint64 firstUs = timer.nsecsElapsed() / 1000;
    timer.restart();
    for (int i = 0; i < calls; ++i)
        sum += model.nearestFocusable((i * 7919) % (n + 2) - 1, (i & 1) ? 1 : -1);
    const qint64 nearUs = timer.nsecsElapsed() / 1000;
    qInfo("microdap perf: sectioned lookups 8400 rows %d calls rowOfLetter %lld us firstRowOfLetter %lld us "
          "nearestFocusable %lld us",
          calls, rowUs, firstUs, nearUs);
    QVERIFY(sum > 0);
    const int z = model.rowOfLetter(QStringLiteral("z"));
    QCOMPARE(model.get(z).value(QStringLiteral("kind")).toString(), QStringLiteral("divider"));
    QCOMPARE(model.get(z).value(QStringLiteral("letter")).toString(), QStringLiteral("z"));
    QCOMPARE(model.firstRowOfLetter(QStringLiteral("z")), z + 1);
    QCOMPARE(model.rowOfLetter(QStringLiteral("#")), 0);
    QCOMPARE(model.nearestFocusable(n, 1), n - 1);
    QCOMPARE(model.nearestFocusable(z, 1), z);
    QVERIFY2(rowUs <= 5000, qPrintable(QStringLiteral("rowOfLetter %1 us").arg(rowUs)));
    QVERIFY2(firstUs <= 5000, qPrintable(QStringLiteral("firstRowOfLetter %1 us").arg(firstUs)));
    QVERIFY2(nearUs <= 5000, qPrintable(QStringLiteral("nearestFocusable %1 us").arg(nearUs)));

    FakeSource albums(synthetic(1200));
    AlbumGridModel grid;
    grid.setSource(&albums);
    grid.setActive(true);
    const QStringList gridLetters = grid.letters();
    QCOMPARE(gridLetters.size(), 27);
    int hits = 0;
    timer.restart();
    for (int i = 0; i < calls; ++i)
        hits += grid.rowOfLetter(gridLetters.at(i % gridLetters.size())) >= 0 ? 1 : 0;
    const qint64 gridUs = timer.nsecsElapsed() / 1000;
    qInfo("microdap perf: album grid lookups 1200 albums %d calls rowOfLetter %lld us", calls, gridUs);
    QCOMPARE(hits, calls);
    QCOMPARE(grid.cellOfLetter(QStringLiteral("#")).value(QStringLiteral("col")).toInt(), 0);
    QVERIFY2(gridUs <= 50000, qPrintable(QStringLiteral("grid rowOfLetter %1 us").arg(gridUs)));
}

QTEST_GUILESS_MAIN(HubModelsTest)
#include "tst_hubmodels.moc"
