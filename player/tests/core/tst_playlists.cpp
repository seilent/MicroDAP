#include "Favorites.h"
#include "Library.h"
#include "LibraryWatcher.h"
#include "PlaylistStore.h"
#include "Scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QtTest>

#include <cstdint>
#include <cstdio>
#include <iterator>

using namespace microdap;

namespace {

constexpr int scanTimeoutMs = 60000;
constexpr int watchMs = 50;

QByteArray str(const M3u &doc)
{
    return PlaylistStore::bytes(doc);
}

QString paths(const M3u &doc)
{
    QStringList out;
    for (const M3uItem &item : doc.items) {
        if (item.entry)
            out.append(item.path);
    }
    return out.join(QLatin1Char('|'));
}

QString texts(const M3u &doc)
{
    QStringList out;
    for (const M3uItem &item : doc.items) {
        if (item.entry)
            out.append(QString::fromUtf8(item.text));
    }
    return out.join(QLatin1Char('|'));
}

QString cues(const M3u &doc)
{
    QString out;
    for (const M3uItem &item : doc.items) {
        if (item.entry)
            out += QString::number(item.cue) + QLatin1Char(' ');
    }
    return out;
}

QList<qsizetype> entries(const M3u &doc)
{
    QList<qsizetype> at;
    for (qsizetype i = 0; i < doc.items.size(); ++i) {
        if (doc.items.at(i).entry)
            at.append(i);
    }
    return at;
}

bool writeBytes(const QString &path, const QByteArray &data)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(data) == data.size();
}

