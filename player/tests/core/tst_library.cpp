#include "Library.h"
#include "Models.h"

#include <taglib/flacfile.h>
#include <taglib/flacpicture.h>
#include <taglib/tbytevector.h>
#include <taglib/tpropertymap.h>
#include <taglib/tstring.h>
#include <taglib/tstringlist.h>

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

using namespace casdap;

namespace {

constexpr int scanTimeoutMs = 60000;

bool copyTree(const QString &from, const QString &to)
{
    if (!QDir().mkpath(to))
        return false;
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString relative = QDir(from).relativeFilePath(info.absoluteFilePath());
        const QString target = to + QLatin1Char('/') + relative;
        if (!QDir().mkpath(QFileInfo(target).absolutePath()))
            return false;
        if (!QFile::copy(info.absoluteFilePath(), target))
            return false;
    }
    return true;
}

QVariant scalar(const QSqlDatabase &db, const QString &sql, const QVariantList &binds = {})
{
    QSqlQuery query(db);
    query.prepare(sql);
    for (const QVariant &bind : binds)
        query.addBindValue(bind);
    if (!query.exec() || !query.next())
        return {};
    return query.value(0);
}

QList<qint64> column(const QSqlDatabase &db, const QString &sql)
{
    QList<qint64> values;
    QSqlQuery query(sql, db);
    while (query.next())
        values.append(query.value(0).toLongLong());
    return values;
}

QHash<QString, qint64> addedByPath(const QSqlDatabase &db)
{
    QHash<QString, qint64> map;
    QSqlQuery query(QStringLiteral("SELECT path, added FROM tracks"), db);
    while (query.next())
        map.insert(query.value(0).toString(), query.value(1).toLongLong());
    return map;
}

bool bumpMtime(const QString &path, int secs)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadWrite))
        return false;
    return file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(secs),
                            QFileDevice::FileModificationTime);
}

bool retag(const QString &path, const char *key, const QStringList &values)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    TagLib::PropertyMap props = file.properties();
    TagLib::StringList list;
    for (const QString &value : values)
        list.append(TagLib::String(value.toUtf8().constData(), TagLib::String::UTF8));
    props[key] = list;
    file.setProperties(props);
    return file.save();
}

bool retagAlbum(const QString &path, const QString &album)
{
    return retag(path, "ALBUM", {album});
}

int rowOf(const LibraryModel &model, const QString &title)
{
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row), LibraryModel::TitleRole).toString() == title)
            return row;
    }
    return -1;
}

QString subtitleOf(const LibraryModel &model, const QString &title)
{
    const int row = rowOf(model, title);
    return row < 0 ? QString() : model.data(model.index(row), LibraryModel::SubtitleRole).toString();
}

bool replaceCover(const QString &path, const QByteArray &data)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    file.removePictures();
    auto *picture = new TagLib::FLAC::Picture;
    picture->setData(TagLib::ByteVector(data.constData(), static_cast<unsigned int>(data.size())));
    picture->setMimeType("image/png");
    picture->setType(TagLib::FLAC::Picture::FrontCover);
    file.addPicture(picture);
    return file.save();
}

QString sha1Name(const QByteArray &data)
{
    return QString::fromLatin1(QCryptographicHash::hash(data, QCryptographicHash::Sha1).toHex())
           + QStringLiteral(".png");
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(data) == data.size();
}

}

class LibraryTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void counts();
    void cjk();
    void art();
    void rescanUnchanged();
    void songs();
    void albums();
    void genres();
    void artists();
    void splitArtists();
    void migrate();
    void history();
    void newAlbums();
    void rating();
    void touched();
    void removed();
    void missingRoot();
    void artContent();
    void pruneArtForeign();
    void pins();
    void pinsPrune();
    void rateAndDepth();
    void migrateFrom2();
    void playlists();

private:
    bool scan(int &added, int &updated, int &removed);
    bool scan(const QStringList &roots, int &added, int &updated, int &removed);
    QString firstTrackOf(const QString &album) const;
    QString artPathOf(const QString &album) const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
};

bool LibraryTest::scan(int &added, int &updated, int &removed)
{
    return scan({m_music}, added, updated, removed);
}

