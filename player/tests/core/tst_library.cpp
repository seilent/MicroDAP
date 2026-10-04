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

bool retagAlbum(const QString &path, const QString &album)
{
    TagLib::FLAC::File file(QFile::encodeName(path).constData());
    if (!file.isValid())
        return false;
    TagLib::PropertyMap props = file.properties();
    props["ALBUM"] = TagLib::StringList(
        TagLib::String(album.toUtf8().constData(), TagLib::String::UTF8));
    file.setProperties(props);
    return file.save();
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
    void history();
    void newAlbums();
    void rating();
    void touched();
    void removed();
    void missingRoot();
    void artContent();

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
    QCOMPARE(scalar(m_library->database(), QStringLiteral("PRAGMA user_version")).toInt(), 1);

    QSignalSpy scanningSpy(m_library.get(), &Library::scanningChanged);
    QSignalSpy changedSpy(m_library.get(), &Library::changed);
    int added = 0;
    int updated = 0;
    int removed = 0;
    QVERIFY(scan(added, updated, removed));
    QCOMPARE(added, 7);
    QCOMPARE(updated, 0);
    QCOMPARE(removed, 0);
    QCOMPARE(scanningSpy.count(), 2);
    QCOMPARE(changedSpy.count(), 1);
    QVERIFY(!m_library->scanning());
}

void LibraryTest::counts()
{
    const QVariantMap counts = m_library->counts();
    QCOMPARE(counts.value(QStringLiteral("tracks")).toInt(), 7);
    QCOMPARE(counts.value(QStringLiteral("albums")).toInt(), 3);
    QCOMPARE(counts.value(QStringLiteral("artists")).toInt(), 3);
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
    QCOMPARE(before.size(), 7);

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
    QCOMPARE(model.rowCount(), 7);
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
             QStringLiteral("3 songs"));
    QVERIFY(model.roleNames().value(LibraryModel::TitleRole) == QByteArrayLiteral("name"));
}

void LibraryTest::artists()
{
    ArtistsModel model(m_library.get());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(model.data(model.index(0), LibraryModel::TitleRole).toString(), QStringLiteral("アーティスト"));
    QCOMPARE(model.data(model.index(1), LibraryModel::TitleRole).toString(), QStringLiteral("Artist A"));
    QCOMPARE(model.data(model.index(2), LibraryModel::TitleRole).toString(), QStringLiteral("The Band"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SectionRole).toString(), QStringLiteral("B"));
    QCOMPARE(model.data(model.index(2), LibraryModel::SubtitleRole).toString(), QStringLiteral("1 album"));
    QVERIFY(!model.data(model.index(1), LibraryModel::ArtPathRole).toString().isEmpty());
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
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 6);
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
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);

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
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 7);
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
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM tracks")).toInt(), 6);
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

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