QByteArray readBytes(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool copyTree(const QString &from, const QString &to)
{
    QDirIterator it(from, QDir::Files | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QFileInfo info = it.nextFileInfo();
        const QString target = to + QLatin1Char('/') + QDir(from).relativeFilePath(info.absoluteFilePath());
        if (!QDir().mkpath(QFileInfo(target).absolutePath()) || !QFile::copy(info.absoluteFilePath(), target))
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

QString fakeMount;
QString fakeDevice;

bool fakeMountCheck(const QString &dir)
{
    return !fakeMount.isEmpty() && QDir::cleanPath(dir) == fakeMount;
}

QString fakeDeviceCheck(const QString &)
{
    return fakeDevice;
}

}

class PlaylistsTest : public QObject
{
    Q_OBJECT

private slots:
    void roundTrip();
    void edits();
    void cueDirectives();
    void fuzz();
    void spareAdoption();
    void caseFoldedDir();
    void ownWriteIgnored();
    void outsideEditKept();
    void cardPlaylists();
    void favoriteOrder();
};

void PlaylistsTest::roundTrip()
{
    const QByteArray raw = "\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:12,A - B\r\n../Music/a.flac\r\n# note\r\n\r\nb.flac\r\n"
                           "/other/x.flac\r\n/r/Music/c.flac";
    M3u m = PlaylistStore::parse(raw, QStringLiteral("/r/Playlists"));
    QCOMPARE(str(m), raw);
    QCOMPARE(m.eol, QByteArray("\r\n"));
    QCOMPARE(paths(m), QStringLiteral("/r/Music/a.flac|/r/Playlists/b.flac|/other/x.flac|/r/Music/c.flac"));
    QCOMPARE(PlaylistStore::remove(m, QStringLiteral("/r/Music/a.flac")), 1);
    QCOMPARE(str(m), QByteArray("\xEF\xBB\xBF#EXTM3U\r\n# note\r\n\r\nb.flac\r\n/other/x.flac\r\n/r/Music/c.flac"));
    PlaylistStore::insert(m, 0, QStringLiteral("/r/Music/d e.flac"), QStringLiteral("/r/Playlists"));
    QCOMPARE(str(m), QByteArray("\xEF\xBB\xBF#EXTM3U\r\n# note\r\n\r\n../Music/d e.flac\r\nb.flac\r\n/other/x.flac\r\n"
                                "/r/Music/c.flac"));
    PlaylistStore::insert(m, 99, QStringLiteral("/r/Playlists/f.flac"), QStringLiteral("/r/Playlists"));
    QVERIFY(str(m).endsWith("Music/c.flac\r\nf.flac\r\n"));

    M3u fresh;
    PlaylistStore::insert(fresh, 0, QStringLiteral("/r/Music/a.flac"), QStringLiteral("/r/Playlists"));
    QCOMPARE(str(fresh), QByteArray("#EXTM3U\n../Music/a.flac\n"));
    QCOMPARE(PlaylistStore::relative(QStringLiteral("/r/A/c"), QStringLiteral("/r/A/B")), QStringLiteral("../c"));
    QCOMPARE(PlaylistStore::relative(QStringLiteral("/r/#a.flac"), QStringLiteral("/r")), QStringLiteral("./#a.flac"));
    QCOMPARE(PlaylistStore::relative(QStringLiteral("/r/x/y.flac"), QStringLiteral("/r/x")), QStringLiteral("y.flac"));
    QCOMPARE(PlaylistStore::relative(QStringLiteral("/r/ a.flac"), QStringLiteral("/r")), QStringLiteral("./ a.flac"));
    QCOMPARE(PlaylistStore::entryPath("file:///r/f%20g.flac", QStringLiteral("/x")), QStringLiteral("/r/f g.flac"));
    QCOMPARE(PlaylistStore::entryPath("file://localhost/r/h.flac", QStringLiteral("/x")), QStringLiteral("/r/h.flac"));
    QCOMPARE(PlaylistStore::entryPath("sub\\c.flac", QStringLiteral("/r")), QStringLiteral("/r/sub/c.flac"));

    const QByteArray crlf = "\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:10,A\r\na.flac\r\n# c\r\n#EXTINF:20,B\r\nb.flac\r\n"
                            "gone.flac\r\nc.flac";
    M3u w = PlaylistStore::parse(crlf, QStringLiteral("/r"));
    const QList<qsizetype> at = entries(w);
    QCOMPARE(str(w), crlf);
    QCOMPARE(at.size(), 4);
    PlaylistStore::swap(w, at[0], at[1]);
    QCOMPARE(str(w), QByteArray("\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:20,B\r\nb.flac\r\n# c\r\n#EXTINF:10,A\r\na.flac\r\n"
                                "gone.flac\r\nc.flac"));
    PlaylistStore::swap(w, at[2], at[3]);
    QCOMPARE(str(w), QByteArray("\xEF\xBB\xBF#EXTM3U\r\n#EXTINF:20,B\r\nb.flac\r\n# c\r\n#EXTINF:10,A\r\na.flac\r\n"
                                "c.flac\r\ngone.flac\r\n"));
    PlaylistStore::insert(w, 99, QStringLiteral("/r/d.flac"), QStringLiteral("/r"));
    QCOMPARE(paths(w), QStringLiteral("/r/b.flac|/r/a.flac|/r/c.flac|/r/gone.flac|/r/d.flac"));
    QVERIFY(str(w).startsWith("\xEF\xBB\xBF"));
    QVERIFY(str(w).endsWith("d.flac\r\n"));
}

void PlaylistsTest::edits()
{
    M3u lone = PlaylistStore::parse("#EXTINF:1,x\n\n#EXTINF:2,y\n#EXTVLCOPT:a\nz.flac\n#EXTINF:3,w", QStringLiteral("/r"));
    QCOMPARE(str(lone), QByteArray("#EXTINF:1,x\n\n#EXTINF:2,y\n#EXTVLCOPT:a\nz.flac\n#EXTINF:3,w"));
    QCOMPARE(PlaylistStore::remove(lone, QStringLiteral("/r/z.flac")), 1);
    QCOMPARE(str(lone), QByteArray("#EXTINF:1,x\n\n#EXTINF:3,w"));

    M3u low = PlaylistStore::parse("#extinf:1,x\nz.flac\n", QStringLiteral("/r"));
    QCOMPARE(PlaylistStore::remove(low, QStringLiteral("/r/z.flac")), 1);
    QVERIFY(str(low).isEmpty());

    M3u orphan = PlaylistStore::parse("a.flac\n# c\n#EXTINF:9,orphan\n# d\n", QStringLiteral("/r"));
    PlaylistStore::insert(orphan, 99, QStringLiteral("/r/b.flac"), QStringLiteral("/r"));
    QCOMPARE(str(orphan), QByteArray("a.flac\n# c\nb.flac\n#EXTINF:9,orphan\n# d\n"));

    M3u two = PlaylistStore::parse("#EXTINF:1,old\n#EXTINF:2,a\na.flac\nb.flac\n", QStringLiteral("/r"));
    PlaylistStore::swap(two, 1, 2);
    QCOMPARE(str(two), QByteArray("b.flac\n#EXTINF:1,old\n#EXTINF:2,a\na.flac\n"));
    QCOMPARE(PlaylistStore::remove(two, QStringLiteral("/r/a.flac")), 1);
    QCOMPARE(str(two), QByteArray("b.flac\n"));

    M3u blank = PlaylistStore::parse("#EXTINF:1,x\n\n", QStringLiteral("/r"));
    PlaylistStore::insert(blank, 99, QStringLiteral("/r/b.flac"), QStringLiteral("/r"));
    QCOMPARE(str(blank), QByteArray("#EXTINF:1,x\n\nb.flac\n"));
}

void PlaylistsTest::cueDirectives()
{
    const M3u cue = PlaylistStore::parse("#EXTINF:1,a\n#FLATDAP-CUE:2\nimg.flac\n#FLATDAP-CUE:3\n#EXTINF:2,b\nimg.flac\n"
                                         "#FLATDAP-CUE:0\nx.flac\n#FLATDAP-CUE:100\ny.flac\n#flatdap-cue: 7 \nz.flac\n"
                                         "#FLATDAP-CUE:4\n\nw.flac\n",
                                         QStringLiteral("/r"));
    QCOMPARE(cues(cue), QStringLiteral("2 3 0 0 7 0 "));

    M3u cue2 = PlaylistStore::parse("#EXTINF:1,a\n#FLATDAP-CUE:2\nimg.flac\n#FLATDAP-CUE:3\n#EXTINF:2,b\nimg.flac\nx.flac\n",
                                    QStringLiteral("/r"));
    const QList<qsizetype> at = entries(cue2);
    QCOMPARE(at.size(), 3);
    QCOMPARE(PlaylistStore::unit(cue2, at[1]), at[0] + 1);
    PlaylistStore::swap(cue2, at[1], at[2]);
    QCOMPARE(str(cue2), QByteArray("#EXTINF:1,a\n#FLATDAP-CUE:2\nimg.flac\nx.flac\n#FLATDAP-CUE:3\n#EXTINF:2,b\nimg.flac\n"));
    PlaylistStore::erase(cue2, 0);
    QCOMPARE(str(cue2), QByteArray("x.flac\n#FLATDAP-CUE:3\n#EXTINF:2,b\nimg.flac\n"));

    M3u cue3 = PlaylistStore::parse("a.flac\n#FLATDAP-CUE:5\n", QStringLiteral("/r"));
    PlaylistStore::insert(cue3, 99, QStringLiteral("/r/Music/img.flac"), QStringLiteral("/r/Playlists"), 12);
    QCOMPARE(str(cue3), QByteArray("a.flac\n#FLATDAP-CUE:12\n../Music/img.flac\n#FLATDAP-CUE:5\n"));
    QCOMPARE(cue3.items.at(1).cue, 12);
    QCOMPARE(str(PlaylistStore::parse(str(cue3), QStringLiteral("/r"))), str(cue3));
}

void PlaylistsTest::fuzz()
{
    std::uint32_t seed = 777;
    const auto rnd = [&seed] {
        seed = seed * 1103515245u + 12345u;
        return seed >> 8;
    };
    const char *parts[] = {"#EXTM3U", "#EXTINF:1,x", "#extinf:-1,", "# c", "#EXTVLCOPT:a", "a.flac", "../b.flac",
                           "sub\\c.flac", "/r/d.flac", "/x/e.flac", "file:///r/f%20g.flac", " h.flac", "",
                           "\xC3\xA9.flac", "..", "#", "#FLATDAP-CUE:2", "#flatdap-cue:17", "#FLATDAP-CUE:x",
                           "#FLATDAP-CUE:0", "#EXTINF:3,T\xC3\xADtulo - Art, ist"};
    const char *ends[] = {"\n", "\r\n", "\r", ""};
    const char *adds[] = {"/r/Music/a.flac", "/r/Playlists/b.flac", "/r/x y/#z.flac", "/r/ lead.flac"};
    for (int i = 0; i < 4000; ++i) {
        QByteArray text;
        if (rnd() % 4 == 0)
            text += "\xEF\xBB\xBF";
        const int lines = static_cast<int>(rnd() % 12);
        for (int k = 0; k < lines; ++k) {
            text += parts[rnd() % std::size(parts)];
            if (rnd() % 8 == 0)
                text += static_cast<char>(rnd());
            text += ends[rnd() % std::size(ends)];
        }
        const QString dir = rnd() % 2 ? QStringLiteral("/r/Playlists") : QStringLiteral("/r");
        M3u f = PlaylistStore::parse(text, dir);
        QVERIFY2(str(f) == text, qPrintable(QString::number(i)));
        for (int op = 0; op < 6; ++op) {
            const QList<qsizetype> at = entries(f);
            const int what = static_cast<int>(rnd() % 3);
            if (what == 0 && at.size() >= 2) {
                const qsizetype a = at[rnd() % at.size()];
                PlaylistStore::swap(f, a, at[rnd() % at.size()]);
            } else if (what == 1 && !at.isEmpty()) {
                PlaylistStore::erase(f, at[rnd() % at.size()]);
            } else {
                const qsizetype before = rnd() % 6;
                const QString add = QString::fromUtf8(adds[rnd() % std::size(adds)]);
                PlaylistStore::insert(f, before, add, dir, rnd() % 3 ? 0 : 1 + static_cast<int>(rnd() % 99));
            }
        }
        const QByteArray out = str(f);
        const M3u back = PlaylistStore::parse(out, dir);
        QVERIFY2(str(back) == out && paths(back) == paths(f) && texts(back) == texts(f) && cues(back) == cues(f),
                 qPrintable(QString::number(i) + QLatin1Char(' ') + texts(back) + QStringLiteral(" / ") + texts(f)));
    }
}

void PlaylistsTest::spareAdoption()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Playlists/x.m3u8"));
    const QByteArray body = "#EXTM3U\r\n../a.flac\r\n";
    QVERIFY(writeBytes(path + QStringLiteral(".tmp"), body));
    PlaylistStore store;
    const PlaylistStore::Loaded loaded = store.read(path);
    QCOMPARE(loaded.state, PlaylistStore::State::Read);
    QCOMPARE(str(loaded.doc), body);
    QCOMPARE(readBytes(path), body);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));

    QVERIFY(writeBytes(path + QStringLiteral(".new"), "stale\n"));
    QVERIFY(writeBytes(path + QStringLiteral(".tmp"), "stale\n"));
    QCOMPARE(str(store.read(path).doc), body);
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".new")));
    QVERIFY(!QFileInfo::exists(path + QStringLiteral(".tmp")));

    const QString lone = dir.filePath(QStringLiteral("Playlists/y.m3u8"));
    QVERIFY(writeBytes(lone + QStringLiteral(".new"), "y.flac\n"));
    QCOMPARE(store.read(lone).state, PlaylistStore::State::Read);
    QCOMPARE(readBytes(lone), QByteArray("y.flac\n"));
    QCOMPARE(QDir(dir.filePath(QStringLiteral("Playlists"))).entryList(QDir::Files | QDir::Hidden),
             (QStringList{QStringLiteral("x.m3u8"), QStringLiteral("y.m3u8")}));

    QCOMPARE(store.read(dir.filePath(QStringLiteral("Playlists/none.m3u8"))).state, PlaylistStore::State::Missing);
    const QString big = dir.filePath(QStringLiteral("Playlists/big.m3u8"));
    QVERIFY(writeBytes(big, QByteArray(PlaylistStore::maxBytes + 1, 'a')));
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("big.m3u8")));
    QCOMPARE(store.read(big).state, PlaylistStore::State::Failed);
    QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("big.m3u8")));
    QCOMPARE(store.edit(big, [](M3u &) { return true; }), PlaylistStore::Edit::Unreadable);
}