bool LibraryTest::scan(const QStringList &roots, int &added, int &updated, int &removed)
{
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescan(roots);
    if (!spy.wait(scanTimeoutMs))
        return false;
    const QList<QVariant> args = spy.takeFirst();
    added = args.at(0).toInt();
    updated = args.at(1).toInt();
    removed = args.at(2).toInt();
    return args.at(3).toBool();
}

QString LibraryTest::firstTrackOf(const QString &album) const
{
    return scalar(m_library->database(),
                  QStringLiteral("SELECT t.path FROM tracks t JOIN albums a ON a.id = t.album_id "
                                 "WHERE a.title = ? ORDER BY t.track_no LIMIT 1"),
                  {album})
        .toString();
}

QString LibraryTest::artPathOf(const QString &album) const
{
    return scalar(m_library->database(),
                  QStringLiteral("SELECT art_path FROM albums WHERE title = ?"), {album})
        .toString();
}

void LibraryTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(CASDAP_FIXTURE_MUSIC), m_music));

    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"),
                                          m_dir.path() + QStringLiteral("/art"));
    QVERIFY(m_library->isOpen());
    QCOMPARE(scalar(m_library->database(), QStringLiteral("PRAGMA user_version")).toInt(), 3);

    QSignalSpy scanningSpy(m_library.get(), &Library::scanningChanged);
    QSignalSpy changedSpy(m_library.get(), &Library::changed);
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 8);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scanningSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!m_library->scanning());
}

void LibraryTest::counts()
{
    const QVariantMap counts = m_library->counts();
    QCOMPARE(counts.value(QStringLiteral("tracks")).toInt(), 8);
    QCOMPARE(counts.value(QStringLiteral("albums")).toInt(), 3);
    QCOMPARE(counts.value(QStringLiteral("artists")).toInt(), 6);
    QCOMPARE(counts.value(QStringLiteral("genres")).toInt(), 3);
}

void LibraryTest::cjk()
{
    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND artist = ?"),
                    {QStringLiteral("アルバム二"), QStringLiteral("アーティスト")})
                 .toInt(),
             1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT title FROM tracks WHERE track_no = 1 AND artist = ?"),
                    {QStringLiteral("アーティスト")})
                 .toString(),
             QStringLiteral("一"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT year FROM albums WHERE title = ?"),
                    {QStringLiteral("Album One")})
                 .toInt(),
             2020);
    QVERIFY(scalar(db, QStringLiteral("SELECT duration_ms FROM tracks WHERE title = ?"),
                   {QStringLiteral("One")})
                .toInt()
            > 1000);
}

void LibraryTest::art()
{
    const QSqlDatabase db = m_library->database();
    const QString embedded = artPathOf(QStringLiteral("Album One"));
    QVERIFY(!embedded.isEmpty());
    QVERIFY(QFileInfo::exists(embedded));
    QVERIFY(embedded.startsWith(m_library->artDir()));
    QVERIFY(embedded.endsWith(QLatin1StringView(".png")));

    const QString folder = artPathOf(QStringLiteral("アルバム二"));
    QVERIFY(!folder.isEmpty());
    QVERIFY(QFileInfo::exists(folder));
    QCOMPARE(QFileInfo(folder).fileName(), QStringLiteral("cover.png"));

    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND art_path IS NULL"),
                    {QStringLiteral("Single")})
                 .toInt(),
             1);
}

void LibraryTest::rescanUnchanged()
{
    const QHash<QString, qint64> before = addedByPath(m_library->database());
    QCOMPARE(before.size(), 8);

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(addedByPath(m_library->database()), before);
}

