#include "Library.h"
#include "Models.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <limits>
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

QHash<QString, qint64> addedByPath(const QSqlDatabase &db)
{
    QHash<QString, qint64> map;
    QSqlQuery query(QStringLiteral("SELECT path, added FROM tracks"), db);
    while (query.next())
        map.insert(query.value(0).toString(), query.value(1).toLongLong());
    return map;
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

private:
    bool scan(int &added, int &updated, int &removed);
    QString firstTrackOf(const QString &album) const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
};

bool LibraryTest::scan(int &added, int &updated, int &removed)
{
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescan({m_music});
    if (!spy.wait(scanTimeoutMs))
        return false;
    const QList<QVariant> args = spy.takeFirst();
    added = args.at(0).toInt();
    updated = args.at(1).toInt();
    removed = args.at(2).toInt();
    return true;
}

QString LibraryTest::firstTrackOf(const QString &album) const
{
    return scalar(m_library->database(),
                  QStringLiteral("SELECT t.path FROM tracks t JOIN albums a ON a.id = t.album_id "
                                 "WHERE a.title = ? ORDER BY t.track_no LIMIT 1"),
                  {album})
        .toString();
}

void LibraryTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(CASDAP_FIXTURE_MUSIC), m_music));

    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"));
    QVERIFY(m_library->isOpen());

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
    const QString embedded = scalar(db, QStringLiteral("SELECT art_path FROM albums WHERE title = ?"),
                                    {QStringLiteral("Album One")})
                                 .toString();
    QVERIFY(!embedded.isEmpty());
    QVERIFY(QFileInfo::exists(embedded));
    QVERIFY(embedded.startsWith(m_library->artDir()));
    QVERIFY(embedded.endsWith(QLatin1StringView(".png")));

    const QString folder = scalar(db, QStringLiteral("SELECT art_path FROM albums WHERE title = ?"),
                                  {QStringLiteral("アルバム二")})
                               .toString();
    QVERIFY(!folder.isEmpty());
    QVERIFY(QFileInfo::exists(folder));
    QCOMPARE(QFileInfo(folder).fileName(), QStringLiteral("cover.png"));

    QVERIFY(scalar(db, QStringLiteral("SELECT art_path FROM albums WHERE title = ?"),
                   {QStringLiteral("Single")})
                .isNull());
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

    qint64 previous = std::numeric_limits<qint64>::max();
    for (int row = 0; row < model.rowCount(); ++row) {
        const qint64 added = scalar(m_library->database(),
                                    QStringLiteral("SELECT added FROM albums WHERE id = ?"),
                                    {model.data(model.index(row), LibraryModel::IdRole)})
                                 .toLongLong();
        QVERIFY(added > 0);
        QVERIFY(added <= previous);
        previous = added;
    }
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

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(QDateTime::currentDateTimeUtc().addSecs(3600),
                             QFileDevice::FileModificationTime));
    file.close();

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

QTEST_GUILESS_MAIN(LibraryTest)
#include "tst_library.moc"