void PlaylistsTest::caseFoldedDir()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QCOMPARE(PlaylistStore::playlistsDir(dir.path()), dir.filePath(QStringLiteral("Playlists")));
    QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("playlists")));
    QCOMPARE(PlaylistStore::playlistsDir(dir.path()), dir.filePath(QStringLiteral("playlists")));
    QVERIFY(writeBytes(dir.filePath(QStringLiteral("playlists/Favorites.M3U8")), "a.flac\n"));
    QCOMPARE(PlaylistStore::findFile(dir.filePath(QStringLiteral("playlists")), QStringLiteral("favorites.m3u8")),
             dir.filePath(QStringLiteral("playlists/Favorites.M3U8")));
    QCOMPARE(PlaylistStore::findFile(dir.filePath(QStringLiteral("playlists")), QStringLiteral("other.m3u8")),
             dir.filePath(QStringLiteral("playlists/other.m3u8")));
}

void PlaylistsTest::ownWriteIgnored()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString music = dir.filePath(QStringLiteral("music"));
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Single"), music + QStringLiteral("/Single")));
    const QString path = music + QStringLiteral("/mix.m3u8");
    QVERIFY(writeBytes(path, "#EXTM3U\n"));
    Library library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")));
    QVERIFY(library.isOpen());
    LibraryWatcher watcher(&library);
    watcher.setDebounceMs(watchMs);
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(scanned.wait(scanTimeoutMs));
    QVERIFY(watcher.watched().contains(music));
    QCOMPARE(scalar(library.database(), QStringLiteral("SELECT COUNT(*) FROM playlist_items")).toInt(), 0);

    QSignalSpy triggered(&watcher, &LibraryWatcher::triggered);
    QSignalSpy changed(&library, &Library::playlistsChanged);
    const QString song = music + QStringLiteral("/Single/01 Single.mp3");
    QTest::ignoreMessage(QtWarningMsg, "microdap library: rescan without settings");
    const auto add = [&song, &music](M3u &doc) {
        PlaylistStore::insert(doc, 99, song, music);
        return true;
    };
    QCOMPARE(library.editPlaylist(path, add), PlaylistStore::Edit::Done);
    QCOMPARE(readBytes(path), QByteArray("#EXTM3U\nSingle/01 Single.mp3\n"));
    QCOMPARE(changed.count(), 1);
    QCOMPARE(scalar(library.database(), QStringLiteral("SELECT COUNT(*) FROM playlist_items")).toInt(), 1);
    QCOMPARE(scalar(library.database(), QStringLiteral("SELECT size FROM playlists WHERE source = ?"), {path})
                 .toLongLong(),
             QFileInfo(path).size());
    QTest::qWait(watchMs * 6);
    QCOMPARE(triggered.count(), 0);

    const QString hand = dir.filePath(QStringLiteral("mix.m3u8.hand"));
    QVERIFY(writeBytes(hand, "#EXTM3U\n"));
    QCOMPARE(::rename(QFile::encodeName(hand).constData(), QFile::encodeName(path).constData()), 0);
    QTRY_COMPARE_WITH_TIMEOUT(triggered.count(), 1, watchMs * 40);
    QTest::qWait(watchMs * 6);
    triggered.clear();

    QVERIFY(writeBytes(music + QStringLiteral("/other.m3u8"), "#EXTM3U\nSingle/01 Single.mp3\n"));
    QTest::ignoreMessage(QtWarningMsg, "microdap library: rescan without settings");
    QCOMPARE(library.editPlaylist(path, add), PlaylistStore::Edit::Done);
    QTRY_COMPARE_WITH_TIMEOUT(triggered.count(), 1, watchMs * 40);
}