void LibraryTest::songs()
{
    SongsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 8);
    const int row = model.indexOf(
        scalar(m_library->database(), QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
               {QStringLiteral("One")})
            .toLongLong());
    QVERIFY(row >= 0);
    QCOMPARE(model.data(model.index(row), LibraryModel::SectionRole).toString(), QStringLiteral("O"));
    QCOMPARE(model.data(model.index(row), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("Artist A"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("title"));
}

void LibraryTest::albums()
{
    AlbumsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("アルバム二"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SectionRole).toString(), QStringLiteral("#"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Album One"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SectionRole).toString(), QStringLiteral("A"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("Single"));
}

void LibraryTest::genres()
{
    GenresModel model(m_library.get());
    QCOMPARE(model.rowCount(), 3);
    const int row = model.indexOf(
        scalar(m_library->database(), QStringLiteral("SELECT id FROM genres WHERE name = ?"),
               {QStringLiteral("Electronic")})
            .toLongLong());
    QVERIFY(row >= 0);
    QCOMPARE(model.data(model.index(row), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("4 songs"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("name"));
}

void LibraryTest::artists()
{
    const QSqlDatabase db = m_library->database();
    ArtistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("アーティスト"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Artist A"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("Artist C"));
    QCOMPARE(model.data(model.index(3), LibraryModel::TitleRole).toString(), QStringLiteral("Artist E"));
    QCOMPARE(model.data(model.index(4), LibraryModel::TitleRole).toString(), QStringLiteral("Artist F"));
    QCOMPARE(model.data(model.index(5), LibraryModel::TitleRole).toString(), QStringLiteral("The Band"));
    QCOMPARE(model.data(model.index(5), LibraryModel::SectionRole).toString(), QStringLiteral("B"));
    QCOMPARE(model.data(model.index(5), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 3 songs"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(3), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(model.data(model.index(4), LibraryModel::SubtitleRole).toString(),
             QStringLiteral("1 album, 1 song"));
    QCOMPARE(rowOf(model, QStringLiteral("Artist E, Artist F")), -1);
    QVERIFY(!model.data(model.index(1), LibraryModel::ArtPathRole).toString().isEmpty());
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("name"));

    const qint64 three = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                {QStringLiteral("Three")})
                             .toLongLong();
    QVERIFY(three > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {three}).toString(),
             QStringLiteral("Artist A, Artist C"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = ?"),
                    {three})
                 .toInt(),
             2);
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 1"),
                    {three})
                 .toString(),
             QStringLiteral("Artist C"));

    const qint64 four = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                               {QStringLiteral("Four")})
                            .toLongLong();
    QVERIFY(four > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {four}).toString(),
             QStringLiteral("Artist E, Artist F"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = ?"),
                    {four})
                 .toInt(),
             2);
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 0"),
                    {four})
                 .toString(),
             QStringLiteral("Artist E"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT ar.name FROM track_artists ta "
                                       "JOIN artists ar ON ar.id = ta.artist_id "
                                       "WHERE ta.track_id = ? AND ta.pos = 1"),
                    {four})
                 .toString(),
             QStringLiteral("Artist F"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);

    const qint64 artistA = model.data(model.index(1), LibraryModel::IdRole).toLongLong();
    const qint64 artistC = model.data(model.index(2), LibraryModel::IdRole).toLongLong();
    const qint64 albumOne = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                   {QStringLiteral("Album One")})
                                .toLongLong();
    QCOMPARE(m_library->artistAlbums(artistC), QVariantList{albumOne});
    QCOMPARE(m_library->artistTracks(artistC), QVariantList{three});
    QCOMPARE(m_library->artistAlbums(artistA), QVariantList{albumOne});
    QCOMPARE(m_library->artistTracks(artistA).size(), 3);
    QCOMPARE(m_library->artistTracks(artistA).last().toLongLong(), three);
}

