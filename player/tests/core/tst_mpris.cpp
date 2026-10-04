#include "Library.h"
#include "Mpris.h"
#include "Player.h"
#include "Queue.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusInterface>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUrl>
#include <QtTest>

#include <gst/gst.h>

#include <memory>

using namespace casdap;

namespace {

constexpr int scanTimeoutMs = 60000;
constexpr int busTimeoutMs = 5000;

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

QList<QueueEntry> albumEntries(const QSqlDatabase &db, const QString &album)
{
    QList<QueueEntry> entries;
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT t.id, t.path, t.title, t.artist, a.title, a.art_path, t.duration_ms "
        "FROM tracks t JOIN albums a ON a.id = t.album_id WHERE a.title = ? ORDER BY t.track_no"));
    query.addBindValue(album);
    if (!query.exec())
        return entries;
    while (query.next()) {
        QueueEntry entry;
        entry.id = query.value(0).toLongLong();
        entry.path = query.value(1).toString();
        entry.title = query.value(2).toString();
        entry.artist = query.value(3).toString();
        entry.album = query.value(4).toString();
        entry.artPath = query.value(5).toString();
        entry.durationMs = query.value(6).toLongLong();
        entries.append(entry);
    }
    return entries;
}

}

class MprisTest : public QObject
{
    Q_OBJECT

public slots:
    void onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                             const QStringList &invalidated);
    void onSeeked(qlonglong position);

private slots:
    void initTestCase();
    void init();
    void registered();
    void identity();
    void metadata();
    void playPause();
    void loopStatus();
    void seek();
    void openUri();

private:
    QString playbackStatus() const;
    QVariantMap metadataMap() const;
    bool sawChange(const QString &name, const QVariant &value) const;

    QTemporaryDir m_dir;
    QString m_music;
    std::unique_ptr<Library> m_library;
    std::unique_ptr<Player> m_player;
    std::unique_ptr<Mpris> m_mpris;
    std::unique_ptr<QDBusInterface> m_root;
    std::unique_ptr<QDBusInterface> m_remote;
    QList<QueueEntry> m_tracks;
    QList<QVariantMap> m_changes;
    QList<qlonglong> m_seeked;
};

void MprisTest::onPropertiesChanged(const QString &interface, const QVariantMap &changed,
                                    const QStringList &invalidated)
{
    Q_UNUSED(invalidated)
    if (interface == Mpris::playerInterface())
        m_changes.append(changed);
}

void MprisTest::onSeeked(qlonglong position)
{
    m_seeked.append(position);
}

QString MprisTest::playbackStatus() const
{
    return m_remote->property("PlaybackStatus").toString();
}

QVariantMap MprisTest::metadataMap() const
{
    return qdbus_cast<QVariantMap>(m_remote->property("Metadata"));
}

bool MprisTest::sawChange(const QString &name, const QVariant &value) const
{
    for (const QVariantMap &change : m_changes) {
        if (change.contains(name) && change.value(name) == value)
            return true;
    }
    return false;
}

void MprisTest::initTestCase()
{
    gst_init(nullptr, nullptr);
    if (!qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
        qputenv("CASDAP_AUDIO_SINK", QByteArrayLiteral("fakesink sync=true"));

    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        QSKIP("no session bus, run under dbus-run-session");

    QVERIFY(m_dir.isValid());
    m_music = m_dir.path() + QStringLiteral("/music");
    QVERIFY(copyTree(QStringLiteral(CASDAP_FIXTURE_MUSIC), m_music));

    m_library = std::make_unique<Library>(m_dir.path() + QStringLiteral("/library.db"),
                                          m_dir.path() + QStringLiteral("/art"));
    QVERIFY(m_library->isOpen());
    QSignalSpy spy(m_library.get(), &Library::scanned);
    m_library->rescan({m_music});
    QVERIFY(spy.wait(scanTimeoutMs));

    m_tracks = albumEntries(m_library->database(), QStringLiteral("Album One"));
    QCOMPARE(m_tracks.size(), 3);

    m_player = std::make_unique<Player>(m_library.get());
    QVERIFY(m_player->isValid());
    m_mpris = std::make_unique<Mpris>(m_player.get(), bus);
    QVERIFY(m_mpris->isRegistered());

    QVERIFY(bus.connect(Mpris::serviceName(), Mpris::objectPath(),
                        QStringLiteral("org.freedesktop.DBus.Properties"),
                        QStringLiteral("PropertiesChanged"), this,
                        SLOT(onPropertiesChanged(QString,QVariantMap,QStringList))));
    QVERIFY(bus.connect(Mpris::serviceName(), Mpris::objectPath(), Mpris::playerInterface(),
                        QStringLiteral("Seeked"), this, SLOT(onSeeked(qlonglong))));

    m_root = std::make_unique<QDBusInterface>(Mpris::serviceName(), Mpris::objectPath(),
                                              Mpris::rootInterface(), bus);
    QVERIFY2(m_root->isValid(), qPrintable(m_root->lastError().message()));
    m_remote = std::make_unique<QDBusInterface>(Mpris::serviceName(), Mpris::objectPath(),
                                                Mpris::playerInterface(), bus);
    QVERIFY2(m_remote->isValid(), qPrintable(m_remote->lastError().message()));
}

void MprisTest::init()
{
    m_player->stop();
    m_player->queue()->setRepeat(Queue::Off);
    m_player->queue()->setShuffle(false);
    m_player->queue()->set(m_tracks, 0);
    QCoreApplication::processEvents();
    m_changes.clear();
    m_seeked.clear();
}

void MprisTest::registered()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    QDBusReply<bool> owned = bus.interface()->isServiceRegistered(Mpris::serviceName());
    QVERIFY2(owned.isValid(), qPrintable(owned.error().message()));
    QVERIFY(owned.value());

    QDBusInterface introspectable(Mpris::serviceName(), Mpris::objectPath(),
                                  QStringLiteral("org.freedesktop.DBus.Introspectable"), bus);
    QDBusReply<QString> xml = introspectable.call(QStringLiteral("Introspect"));
    QVERIFY2(xml.isValid(), qPrintable(xml.error().message()));
    QVERIFY(xml.value().contains(QStringLiteral("org.mpris.MediaPlayer2\"")));
    QVERIFY(xml.value().contains(QStringLiteral("org.mpris.MediaPlayer2.Player")));
    QVERIFY(xml.value().contains(QStringLiteral("PlayPause")));
    QVERIFY(xml.value().contains(QStringLiteral("Seeked")));
    QVERIFY(xml.value().contains(QStringLiteral("Metadata")));
}