void PlaylistsTest::outsideEditKept()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString path = dir.filePath(QStringLiteral("Playlists/x.m3u8"));
    QVERIFY(writeBytes(path, "#EXTM3U\na.flac\n"));
    PlaylistStore store;
    const QString lists = dir.filePath(QStringLiteral("Playlists"));
    const auto append = [&lists](M3u &doc) {
        PlaylistStore::insert(doc, 99, lists + QStringLiteral("/c.flac"), lists);
        return true;
    };
    QCOMPARE(store.edit(path, append), PlaylistStore::Edit::Done);
    QVERIFY(store.ownChange(lists));
    QVERIFY(writeBytes(path, "#EXTM3U\na.flac\n# hand\nb.flac\nc.flac\n"));
    QVERIFY(!store.ownChange(lists));
    const auto drop = [&lists](M3u &doc) { return PlaylistStore::remove(doc, lists + QStringLiteral("/a.flac")) > 0; };
    QCOMPARE(store.edit(path, drop), PlaylistStore::Edit::Done);
    QCOMPARE(readBytes(path), QByteArray("#EXTM3U\n# hand\nb.flac\nc.flac\n"));
    QCOMPARE(store.edit(path, [](M3u &) { return false; }), PlaylistStore::Edit::Done);
    QCOMPARE(readBytes(path), QByteArray("#EXTM3U\n# hand\nb.flac\nc.flac\n"));
}