void LibraryTest::splitArtists()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString splitDir = m_music + QStringLiteral("/Split");
    const QString semi = splitDir + QStringLiteral("/01 Semi.flac");
    const QString band = splitDir + QStringLiteral("/02 Band.flac");
    QVERIFY(QDir().mkpath(splitDir));
    QVERIFY(QFile::copy(source, semi));
    QVERIFY(QFile::copy(source, band));
    QVERIFY(retag(semi, "ALBUM", {QStringLiteral("Split")}));
    QVERIFY(retag(semi, "TITLE", {QStringLiteral("Semi")}));
    QVERIFY(retag(semi, "ARTIST", {QStringLiteral("Artist A; Artist D")}));
    QVERIFY(retag(band, "ALBUM", {QStringLiteral("Split")}));
    QVERIFY(retag(band, "TITLE", {QStringLiteral("Band")}));
    QVERIFY(retag(band, "ARTIST", {QStringLiteral("Earth, Wind & Fire")}));
    QVERIFY(retag(band, "ALBUMARTIST", {QStringLiteral("earth, wind & fire")}));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 2);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);

    ArtistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 8);
    QCOMPARE(rowOf(model, QStringLiteral("Artist A; Artist D")), -1);
    QCOMPARE(rowOf(model, QStringLiteral("Earth")), -1);
    QCOMPARE(rowOf(model, QStringLiteral("Wind & Fire")), -1);
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist D")), QStringLiteral("1 album, 1 song"));
    QCOMPARE(subtitleOf(model, QStringLiteral("Earth, Wind & Fire")), QStringLiteral("1 album, 1 song"));
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist A")), QStringLiteral("2 albums, 4 songs"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE path = ?"), {semi}).toString(),
             QStringLiteral("Artist A, Artist D"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE path = ?"), {band}).toString(),
             QStringLiteral("Earth, Wind & Fire"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT album_artist FROM tracks WHERE path = ?"), {band})
                 .toString(),
             QStringLiteral("earth, wind & fire"));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists WHERE track_id = "
                                       "(SELECT id FROM tracks WHERE path = ?)"),
                    {band})
                 .toInt(),
             1);

    QVERIFY(QDir(splitDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 2);
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(rowOf(model, QStringLiteral("Artist D")), -1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM artists")).toInt(), 6);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::migrate()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate-setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("DROP TABLE track_artists")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 1")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 3);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), 8);

    QSignalSpy spy(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));
    const QList<QVariant> args = spy.takeFirst();
    QCOMPARE(args.at(0).toInt(), 0);
    QCOMPARE(args.at(1).toInt(), 8);
    QCOMPARE(args.at(2).toInt(), 0);
    QVERIFY(args.at(3).toBool());
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM track_artists")).toInt(), 10);

    ArtistsModel model(&library);
    QCOMPARE(model.rowCount(), 6);
    QCOMPARE(subtitleOf(model, QStringLiteral("Artist C")), QStringLiteral("1 album, 1 song"));
}

void LibraryTest::history()
{
    const QSqlDatabase db = m_library->database();
    const qint64 first = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                {QStringLiteral("One")})
                             .toLongLong();
    const qint64 second = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                                 {QStringLiteral("Single")})
                              .toLongLong();
    QVERIFY(first > 0);
    QVERIFY(second > 0);

    HistoryModel model(m_library.get());
    QCOMPARE(model.mode(), HistoryModel::Recent);
    QCOMPARE(model.rowCount(), 0);

    const QDateTime base = QDateTime::currentDateTimeUtc();
    m_library->addHistory(first, base);
    m_library->addHistory(second, base.addSecs(1));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), second);
    QCOMPARE(model.data(model.index(1), LibraryModel::IdRole).toLongLong(), first);

    m_library->addHistory(first, base.addSecs(2));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), first);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 3);
}

void LibraryTest::newAlbums()
{
    HistoryModel model(m_library.get());
    QSignalSpy modeSpy(&model, &HistoryModel::modeChanged);
    model.setMode(HistoryModel::New);
    QCOMPARE(modeSpy.count(), 1);
    QCOMPARE(model.rowCount(), 3);

    QList<qint64> actual;
    for (int row = 0; row < model.rowCount(); ++row)
        actual.append(model.data(model.index(row), LibraryModel::IdRole).toLongLong());
    const QList<qint64> expected = column(
        m_library->database(), QStringLiteral("SELECT id FROM albums ORDER BY added DESC, id DESC"));
    QCOMPARE(expected.size(), 3);
    QCOMPARE(actual, expected);
}

void LibraryTest::rating()
{
    const QSqlDatabase db = m_library->database();
    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                             {QStringLiteral("Two")})
                          .toLongLong();
    QVERIFY(id > 0);
    QCOMPARE(m_library->rating(id), 0);

    QSignalSpy spy(m_library.get(), &Library::ratingChanged);
    m_library->setRating(id, 1);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT rating FROM tracks WHERE id = ?"), {id}).toInt(), 1);
    QCOMPARE(m_library->rating(id), 1);

    m_library->setRating(id, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT rating FROM tracks WHERE id = ?"), {id}).toInt(), 0);
}

void LibraryTest::touched()
{
    const QString path = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!path.isEmpty());
    const QHash<QString, qint64> before = addedByPath(m_library->database());

    QVERIFY(bumpMtime(path, 3600));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);
    QCOMPARE(addedByPath(m_library->database()), before);
}

