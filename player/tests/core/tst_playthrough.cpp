#include "Library.h"
#include "Models.h"
#include "PlayThrough.h"
#include "Queue.h"

#include <QElapsedTimer>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>

using namespace microdap;

namespace {

QList<QueueEntry> entries(int n)
{
    QList<QueueEntry> out;
    for (int i = 0; i < n; ++i) {
        QueueEntry e;
        e.id = i + 1;
        e.path = QStringLiteral("/m/A/%1.flac").arg(i);
        out.append(e);
    }
    return out;
}

}

class PlayThroughTest : public QObject
{
    Q_OBJECT

private slots:
    void dueOnlyAtTheLastTrack();
    void foldersDepthFirst();
    void foldersAcrossRoots();
    void nameOrder();
    void appendsNextFolder();
    void appendsNextAlbum();
    void folderBudget();

private:
    static bool fill(Library &library, const QList<std::pair<QString, QString>> &tracks);
    static QList<QueueEntry> entriesOf(Library &library, const QList<qint64> &ids);
};

bool PlayThroughTest::fill(Library &library, const QList<std::pair<QString, QString>> &tracks)
{
    QSqlDatabase db = library.database();
    db.transaction();
    QSqlQuery album(db);
    album.prepare(QStringLiteral("INSERT OR IGNORE INTO albums(title, artist, added) VALUES (?, 'A', 0)"));
    QSqlQuery track(db);
    track.prepare(QStringLiteral("INSERT INTO tracks(path, mtime, size, title, artist, album_artist, album_id, added) "
                                 "VALUES (?, 0, 0, ?, 'A', 'A', (SELECT id FROM albums WHERE title = ?), 0)"));
    for (const auto &[path, title] : tracks) {
        album.bindValue(0, title);
        if (!album.exec())
            return false;
        track.bindValue(0, path);
        track.bindValue(1, path.section(QLatin1Char('/'), -1));
        track.bindValue(2, title);
        if (!track.exec())
            return false;
    }
    return db.commit();
}

QList<QueueEntry> PlayThroughTest::entriesOf(Library &library, const QList<qint64> &ids)
{
    QVariantList list;
    for (qint64 id : ids)
        list.append(id);
    return library.queueEntries(list);
}

void PlayThroughTest::dueOnlyAtTheLastTrack()
{
    Queue queue;
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("albums")));
    queue.set(entries(3), 1);
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("albums")));
    queue.setCurrentIndex(2);
    QVERIFY(PlayThrough::due(queue, QStringLiteral("albums")));
    QVERIFY(PlayThrough::due(queue, QStringLiteral("folders")));
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("off")));
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("genres")));
    queue.setRepeat(Queue::All);
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("albums")));
    queue.setRepeat(Queue::One);
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("albums")));
    queue.setRepeat(Queue::Off);
    queue.setShuffle(true);
    QVERIFY(!PlayThrough::due(queue, QStringLiteral("albums")));
}

void PlayThroughTest::foldersDepthFirst()
{
    const QStringList roots{QStringLiteral("/m")};
    const QStringList dirs{QStringLiteral("/m/A"), QStringLiteral("/m/A/b"), QStringLiteral("/m/A/C"),
                           QStringLiteral("/m/A/C/x"), QStringLiteral("/m/B"), QStringLiteral("/m")};
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m/A/b")), QStringLiteral("/m/A/C/x"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m/A/C/x")), QStringLiteral("/m/A/C"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m/A/C")), QStringLiteral("/m/A"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m/A")), QStringLiteral("/m/B"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m/B")), QStringLiteral("/m"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/m")), QString());
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/elsewhere")), QString());
}

void PlayThroughTest::foldersAcrossRoots()
{
    const QStringList roots{QStringLiteral("/home/micro/Music"), QStringLiteral("/run/media/card/Music")};
    const QStringList dirs{QStringLiteral("/run/media/card/Music/Z"), QStringLiteral("/home/micro/Music/Y"),
                           QStringLiteral("/tmp/other")};
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/home/micro/Music/Y")),
             QStringLiteral("/run/media/card/Music/Z"));
    QCOMPARE(PlayThrough::nextDir(dirs, roots, QStringLiteral("/run/media/card/Music/Z")), QString());
}

