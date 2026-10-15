#pragma once

#include "Queue.h"

#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariant>
#include <QVariantMap>

namespace microdap {

class Player;
class Mpris;

class MprisRootAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit CONSTANT)
    Q_PROPERTY(bool CanRaise READ canRaise CONSTANT)
    Q_PROPERTY(bool HasTrackList READ hasTrackList CONSTANT)
    Q_PROPERTY(QString Identity READ identity CONSTANT)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes CONSTANT)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes CONSTANT)

public:
    explicit MprisRootAdaptor(Mpris *mpris);

    bool canQuit() const;
    bool canRaise() const;
    bool hasTrackList() const;
    QString identity() const;
    QStringList supportedUriSchemes() const;
    QStringList supportedMimeTypes() const;

public slots:
    void Raise();
    void Quit();
};

class MprisPlayerAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ minimumRate CONSTANT)
    Q_PROPERTY(double MaximumRate READ maximumRate CONSTANT)
    Q_PROPERTY(bool CanGoNext READ canControl CONSTANT)
    Q_PROPERTY(bool CanGoPrevious READ canControl CONSTANT)
    Q_PROPERTY(bool CanPlay READ canControl CONSTANT)
    Q_PROPERTY(bool CanPause READ canControl CONSTANT)
    Q_PROPERTY(bool CanSeek READ canControl CONSTANT)
    Q_PROPERTY(bool CanControl READ canControl CONSTANT)

public:
    explicit MprisPlayerAdaptor(Mpris *mpris);

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    double rate() const;
    void setRate(double rate);
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QVariantMap metadata() const;
    double volume() const;
    void setVolume(double volume);
    qlonglong position() const;
    double minimumRate() const;
    double maximumRate() const;
    bool canControl() const;

public slots:
    void Next();
    void Previous();
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &uri);

signals:
    void Seeked(qlonglong position);

private:
    Mpris *m_mpris;
};

class Mpris : public QObject
{
    Q_OBJECT

public:
    Mpris(Player *player, const QDBusConnection &bus, QObject *parent = nullptr);
    ~Mpris() override;

    static QString serviceName();
    static QString objectPath();
    static QString rootInterface();
    static QString playerInterface();
    static QDBusObjectPath noTrack();
    static QDBusObjectPath trackPath(qint64 id);

    bool isRegistered() const;
    Player *player() const;
    QDBusConnection bus() const;

    QString playbackStatus() const;
    QString loopStatus() const;
    void setLoopStatus(const QString &status);
    bool shuffle() const;
    void setShuffle(bool shuffle);
    QVariantMap metadata() const;
    qlonglong position() const;
    QDBusObjectPath currentTrackId() const;

    void play();
    void seek(qlonglong offsetUs);
    void setPosition(const QDBusObjectPath &trackId, qlonglong positionUs);
    void openUri(const QString &uri);

private:
    void notifyStatus();
    void notifyMetadata();
    void scheduleMetadata();
    void notifyShuffle();
    void notifyLoopStatus();
    void emitPropertiesChanged(const QString &interface, const QVariantMap &changed);
    void emitSeeked();
    QueueEntry entryForPath(const QString &path) const;

    Player *m_player;
    QDBusConnection m_bus;
    MprisRootAdaptor *m_root;
    MprisPlayerAdaptor *m_playerAdaptor;
    QTimer m_metadataTimer;
    bool m_registered = false;
};

}