void PlaylistsTest::cardPlaylists()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString base = dir.filePath(QStringLiteral("media"));
    const QString card = base + QStringLiteral("/micro/CARD");
    const QString music = card + QStringLiteral("/Music");
    QVERIFY(copyTree(QStringLiteral(MICRODAP_FIXTURE_MUSIC "/Single"), music + QStringLiteral("/Single")));
    QVERIFY(writeBytes(card + QStringLiteral("/Playlists/road.m3u8"), "#EXTM3U\r\n../Music/Single/01 Single.mp3\r\n"));
    fakeDevice = dir.filePath(QStringLiteral("dev"));
    QVERIFY(writeBytes(fakeDevice, QByteArray()));
    fakeMount = card;
    qputenv("MICRODAP_MEDIA_ROOT", QFile::encodeName(base));
    Scanner::setMountCheck(fakeMountCheck);
    Scanner::setDeviceCheck(fakeDeviceCheck);
    const auto restore = qScopeGuard([] {
        Scanner::setMountCheck(nullptr);
        Scanner::setDeviceCheck(nullptr);
        qunsetenv("MICRODAP_MEDIA_ROOT");
        fakeMount.clear();
    });

    Library library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")));
    QVERIFY(library.isOpen());
    LibraryWatcher watcher(&library);
    watcher.setDebounceMs(watchMs);
    QSignalSpy scanned(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(scanned.wait(scanTimeoutMs));
    const QSqlDatabase db = library.database();
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists WHERE source = ?"),
                    {card + QStringLiteral("/Playlists/road.m3u8")})
                 .toInt(),
             1);
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlist_items")).toInt(), 1);
    QVERIFY(library.scannedDirectories().contains(card + QStringLiteral("/Playlists")));
    QVERIFY(watcher.watched().contains(card + QStringLiteral("/Playlists")));

    QSignalSpy triggered(&watcher, &LibraryWatcher::triggered);
    QTest::ignoreMessage(QtWarningMsg, "microdap library: rescan without settings");
    QVERIFY(writeBytes(card + QStringLiteral("/Playlists/night.m3u8"), "../Music/Single/01 Single.mp3\n"));
    QTRY_COMPARE_WITH_TIMEOUT(triggered.count(), 1, watchMs * 40);

    QSignalSpy again(&library, &Library::scanned);
    library.rescan({music});
    QVERIFY(again.wait(scanTimeoutMs));
    QCOMPARE(scalar(db, QStringLiteral("SELECT COUNT(*) FROM playlists WHERE source LIKE ?"),
                    {card + QStringLiteral("/Playlists/%")})
                 .toInt(),
             2);
}