void MprisTest::identity()
{
    QCOMPARE(m_root->property("Identity").toString(), QStringLiteral("CASDAP"));
    QVERIFY(!m_root->property("DesktopEntry").isValid());
    QCOMPARE(m_root->property("CanQuit").toBool(), false);
    QCOMPARE(m_root->property("CanRaise").toBool(), false);
    QCOMPARE(m_root->property("HasTrackList").toBool(), false);
    QCOMPARE(m_root->property("SupportedUriSchemes").toStringList(),
             QStringList{QStringLiteral("file")});
    QVERIFY(m_root->property("SupportedMimeTypes").toStringList().contains(
        QStringLiteral("audio/flac")));

    QCOMPARE(m_remote->property("Volume").toDouble(), 1.0);
    QCOMPARE(m_remote->property("Rate").toDouble(), 1.0);
    QCOMPARE(m_remote->property("MinimumRate").toDouble(), 1.0);
    QCOMPARE(m_remote->property("MaximumRate").toDouble(), 1.0);
    QCOMPARE(m_remote->property("CanControl").toBool(), true);
    QCOMPARE(m_remote->property("CanSeek").toBool(), true);
    m_remote->setProperty("Volume", 0.2);
    QCOMPARE(m_remote->property("Volume").toDouble(), 1.0);
}

void MprisTest::metadata()
{
    const QueueEntry &first = m_tracks.at(0);
    QVariantMap map = metadataMap();
    QCOMPARE(map.value(QStringLiteral("xesam:title")).toString(), first.title);
    QCOMPARE(map.value(QStringLiteral("xesam:artist")).toStringList(),
             QStringList{first.artist});
    QCOMPARE(map.value(QStringLiteral("xesam:album")).toString(), first.album);
    QCOMPARE(qvariant_cast<QDBusObjectPath>(map.value(QStringLiteral("mpris:trackid"))).path(),
             Mpris::trackPath(first.id).path());
    QCOMPARE(map.value(QStringLiteral("xesam:url")).toString(),
             QUrl::fromLocalFile(first.path).toString());
    QVERIFY(map.value(QStringLiteral("mpris:artUrl")).toString().startsWith(
        QStringLiteral("file://")));

    m_player->playPause();
    QTRY_VERIFY_WITH_TIMEOUT(
        metadataMap().value(QStringLiteral("mpris:length")).toLongLong() > 1000000,
        busTimeoutMs);
    m_player->pause();
    QVERIFY(metadataMap().value(QStringLiteral("mpris:length")).toLongLong() > 1000000);

    m_player->stop();
    m_player->queue()->clear();
    QCOMPARE(qvariant_cast<QDBusObjectPath>(
                 metadataMap().value(QStringLiteral("mpris:trackid"))).path(),
             Mpris::noTrack().path());
}

