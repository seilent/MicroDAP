#pragma once

#include "BluezModel.h"
#include "LinkLibrary.h"
#include "LinkProto.h"
#include "LinkSettings.h"

#include <QDeadlineTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <functional>
#include <memory>
#include <vector>

class QSocketNotifier;

namespace microdap {

class BluezModel;
class Favorites;
class Library;
class Player;
class Settings;
class SinkVolume;
class StatusModel;

struct LinkTime
{
    qint64 mono = 0;
    qint64 boot = 0;
};

struct LinkSig
{
    qint64 track = 0;
    QString title;
    int source = 0;
    int output = 0;
    int repeat = 0;
    int volume = -1;
    int cap = 0;
    int step = 0;
    int battery = -1;
    int favorite = -1;
    bool playing = false;
    bool held = false;
    bool shuffle = false;
    bool charging = false;
    quint32 queueRev = 0;
    quint32 slot = 0;
    quint32 slotCount = 0;
    qint64 position = 0;

    bool same(const LinkSig &o) const;
};

class LinkServer : public QObject, public fd::LinkHandler
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
    Q_PROPERTY(bool pairWindow READ pairWindow NOTIFY pairWindowChanged)

public:
    using Clock = std::function<LinkTime()>;

    static LinkTime now();
    static quint64 features();
    static quint32 randomRev();
    static QString address(const QString &device);

    LinkServer(Player *player, Settings *settings, QObject *parent = nullptr);
    ~LinkServer() override;

    void setBluez(BluezModel *bluez);
    void setStatus(StatusModel *status);
    void setVolume(SinkVolume *volume);
    void setFavorites(Favorites *favorites);
    void setClock(Clock clock);
    void setPairWindowMs(int ms);
    void setSources(const LinkSources &sources);

    fd::LinkHello hello() const;
    bool connected() const;
    bool pairWindow() const;
    int sessions() const;
    quint32 libRev() const;
    quint32 snapRev() const;

    void open(int fd, const QString &device);
    void end(const QString &device);
    void bye(uint8_t reason);
    void poll();
    void command(fd::LinkCommand &c, int64_t now) override;

    Q_INVOKABLE void pairPhoneApp();
    void bind(const QString &device);

signals:
    void connectedChanged();
    void pairWindowChanged();
    void phoneBound(const QString &name);
    void phoneHello();

private:
    struct Peer
    {
        std::unique_ptr<fd::LinkSession> s;
        QSocketNotifier *read = nullptr;
        QSocketNotifier *write = nullptr;
        QString device;
        quint32 gen = 0;
        bool refused = false;
        bool candidate = false;
        bool hello = false;
        bool schemaSent = false;
        bool settingsSent = false;
        quint32 schema = 0;
        quint32 settings = 0;
    };

    struct Pivot
    {
        QString name;
        std::vector<fd::LinkWriter> rows;
    };

    LinkSig sig(qint64 mono) const;
    void writeState(fd::LinkWriter &w, const LinkTime &at, const LinkSig &s) const;
    void state(Peer &p, qint64 now);
    void tick(Peer &p, qint64 now);
    void watch(Peer &p);
    void unwatch(int fd);
    void schedule(qint64 now);
    void companionChanged();
    void updateConnected();
    uint8_t transport(const fd::LinkCommand &c);
    uint8_t volume(const fd::LinkCommand &c);
    bool library(Peer &p, fd::LinkCommand &c, qint64 now);
    uint8_t revCode(const fd::LinkBody &b, bool list, bool need) const;
    uint8_t libGet(Peer &p, const fd::LinkCommand &c, qint64 now);
    uint8_t play(const fd::LinkCommand &c);
    uint8_t queuePage(Peer &p, const fd::LinkCommand &c, qint64 now);
    uint8_t queueOp(const fd::LinkCommand &c);
    uint8_t action(const fd::LinkCommand &c);
    bool pages(Peer &p, uint8_t type, const fd::LinkWriter &head, const uint8_t tags[4], quint32 &at, quint32 end,
               quint32 total, const std::function<void(quint32, fd::LinkWriter &)> &rec, qint64 now);
    bool knownTracks(const QVariantList &ids) const;
    void bumpLib(bool snap);
    void refresh();
    void pushSchema(Peer &p, qint64 now);
    void pushSettings(Peer &p, qint64 now);
    void settings(Peer &p, qint64 now);
    void settingsCommand(Peer &p, fd::LinkCommand &c, qint64 now);
    bool phone() const;
    int cap() const;
    void setPairWindow(bool on);
    void closeWindow(bool stop);
    bool companionWindow() const;

    Player *m_player;
    Settings *m_settings;
    QPointer<BluezModel> m_bluez;
    QPointer<StatusModel> m_status;
    QPointer<SinkVolume> m_volume;
    QPointer<Favorites> m_favorites;
    QString m_confirmName;
    LinkSources m_sources;
    Clock m_clock;
    std::vector<std::unique_ptr<Peer>> m_peers;
    Peer *m_ticking = nullptr;
    QTimer m_timer;
    QTimer m_soon;
    QTimer m_windowTimer;
    QDeadlineTimer m_windowEnd;
    bool m_windowSeen = false;
    int m_windowMs = BluezModel::defaultPairingTimeout * 1000;
    LinkSig m_sent;
    qint64 m_sentAt = -1;
    quint32 m_sentRev = 0;
    std::vector<Pivot> m_schema;
    QByteArray m_schemaSig;
    QString m_settingsText;
    quint32 m_schemaRev;
    quint32 m_settingsRev;
    bool m_settingsFresh = false;
    qint64 m_seen = 0;
    qint64 m_nextAt = -1;
    quint32 m_gen = 0;
    quint32 m_libRev;
    quint32 m_snapRev;
    int m_level = -1;
    bool m_pairWindow = false;
    bool m_connected = false;
    bool m_polling = false;
    bool m_companion = false;
};

}