void LibraryTest::removed()
{
    const QString path = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!path.isEmpty());
    QVERIFY(QFile::remove(path));

    int added = 0;
    int updated = 0;
    int removedCount = 0;
    QVERIFY(scan(added, updated, removedCount));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removedCount, 1);

    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path = ?"), {path}).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::missingRoot()
{
    const QSqlDatabase db = m_library->database();
    const QString extra = m_dir.path() + QStringLiteral("/extra");
    const QString hidden = m_dir.path() + QStringLiteral("/extra.gone");
    QVERIFY(copyTree(QStringLiteral(CASDAP_FIXTURE_MUSIC) + QStringLiteral("/Single"),
                     extra + QStringLiteral("/Single")));
    const QStringList roots{m_music, extra};

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 8);

    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks ORDER BY id DESC LIMIT 1"))
                          .toLongLong();
    QVERIFY(id > 0);
    QVERIFY(scalar(db, QStringLiteral("SELECT path FROM tracks WHERE id = ?"), {id})
                .toString()
                .startsWith(extra + QLatin1Char('/')));
    m_library->addHistory(id);
    const int history = scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt();
    QCOMPARE(history, 2);

    QVERIFY(QDir().rename(extra, hidden));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 8);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE id = ?"), {id}).toInt(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history WHERE track_id = ?"), {id}).toInt(), 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), history);

    QVERIFY(QDir().rename(hidden, extra));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);

    QVERIFY(QDir(extra).removeRecursively());
    QVERIFY(QDir().mkpath(extra));
    QVERIFY(scan(roots, added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM history")).toInt(), history - 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
}

void LibraryTest::artContent()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString albumDir = m_music + QStringLiteral("/Album Two");
    const QString copy = albumDir + QStringLiteral("/01 Copy.flac");
    QVERIFY(QDir().mkpath(albumDir));
    QVERIFY(QFile::copy(source, copy));
    QVERIFY(retagAlbum(copy, QStringLiteral("Album Two")));
    QVERIFY(bumpMtime(copy, 3600));

    const QString coverPath = m_music + QStringLiteral("/アルバム二/cover.png");
    QFile coverFile(coverPath);
    QVERIFY(coverFile.open(QIODevice::ReadOnly));
    const QByteArray cover = coverFile.readAll();
    coverFile.close();
    QVERIFY(!cover.isEmpty());

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 1);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 4);

    const QString one = artPathOf(QStringLiteral("Album One"));
    const QString two = artPathOf(QStringLiteral("Album Two"));
    QVERIFY(!one.isEmpty());
    QCOMPARE(two, one);
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(QFileInfo(one).fileName(), sha1Name(cover));
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 1);

    QByteArray altered = cover;
    altered.append(QByteArrayLiteral("casdap"));
    QVERIFY(replaceCover(copy, altered));
    QVERIFY(bumpMtime(copy, 7200));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 1);
    QCOMPARE(removed, 0);

    const QString replaced = artPathOf(QStringLiteral("Album Two"));
    QVERIFY(replaced != one);
    QCOMPARE(QFileInfo(replaced).fileName(), sha1Name(altered));
    QVERIFY(QFileInfo::exists(replaced));
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(artPathOf(QStringLiteral("Album One")), one);
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 2);

    QVERIFY(QDir(albumDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 1);
    QCOMPARE(m_library->counts().value(QStringLiteral("albums")).toInt(), 3);
    QVERIFY(!QFileInfo::exists(replaced));
    QVERIFY(QFileInfo::exists(one));
    QCOMPARE(QDir(m_library->artDir()).entryList(QDir::Files).size(), 1);

    QVERIFY(QFile::remove(coverPath));
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM albums WHERE title = ? AND art_path IS NULL"),
                    {QStringLiteral("アルバム二")})
                 .toInt(),
             1);
}