void PlaylistsTest::favoriteOrder()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    Library library(dir.filePath(QStringLiteral("library.db")), dir.filePath(QStringLiteral("art")));
    QVERIFY(library.isOpen());
    const QString internal = QStringLiteral("internal");
    const QString card = QStringLiteral("CARD");
    QVERIFY(library.stampFavorites(internal, {{QStringLiteral("../e.flac"), 0}}));
    QVERIFY(library.stampFavorites(internal, {{QStringLiteral("../a.flac"), 0}}));
    QVERIFY(library.stampFavorites(card, {{QStringLiteral("../c.flac"), 0}}));
    const QList<std::pair<QString, QStringList>> media{
        {internal, {QStringLiteral("../a.flac"), QStringLiteral("../b.flac"), QStringLiteral("../e.flac")}},
        {card, {QStringLiteral("../c.flac"), QStringLiteral("../d.flac")}}};
    QList<qint64> seqs;
    QStringList names;
    for (const auto &[medium, files] : media) {
        const QHash<QString, qint64> stamps = library.favoriteStamps(medium);
        for (const QString &file : files) {
            seqs.append(stamps.value(Library::favoriteKey(file, 0), -1));
            names.append(medium + QLatin1Char(':') + file);
        }
    }
    QStringList ordered;
    for (qsizetype i : Favorites::order(seqs))
        ordered.append(names.at(i));
    QCOMPARE(ordered, (QStringList{QStringLiteral("CARD:../c.flac"), QStringLiteral("internal:../a.flac"),
                                   QStringLiteral("internal:../e.flac"), QStringLiteral("internal:../b.flac"),
                                   QStringLiteral("CARD:../d.flac")}));
    QCOMPARE(Favorites::order({}), QList<qsizetype>());
    QCOMPARE(Favorites::order({-1, -1, -1}), (QList<qsizetype>{0, 1, 2}));
}

QTEST_GUILESS_MAIN(PlaylistsTest)
#include "tst_playlists.moc"