void MprisTest::playPause()
{
    QCOMPARE(playbackStatus(), QStringLiteral("Stopped"));

    QDBusReply<void> reply = m_remote->call(QStringLiteral("PlayPause"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Playing"), busTimeoutMs);
    QCOMPARE(m_player->status(), Player::Playing);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("PlaybackStatus"),
                                       QStringLiteral("Playing")),
                             busTimeoutMs);

    reply = m_remote->call(QStringLiteral("PlayPause"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Paused"), busTimeoutMs);
    QCOMPARE(m_player->status(), Player::Paused);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("PlaybackStatus"),
                                       QStringLiteral("Paused")),
                             busTimeoutMs);

    reply = m_remote->call(QStringLiteral("Play"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Playing"), busTimeoutMs);

    reply = m_remote->call(QStringLiteral("Pause"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Paused"), busTimeoutMs);

    reply = m_remote->call(QStringLiteral("Stop"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Stopped"), busTimeoutMs);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("PlaybackStatus"),
                                       QStringLiteral("Stopped")),
                             busTimeoutMs);
}

void MprisTest::loopStatus()
{
    QCOMPARE(m_remote->property("LoopStatus").toString(), QStringLiteral("None"));
    m_remote->setProperty("LoopStatus", QStringLiteral("Track"));
    QCOMPARE(m_player->queue()->repeat(), Queue::One);
    QCOMPARE(m_remote->property("LoopStatus").toString(), QStringLiteral("Track"));
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("LoopStatus"), QStringLiteral("Track")),
                             busTimeoutMs);

    m_changes.clear();
    m_remote->setProperty("LoopStatus", QStringLiteral("Bogus"));
    QCOMPARE(m_player->queue()->repeat(), Queue::One);
    QCOMPARE(m_remote->property("LoopStatus").toString(), QStringLiteral("Track"));
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("LoopStatus"), QStringLiteral("Track")),
                             busTimeoutMs);

    m_remote->setProperty("LoopStatus", QStringLiteral("Playlist"));
    QCOMPARE(m_player->queue()->repeat(), Queue::All);
    m_remote->setProperty("LoopStatus", QStringLiteral("None"));
    QCOMPARE(m_player->queue()->repeat(), Queue::Off);

    QCOMPARE(m_remote->property("Shuffle").toBool(), false);
    m_remote->setProperty("Shuffle", true);
    QVERIFY(m_player->queue()->shuffle());
    QCOMPARE(m_remote->property("Shuffle").toBool(), true);
    QTRY_VERIFY_WITH_TIMEOUT(sawChange(QStringLiteral("Shuffle"), true), busTimeoutMs);
    m_remote->setProperty("Shuffle", false);
    QVERIFY(!m_player->queue()->shuffle());
}

void MprisTest::seek()
{
    QDBusReply<void> reply = m_remote->call(QStringLiteral("Seek"),
                                            QVariant::fromValue<qlonglong>(500000));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCoreApplication::processEvents();
    QVERIFY(m_seeked.isEmpty());

    m_player->playPause();
    QCOMPARE(m_player->status(), Player::Playing);
    reply = m_remote->call(QStringLiteral("Seek"), QVariant::fromValue<qlonglong>(500000));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(!m_seeked.isEmpty(), busTimeoutMs);
    QVERIFY(m_seeked.first() >= 400000);
    QTRY_VERIFY_WITH_TIMEOUT(m_remote->property("Position").toLongLong() >= 400000,
                             busTimeoutMs);

    m_seeked.clear();
    const QDBusObjectPath other = Mpris::trackPath(m_tracks.at(1).id);
    reply = m_remote->call(QStringLiteral("SetPosition"), QVariant::fromValue(other),
                           QVariant::fromValue<qlonglong>(100000));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCoreApplication::processEvents();
    QVERIFY(m_seeked.isEmpty());

    const QDBusObjectPath current = Mpris::trackPath(m_tracks.at(0).id);
    reply = m_remote->call(QStringLiteral("SetPosition"), QVariant::fromValue(current),
                           QVariant::fromValue<qlonglong>(1000000));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_VERIFY_WITH_TIMEOUT(!m_seeked.isEmpty(), busTimeoutMs);
    QVERIFY(m_seeked.first() >= 900000);
}

void MprisTest::openUri()
{
    const QueueEntry target = m_tracks.at(2);
    QDBusReply<void> reply = m_remote->call(QStringLiteral("OpenUri"),
                                            QUrl::fromLocalFile(target.path).toString());
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QTRY_COMPARE_WITH_TIMEOUT(playbackStatus(), QStringLiteral("Playing"), busTimeoutMs);
    QCOMPARE(m_player->queue()->count(), 1);
    QCOMPARE(m_player->queue()->entry(0).path, target.path);
    QCOMPARE(m_player->queue()->entry(0).id, target.id);
    QCOMPARE(metadataMap().value(QStringLiteral("xesam:title")).toString(), target.title);

    reply = m_remote->call(QStringLiteral("OpenUri"), QStringLiteral("http://example.invalid/x"));
    QVERIFY2(reply.isValid(), qPrintable(reply.error().message()));
    QCOMPARE(m_player->queue()->count(), 1);
    QCOMPARE(m_player->queue()->entry(0).path, target.path);
}

QTEST_GUILESS_MAIN(MprisTest)
#include "tst_mpris.moc"