void LibraryTest::pruneArtForeign()
{
    const QString artDir = m_library->artDir();
    QVERIFY(QDir(artDir).exists());
    const QString kept = artPathOf(QStringLiteral("Album One"));
    QVERIFY(QFileInfo::exists(kept));

    const QString foreign = artDir + QStringLiteral("/notes.txt");
    const QString lookalike = artDir + QStringLiteral("/cover.png");
    const QString shortHash = artDir + QStringLiteral("/") + QString(39, QLatin1Char('a'))
                              + QStringLiteral(".png");
    const QString upper = artDir + QStringLiteral("/") + QString(40, QLatin1Char('A'))
                          + QStringLiteral(".png");
    const QString stalePng = artDir + QStringLiteral("/") + QString(40, QLatin1Char('b'))
                             + QStringLiteral(".png");
    const QString staleJpg = artDir + QStringLiteral("/") + QString(40, QLatin1Char('c'))
                             + QStringLiteral(".jpg");
    QVERIFY(writeBytes(foreign, QByteArrayLiteral("notes")));
    QVERIFY(writeBytes(lookalike, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(shortHash, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(upper, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(stalePng, QByteArrayLiteral("png")));
    QVERIFY(writeBytes(staleJpg, QByteArrayLiteral("jpg")));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 0);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);

    QVERIFY(QFileInfo::exists(kept));
    QVERIFY(QFileInfo::exists(foreign));
    QVERIFY(QFileInfo::exists(lookalike));
    QVERIFY(QFileInfo::exists(shortHash));
    QVERIFY(QFileInfo::exists(upper));
    QVERIFY(!QFileInfo::exists(stalePng));
    QVERIFY(!QFileInfo::exists(staleJpg));

    QVERIFY(QFile::remove(foreign));
    QVERIFY(QFile::remove(lookalike));
    QVERIFY(QFile::remove(shortHash));
    QVERIFY(QFile::remove(upper));
}

void LibraryTest::pins()
{
    const QSqlDatabase db = m_library->database();
    const qint64 albumOne = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                   {QStringLiteral("Album One")})
                                .toLongLong();
    const qint64 artistA = scalar(db, QStringLiteral("SELECT id FROM artists WHERE name = ?"),
                                  {QStringLiteral("Artist A")})
                               .toLongLong();
    const qint64 two = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE title = ?"),
                              {QStringLiteral("Two")})
                           .toLongLong();
    QVERIFY(albumOne > 0);
    QVERIFY(artistA > 0);
    QVERIFY(two > 0);
    {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO playlists(name) VALUES(?)"));
        insert.addBindValue(QStringLiteral("Mix"));
        QVERIFY(insert.exec());
    }
    const qint64 mix = scalar(db, QStringLiteral("SELECT id FROM playlists WHERE name = ?"),
                              {QStringLiteral("Mix")})
                           .toLongLong();
    QVERIFY(mix > 0);
    {
        QSqlQuery insert(db);
        insert.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, pos, track_id) VALUES(?, 0, ?)"));
        insert.addBindValue(mix);
        insert.addBindValue(two);
        QVERIFY(insert.exec());
    }

    PinsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.roleNames().value(LibraryModel::KindRole) == QByteArrayLiteral("kind"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("title"));

    QSignalSpy spy(m_library.get(), &Library::pinsChanged);
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->pin(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->isPinned(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->pin(QStringLiteral("album"), albumOne));
    QVERIFY(m_library->pin(QStringLiteral("artist"), artistA));
    QVERIFY(m_library->pin(QStringLiteral("song"), two));
    QVERIFY(m_library->pin(QStringLiteral("playlist"), mix));
    QVERIFY(!m_library->pin(QStringLiteral("genre"), 1));
    QCOMPARE(spy.count(), 4);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pins")).toInt(), 4);
    QCOMPARE(scalar(db, QStringLiteral("SELECT MAX(pos) FROM pins")).toInt(), 3);

    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(0), LibraryModel::KindRole).toString(), QStringLiteral("album"));
    QCOMPARE(model.data(model.index(0), LibraryModel::IdRole).toLongLong(), albumOne);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("Album One"));
    QCOMPARE(model.data(model.index(0), LibraryModel::SubtitleRole).toString(),
             scalar(db, QStringLiteral("SELECT artist FROM albums WHERE id = ?"), {albumOne}).toString());
    QVERIFY(!model.data(model.index(0), LibraryModel::ArtPathRole).toString().isEmpty());
    QCOMPARE(model.data(model.index(0), LibraryModel::SectionRole).toString(), QStringLiteral("pins"));
    QCOMPARE(model.data(model.index(1), LibraryModel::KindRole).toString(), QStringLiteral("artist"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Artist A"));
    QCOMPARE(model.data(model.index(1), LibraryModel::SubtitleRole).toString(), QStringLiteral("1 album"));
    QVERIFY(!model.data(model.index(1), LibraryModel::ArtPathRole).toString().isEmpty());
    QCOMPARE(model.data(model.index(2), LibraryModel::KindRole).toString(), QStringLiteral("song"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("Two"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SubtitleRole).toString(),
             scalar(db, QStringLiteral("SELECT artist FROM tracks WHERE id = ?"), {two}).toString());
    QCOMPARE(model.data(model.index(2), LibraryModel::ArtPathRole).toString(),
             artPathOf(QStringLiteral("Album One")));
    QCOMPARE(model.data(model.index(3), LibraryModel::KindRole).toString(), QStringLiteral("playlist"));
    QCOMPARE(model.data(model.index(3), LibraryModel::TitleRole).toString(), QStringLiteral("Mix"));
    QCOMPARE(model.data(model.index(3), LibraryModel::SubtitleRole).toString(), QStringLiteral("1 song"));
    QVERIFY(model.data(model.index(3), LibraryModel::ArtPathRole).toString().isEmpty());
    const QVariantMap first = model.get(0);
    QCOMPARE(first.value(QStringLiteral("kind")).toString(), QStringLiteral("album"));
    QCOMPARE(first.value(QStringLiteral("id")).toLongLong(), albumOne);

    QVERIFY(m_library->unpin(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->unpin(QStringLiteral("album"), albumOne));
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), albumOne));
    QCOMPARE(spy.count(), 5);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), LibraryModel::KindRole).toString(), QStringLiteral("artist"));

    QVERIFY(m_library->pin(QStringLiteral("album"), albumOne));
    QCOMPARE(model.rowCount(), 4);
    QCOMPARE(model.data(model.index(3), LibraryModel::KindRole).toString(), QStringLiteral("album"));
    QCOMPARE(model.data(model.index(3), LibraryModel::IdRole).toLongLong(), albumOne);
}