void PlayThroughTest::nameOrder()
{
    const QStringList roots{QStringLiteral("/m")};
    QVERIFY(PlayThrough::dirBefore(QStringLiteral("/m/apple"), QStringLiteral("/m/Banana"), roots));
    QVERIFY(PlayThrough::dirBefore(QStringLiteral("/m/B"), QStringLiteral("/m/b"), roots));
    QVERIFY(!PlayThrough::dirBefore(QStringLiteral("/m/b"), QStringLiteral("/m/B"), roots));
    QVERIFY(PlayThrough::dirBefore(QStringLiteral("/m/a/deep"), QStringLiteral("/m/a"), roots));
    QVERIFY(PlayThrough::dirBefore(QStringLiteral("/m/a"), QStringLiteral("/m/B"), roots));
}

void PlayThroughTest::appendsNextFolder()
{
    QTemporaryDir dir;
    Library library(dir.filePath(QStringLiteral("library.db")));
    QVERIFY(fill(library, {{QStringLiteral("/m/F1/a.flac"), QStringLiteral("Alpha")},
                           {QStringLiteral("/m/F1/b.flac"), QStringLiteral("Alpha")},
                           {QStringLiteral("/m/F2/c.flac"), QStringLiteral("Beta")},
                           {QStringLiteral("/m/F2/d.flac"), QStringLiteral("Beta")},
                           {QStringLiteral("/other/e.flac"), QStringLiteral("Beta")}}));
    AlbumsModel albums(&library);
    Queue queue;
    PlayThrough through(&queue, &library, &albums);
    through.setRoots({QStringLiteral("/m")});
    through.setMode(QStringLiteral("folders"));
    queue.set(entriesOf(library, {1, 2}), 1);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 4);
    QCOMPARE(queue.entry(2).path, QStringLiteral("/m/F2/c.flac"));
    QCOMPARE(queue.entry(3).path, QStringLiteral("/m/F2/d.flac"));
    queue.setCurrentIndex(3);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 4);
    queue.set(entriesOf(library, {1, 2}), 1);
    queue.setRepeat(Queue::All);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 2);
    queue.setRepeat(Queue::Off);
    queue.setShuffle(true);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 2);
    queue.setShuffle(false);
    through.setMode(QStringLiteral("off"));
    through.onTrackStarted();
    QCOMPARE(queue.count(), 2);
}

void PlayThroughTest::appendsNextAlbum()
{
    QTemporaryDir dir;
    Library library(dir.filePath(QStringLiteral("library.db")));
    QVERIFY(fill(library, {{QStringLiteral("/m/X/a.flac"), QStringLiteral("Beta")},
                           {QStringLiteral("/m/Y/b.flac"), QStringLiteral("Alpha")},
                           {QStringLiteral("/m/Y/c.flac"), QStringLiteral("Alpha")}}));
    AlbumsModel albums(&library);
    Queue queue;
    PlayThrough through(&queue, &library, &albums);
    through.setRoots({QStringLiteral("/m")});
    through.setMode(QStringLiteral("albums"));
    queue.set(entriesOf(library, {2, 3}), 1);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 3);
    QCOMPARE(queue.entry(2).path, QStringLiteral("/m/X/a.flac"));
    queue.setCurrentIndex(2);
    through.onTrackStarted();
    QCOMPARE(queue.count(), 3);
}

void PlayThroughTest::folderBudget()
{
    QTemporaryDir dir;
    Library library(dir.filePath(QStringLiteral("library.db")));
    QList<std::pair<QString, QString>> tracks;
    for (int f = 0; f < 840; ++f) {
        for (int t = 0; t < 10; ++t)
            tracks.append({QStringLiteral("/m/Artist %1/Album %2/%3.flac").arg(f / 10).arg(f).arg(t),
                           QStringLiteral("Album %1").arg(f)});
    }
    QVERIFY(fill(library, tracks));
    AlbumsModel albums(&library);
    Queue queue;
    PlayThrough through(&queue, &library, &albums);
    through.setRoots({QStringLiteral("/m")});
    through.setMode(QStringLiteral("folders"));
    queue.set(entriesOf(library, {1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 9);
    QElapsedTimer timer;
    timer.start();
    through.onTrackStarted();
    const qint64 ms = timer.elapsed();
    QCOMPARE(queue.count(), 20);
    qInfo("play through folders, 8400 tracks: %lld ms", ms);
    QVERIFY2(ms < 250, qPrintable(QString::number(ms)));
}

QTEST_GUILESS_MAIN(PlayThroughTest)
#include "tst_playthrough.moc"
