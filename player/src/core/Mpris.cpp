#include "Mpris.h"

#include "Library.h"
#include "Player.h"

#include <QDBusMessage>
#include <QDebug>
#include <QFileInfo>
#include <QSqlQuery>
#include <QUrl>

namespace casdap {

namespace {

constexpr qint64 usPerMs = 1000;

QString propertiesInterface()
{
    return QStringLiteral("org.freedesktop.DBus.Properties");
}

}

MprisRootAdaptor::MprisRootAdaptor(Mpris *mpris)
    : QDBusAbstractAdaptor(mpris)
{
}

bool MprisRootAdaptor::canQuit() const
{
    return false;
}

bool MprisRootAdaptor::canRaise() const
{
    return false;
}

bool MprisRootAdaptor::hasTrackList() const
{
    return false;
}

QString MprisRootAdaptor::identity() const
{
    return QStringLiteral("CASDAP");
}

QStringList MprisRootAdaptor::supportedUriSchemes() const
{
    return {QStringLiteral("file")};
}

QStringList MprisRootAdaptor::supportedMimeTypes() const
{
    return {
        QStringLiteral("audio/flac"),
        QStringLiteral("audio/mpeg"),
        QStringLiteral("audio/mp4"),
        QStringLiteral("audio/ogg"),
        QStringLiteral("audio/x-wav"),
    };
}

void MprisRootAdaptor::Raise()
{
}

void MprisRootAdaptor::Quit()
{
}

MprisPlayerAdaptor::MprisPlayerAdaptor(Mpris *mpris)
    : QDBusAbstractAdaptor(mpris)
    , m_mpris(mpris)
{
}

QString MprisPlayerAdaptor::playbackStatus() const
{
    return m_mpris->playbackStatus();
}

QString MprisPlayerAdaptor::loopStatus() const
{
    return m_mpris->loopStatus();
}

void MprisPlayerAdaptor::setLoopStatus(const QString &status)
{
    m_mpris->setLoopStatus(status);
}

double MprisPlayerAdaptor::rate() const
{
    return 1.0;
}

void MprisPlayerAdaptor::setRate(double)
{
}

bool MprisPlayerAdaptor::shuffle() const
{
    return m_mpris->shuffle();
}

void MprisPlayerAdaptor::setShuffle(bool shuffle)
{
    m_mpris->setShuffle(shuffle);
}

QVariantMap MprisPlayerAdaptor::metadata() const
{
    return m_mpris->metadata();
}

double MprisPlayerAdaptor::volume() const
{
    return 1.0;
}

void MprisPlayerAdaptor::setVolume(double)
{
}

qlonglong MprisPlayerAdaptor::position() const
{
    return m_mpris->position();
}

double MprisPlayerAdaptor::minimumRate() const
{
    return 1.0;
}

double MprisPlayerAdaptor::maximumRate() const
{
    return 1.0;
}

bool MprisPlayerAdaptor::canControl() const
{
    return true;
}

void MprisPlayerAdaptor::Next()
{
    m_mpris->player()->next();
}

void MprisPlayerAdaptor::Previous()
{
    m_mpris->player()->previous();
}

void MprisPlayerAdaptor::Pause()
{
    m_mpris->player()->pause();
}

void MprisPlayerAdaptor::PlayPause()
{
    m_mpris->player()->playPause();
}

void MprisPlayerAdaptor::Stop()
{
    m_mpris->player()->stop();
}

void MprisPlayerAdaptor::Play()
{
    m_mpris->play();
}

void MprisPlayerAdaptor::Seek(qlonglong offset)
{
    m_mpris->seek(offset);
}

void MprisPlayerAdaptor::SetPosition(const QDBusObjectPath &trackId, qlonglong position)
{
    m_mpris->setPosition(trackId, position);
}

void MprisPlayerAdaptor::OpenUri(const QString &uri)
{
    m_mpris->openUri(uri);
}

Mpris::Mpris(Player *player, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_bus(bus)
    , m_root(new MprisRootAdaptor(this))
    , m_playerAdaptor(new MprisPlayerAdaptor(this))
{
    m_metadataTimer.setSingleShot(true);
    m_metadataTimer.setInterval(0);
    connect(&m_metadataTimer, &QTimer::timeout, this, &Mpris::notifyMetadata);
    connect(m_player, &Player::statusChanged, this, &Mpris::notifyStatus);
    connect(m_player, &Player::currentChanged, this, &Mpris::scheduleMetadata);
    connect(m_player, &Player::durationChanged, this, &Mpris::scheduleMetadata);
    connect(m_player->queue(), &Queue::shuffleChanged, this, &Mpris::notifyShuffle);
    connect(m_player->queue(), &Queue::repeatChanged, this, &Mpris::notifyLoopStatus);

    if (!m_bus.isConnected()) {
        qWarning() << "casdap mpris: session bus not connected";
        return;
    }
    if (!m_bus.registerObject(objectPath(), this, QDBusConnection::ExportAdaptors)) {
        qWarning() << "casdap mpris: cannot register" << objectPath()
                   << m_bus.lastError().message();
        return;
    }
    if (!m_bus.registerService(serviceName())) {
        qWarning() << "casdap mpris: cannot register" << serviceName()
                   << m_bus.lastError().message();
        m_bus.unregisterObject(objectPath());
        return;
    }
    m_registered = true;
}

Mpris::~Mpris()
{
    if (!m_registered)
        return;
    m_bus.unregisterService(serviceName());
    m_bus.unregisterObject(objectPath());
}

QString Mpris::serviceName()
{
    return QStringLiteral("org.mpris.MediaPlayer2.casdap");
}

QString Mpris::objectPath()
{
    return QStringLiteral("/org/mpris/MediaPlayer2");
}

QString Mpris::rootInterface()
{
    return QStringLiteral("org.mpris.MediaPlayer2");
}

QString Mpris::playerInterface()
{
    return QStringLiteral("org.mpris.MediaPlayer2.Player");
}

QDBusObjectPath Mpris::noTrack()
{
    return QDBusObjectPath(QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack"));
}

QDBusObjectPath Mpris::trackPath(qint64 id)
{
    return QDBusObjectPath(QStringLiteral("/org/casdap/track/%1").arg(qMax<qint64>(0, id)));
}

bool Mpris::isRegistered() const
{
    return m_registered;
}

Player *Mpris::player() const
{
    return m_player;
}

QDBusConnection Mpris::bus() const
{
    return m_bus;
}

QString Mpris::playbackStatus() const
{
    switch (m_player->status()) {
    case Player::Playing:
        return QStringLiteral("Playing");
    case Player::Paused:
        return QStringLiteral("Paused");
    case Player::Stopped:
        break;
    }
    return QStringLiteral("Stopped");
}

QString Mpris::loopStatus() const
{
    switch (m_player->queue()->repeat()) {
    case Queue::One:
        return QStringLiteral("Track");
    case Queue::All:
        return QStringLiteral("Playlist");
    case Queue::Off:
        break;
    }
    return QStringLiteral("None");
}

void Mpris::setLoopStatus(const QString &status)
{
    if (status == QLatin1String("None"))
        m_player->queue()->setRepeat(Queue::Off);
    else if (status == QLatin1String("Track"))
        m_player->queue()->setRepeat(Queue::One);
    else if (status == QLatin1String("Playlist"))
        m_player->queue()->setRepeat(Queue::All);
    else {
        qWarning() << "casdap mpris: unknown LoopStatus" << status;
        notifyLoopStatus();
    }
}

bool Mpris::shuffle() const
{
    return m_player->queue()->shuffle();
}

void Mpris::setShuffle(bool shuffle)
{
    m_player->queue()->setShuffle(shuffle);
}

QVariantMap Mpris::metadata() const
{
    const QVariantMap current = m_player->current();
    QVariantMap map;
    if (current.isEmpty()) {
        map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(noTrack()));
        return map;
    }

    const qint64 id = current.value(QStringLiteral("id")).toLongLong();
    map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(trackPath(id)));

    qint64 lengthMs = m_player->status() == Player::Stopped ? 0 : m_player->duration();
    if (lengthMs <= 0)
        lengthMs = current.value(QStringLiteral("durationMs")).toLongLong();
    map.insert(QStringLiteral("mpris:length"), QVariant::fromValue<qlonglong>(lengthMs * usPerMs));

    map.insert(QStringLiteral("xesam:title"), current.value(QStringLiteral("title")).toString());
    const QString artist = current.value(QStringLiteral("artist")).toString();
    if (!artist.isEmpty())
        map.insert(QStringLiteral("xesam:artist"), QStringList{artist});
    const QString album = current.value(QStringLiteral("album")).toString();
    if (!album.isEmpty())
        map.insert(QStringLiteral("xesam:album"), album);
    const QString path = current.value(QStringLiteral("path")).toString();
    if (!path.isEmpty())
        map.insert(QStringLiteral("xesam:url"), QUrl::fromLocalFile(path).toString());
    const QString art = current.value(QStringLiteral("artPath")).toString();
    if (!art.isEmpty())
        map.insert(QStringLiteral("mpris:artUrl"), QUrl::fromLocalFile(art).toString());
    return map;
}

qlonglong Mpris::position() const
{
    return m_player->position() * usPerMs;
}

QDBusObjectPath Mpris::currentTrackId() const
{
    const QVariantMap current = m_player->current();
    if (current.isEmpty())
        return noTrack();
    return trackPath(current.value(QStringLiteral("id")).toLongLong());
}

void Mpris::play()
{
    m_player->resume();
}

void Mpris::seek(qlonglong offsetUs)
{
    if (m_player->status() == Player::Stopped)
        return;
    const qint64 target = m_player->position() + offsetUs / usPerMs;
    const qint64 duration = m_player->duration();
    if (duration > 0 && target >= duration) {
        m_player->next();
        return;
    }
    m_player->seek(qMax<qint64>(0, target));
    emitSeeked();
}

void Mpris::setPosition(const QDBusObjectPath &trackId, qlonglong positionUs)
{
    if (positionUs < 0 || trackId.path() != currentTrackId().path())
        return;
    if (m_player->status() == Player::Stopped)
        return;
    const qint64 ms = positionUs / usPerMs;
    const qint64 duration = m_player->duration();
    if (duration > 0 && ms > duration)
        return;
    m_player->seek(ms);
    emitSeeked();
}

void Mpris::openUri(const QString &uri)
{
    const QUrl url(uri);
    if (!url.isLocalFile()) {
        qWarning() << "casdap mpris: unsupported uri" << uri;
        return;
    }
    const QFileInfo info(url.toLocalFile());
    if (!info.isFile()) {
        qWarning() << "casdap mpris: no such file" << info.filePath();
        return;
    }
    m_player->play({entryForPath(info.absoluteFilePath())}, 0);
}

QueueEntry Mpris::entryForPath(const QString &path) const
{
    QueueEntry entry;
    entry.path = path;
    entry.title = QFileInfo(path).completeBaseName();

    Library *library = m_player->library();
    if (!library || !library->isOpen())
        return entry;
    QSqlQuery query(library->database());
    query.prepare(QStringLiteral(
        "SELECT t.id, t.title, t.artist, a.title, a.art_path, t.duration_ms "
        "FROM tracks t LEFT JOIN albums a ON a.id = t.album_id WHERE t.path = ?"));
    query.addBindValue(path);
    if (!query.exec() || !query.next())
        return entry;
    entry.id = query.value(0).toLongLong();
    const QString title = query.value(1).toString();
    if (!title.isEmpty())
        entry.title = title;
    entry.artist = query.value(2).toString();
    entry.album = query.value(3).toString();
    entry.artPath = query.value(4).toString();
    entry.durationMs = query.value(5).toLongLong();
    return entry;
}

void Mpris::notifyStatus()
{
    emitPropertiesChanged(playerInterface(),
                          {{QStringLiteral("PlaybackStatus"), playbackStatus()}});
}

void Mpris::notifyMetadata()
{
    emitPropertiesChanged(playerInterface(), {{QStringLiteral("Metadata"), metadata()}});
}

void Mpris::scheduleMetadata()
{
    m_metadataTimer.start();
}

void Mpris::notifyShuffle()
{
    emitPropertiesChanged(playerInterface(), {{QStringLiteral("Shuffle"), shuffle()}});
}

void Mpris::notifyLoopStatus()
{
    emitPropertiesChanged(playerInterface(), {{QStringLiteral("LoopStatus"), loopStatus()}});
}

void Mpris::emitPropertiesChanged(const QString &interface, const QVariantMap &changed)
{
    if (!m_registered)
        return;
    QDBusMessage message = QDBusMessage::createSignal(objectPath(), propertiesInterface(),
                                                      QStringLiteral("PropertiesChanged"));
    message << interface << QVariant(changed) << QVariant(QStringList());
    m_bus.send(message);
}

void Mpris::emitSeeked()
{
    if (!m_registered)
        return;
    QDBusMessage message = QDBusMessage::createSignal(objectPath(), playerInterface(),
                                                      QStringLiteral("Seeked"));
    message << QVariant::fromValue<qlonglong>(position());
    m_bus.send(message);
}

}