void LibraryTest::pinsPrune()
{
    const QSqlDatabase db = m_library->database();
    const QString source = firstTrackOf(QStringLiteral("Album One"));
    QVERIFY(!source.isEmpty());
    const QString albumDir = m_music + QStringLiteral("/Pinned");
    const QString copy = albumDir + QStringLiteral("/01 Pinned.flac");
    QVERIFY(QDir().mkpath(albumDir));
    QVERIFY(QFile::copy(source, copy));
    QVERIFY(retagAlbum(copy, QStringLiteral("Pinned")));
    QVERIFY(bumpMtime(copy, 3600));

    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 1);
    const qint64 pinned = scalar(db, QStringLiteral("SELECT id FROM albums WHERE title = ?"),
                                 {QStringLiteral("Pinned")})
                              .toLongLong();
    QVERIFY(pinned > 0);

    PinsModel model(m_library.get());
    const int before = model.rowCount();
    QVERIFY(m_library->pin(QStringLiteral("album"), pinned));
    QCOMPARE(model.rowCount(), before + 1);

    QSignalSpy spy(m_library.get(), &Library::pinsChanged);
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 0);
    QCOMPARE(spy.count(), 0);
    QVERIFY(m_library->isPinned(QStringLiteral("album"), pinned));

    QVERIFY(QDir(albumDir).removeRecursively());
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(removed, 1);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_library->isPinned(QStringLiteral("album"), pinned));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM pins WHERE kind = ? AND ref_id = ?"),
                    {QStringLiteral("album"), pinned})
                 .toInt(),
             0);
    QCOMPARE(model.rowCount(), before);
}

void LibraryTest::rateAndDepth()
{
    const QSqlDatabase db = m_library->database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate = 44100")).toInt(),
             scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt());
    QVERIFY(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.flac' AND bit_depth = 8")).toInt() > 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.flac' AND bit_depth != 8")).toInt(), 0);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE path LIKE '%.mp3' AND bit_depth != 0")).toInt(), 0);
    const qint64 id = scalar(db, QStringLiteral("SELECT id FROM tracks WHERE path LIKE '%.flac' LIMIT 1")).toLongLong();
    const QVariantList rows = m_library->trackRows({id});
    QCOMPARE(rows.size(), 1);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("sampleRate")).toInt(), 44100);
    QCOMPARE(rows.first().toMap().value(QStringLiteral("bitDepth")).toInt(), 8);
}

void LibraryTest::migrateFrom2()
{
    const QString path = m_dir.path() + QStringLiteral("/migrate2.db");
    QVERIFY(QFile::copy(m_library->dbPath(), path));
    const QString setup = QStringLiteral("migrate2-setup");
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), setup);
        db.setDatabaseName(path);
        QVERIFY(db.open());
        QSqlQuery query(db);
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN sample_rate")));
        QVERIFY(query.exec(QStringLiteral("ALTER TABLE tracks DROP COLUMN bit_depth")));
        QVERIFY(query.exec(QStringLiteral("PRAGMA user_version = 2")));
        query.finish();
        db.close();
    }
    QSqlDatabase::removeDatabase(setup);

    Library library(path, m_dir.path() + QStringLiteral("/art"));
    QVERIFY(library.isOpen());
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("PRAGMA user_version")).toInt(), 3);
    const int total = scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE mtime = -1")).toInt(), total);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate IS NOT NULL")).toInt(), 0);

    QSignalSpy spy(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));
    QCOMPARE(spy.takeFirst().at(1).toInt(), total);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks WHERE sample_rate = 44100")).toInt(), total);

    QSignalSpy again(&library, &Library::scanned);
    library.rescan({m_music});
    QVERIFY(again.wait(scanTimeoutMs));
    const QList<QVariant> args = again.takeFirst();
    QCOMPARE(args.at(0).toInt() + args.at(1).toInt() + args.at(2).toInt(), 0);
}

void LibraryTest::playlists()
{
    QSqlDatabase db = m_library->database();
    QSqlQuery query(db);
    QVERIFY(query.exec(QStringLiteral("DELETE FROM playlist_items")));
    QVERIFY(query.exec(QStringLiteral("DELETE FROM playlists")));
    QCOMPARE(m_library->playlistCount(), 0);
    PlaylistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(id, name) VALUES (1, 'Morning')")));
    QVERIFY(query.exec(QStringLiteral("INSERT INTO playlists(id, name) VALUES (2, 'Drive')")));
    const QList<QVariant> ids = [&] {
        QList<QVariant> out;
        QSqlQuery q(QStringLiteral("SELECT id FROM tracks ORDER BY id LIMIT 3"), db);
        while (q.next())
            out.append(q.value(0));
        return out;
    }();
    QCOMPARE(ids.size(), 3);
    for (int i = 0; i < 3; i++) {
        QSqlQuery item(db);
        item.prepare(QStringLiteral("INSERT INTO playlist_items(playlist_id, track_id, pos) VALUES (1, ?, ?)"));
        item.addBindValue(ids.at(2 - i));
        item.addBindValue(i);
        QVERIFY(item.exec());
    }
    emit m_library->playlistsChanged();
    emit m_library->playlistCountChanged();
    QCOMPARE(m_library->playlistCount(), 2);
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0), model.roleNames().key("title")).toString(), QStringLiteral("Drive"));
    QCOMPARE(model.data(model.index(1), model.roleNames().key("title")).toString(), QStringLiteral("Morning"));
    QCOMPARE(model.data(model.index(1), model.roleNames().key("subtitle")).toString(), QStringLiteral("3 songs"));
    QCOMPARE(model.data(model.index(0), model.roleNames().key("subtitle")).toString(), QStringLiteral("0 songs"));
    QCOMPARE(model.data(model.index(0), model.roleNames().key("kind")).toString(), QStringLiteral("playlist"));
    const QVariantList tracks = m_library->playlistTracks(1);
    QCOMPARE(tracks.size(), 3);
    QCOMPARE(tracks.at(0).toLongLong(), ids.at(2).toLongLong());
    QCOMPARE(tracks.at(2).toLongLong(), ids.at(0).toLongLong());
    QCOMPARE(m_library->playlistTracks(9).size(), 0);
}

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
