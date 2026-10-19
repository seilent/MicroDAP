#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QHash>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include "OutputPorts.h"

#include <functional>
#include <memory>
#include <vector>

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>

struct pa_glib_mainloop;

class QSocketNotifier;

namespace microdap {

class AlsaVolume;
class Settings;

class KeySource : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

signals:
    void step(int direction);
};

class EvdevKeys : public KeySource
{
    Q_OBJECT

public:
    static constexpr int retryMs = 2000;
    static constexpr int holdMs = 400;
    static constexpr int repeatMs = 100;

    explicit EvdevKeys(QObject *parent = nullptr);
    explicit EvdevKeys(int retry, QObject *parent = nullptr);
    ~EvdevKeys() override;

    static QStringList defaultNames();
    int openCount() const;

private:
    struct Node
    {
        QString name;
        int fd = -1;
        QSocketNotifier *notifier = nullptr;
        bool logged = false;
    };

    QString find(const QString &name) const;
    void openMissing();
    void closeNode(int index);
    void drain(int index);
    void repeat();

    QString m_devRoot;
    QString m_sysRoot;
    QList<Node> m_nodes;
    QTimer m_retry;
    QTimer m_repeat;
    int m_repeatCode = 0;
    int m_repeatNode = -1;
    int m_opens = 0;
};

class SinkVolume : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    virtual void step(int percent) = 0;
    virtual void set(int) {}
    virtual void setCap(int) {}
    virtual void dropPending() {}

signals:
    void levelChanged(int percent);
};

class PulseSinkVolume : public SinkVolume, public DacSinkPort
{
    Q_OBJECT

public:
    static constexpr int reconnectMs = 1000;
    static constexpr int quietMs = 300;
    static constexpr int sendGapMs = 250;
    static constexpr int maxPendingSteps = 20;

    explicit PulseSinkVolume(QObject *parent = nullptr);
    ~PulseSinkVolume() override;

    static pa_cvolume stepped(const pa_cvolume &volume, int percent);
    static pa_cvolume scaledTo(const pa_cvolume &volume, pa_volume_t top);
    static pa_cvolume capped(const pa_cvolume &volume, int capPercent);
    static pa_cvolume leveled(const pa_cvolume &volume, int percent, int capPercent);
    static int level(const pa_cvolume &volume);
    static int alsaCardOf(const pa_proplist *props);
    static DacCard dacCardOf(const pa_proplist *props);
    static bool isBluezInput(const pa_proplist *props);

    void step(int percent) override;
    void set(int percent) override;
    void setCap(int percent) override;
    void dropPending() override;

    void cardInfo(quint32 sinkIndex, std::function<void(const DacCard &)> done) override;
    void sinkCard(const QString &name, std::function<void(const DacCard &)> done) override;
    void sinkVolumeDb(const QString &name, std::function<void(bool ok, double db)> done) override;
    void sinkMuted(const QString &name, std::function<void(bool ok, bool muted)> done) override;
    void setSinkVolumeDb(const QString &name, double db, std::function<void(bool ok)> done = nullptr) override;
    void unmuteSink(const QString &name) override;
    void bluezInputs(std::function<void(const QList<quint32> &)> done) override;
    void moveSinkInput(quint32 index, const QString &sink) override;

signals:
    void sinkAppeared(quint32 index, const QString &name, int card);
    void sinkChanged(quint32 index);
    void cardRemoved(quint32 cardIndex);

private:
    struct Request
    {
        std::function<void(const pa_sink_info *)> sink;
        std::function<void(const pa_card_info *)> card;
        std::function<void(const QList<quint32> *)> inputs;
        std::function<void(bool)> set;
        PulseSinkVolume *self = nullptr;
        QList<quint32> found;
        bool delivered = false;
    };

    static void onState(pa_context *context, void *userdata);
    static void onEvent(pa_context *context, pa_subscription_event_type_t type, quint32 index, void *userdata);
    static void onServerInfo(pa_context *context, const pa_server_info *info, void *userdata);
    static void onValidSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void onOutsideSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void onSet(pa_context *context, int success, void *userdata);
    static void onRequestSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void onRequestCard(pa_context *context, const pa_card_info *info, int eol, void *userdata);
    static void onRequestInput(pa_context *context, const pa_sink_input_info *info, int eol, void *userdata);
    static void onDacSet(pa_context *context, int success, void *userdata);
    static void onRequestSet(pa_context *context, int success, void *userdata);

    void connectContext();
    void teardown(bool reconnect);
    void validate();
    void apply(int percent);
    void applyLevel(int percent);
    void push(bool report);
    void send();
    bool quiet() const;
    void querySink(quint32 index, const QByteArray &name, std::function<void(const pa_sink_info *)> done);
    void queryCard(quint32 index, std::function<void(const pa_card_info *)> done);
    void lookupCard(quint32 sinkIndex, const QByteArray &name, std::function<void(const DacCard &)> done);
    void queryInputs(std::function<void(const QList<quint32> *)> done);
    void issue(std::unique_ptr<Request> request, const std::function<pa_operation *(Request *)> &start);
    std::unique_ptr<Request> take(Request *request);
    void fail(std::unique_ptr<Request> request);
    void failPending();
    void watchSink(quint32 index, bool appeared);
    void dacCall(const char *what, const std::function<pa_operation *()> &start);

    std::vector<std::unique_ptr<Request>> m_requests;
    pa_glib_mainloop *m_mainloop = nullptr;
    pa_context *m_context = nullptr;
    QTimer m_teardown;
    QTimer m_reconnect;
    QTimer m_send;
    QElapsedTimer m_clock;
    QByteArray m_sink;
    quint32 m_sinkIndex = 0;
    pa_cvolume m_volume{};
    qint64 m_lastSetMs = -quietMs;
    qint64 m_sentMs = -sendGapMs;
    int m_pendingSteps = 0;
    int m_pendingPercent = 0;
    int m_pendingSet = -1;
    int m_cap = 100;
    bool m_valid = false;
    bool m_validating = false;
    bool m_revalidate = false;
    bool m_failLogged = false;
    bool m_unmute = false;
};

class RoutedSinkVolume : public SinkVolume, public DacVolumePort
{
    Q_OBJECT

public:
    static constexpr int defaultStoreDelayMs = 2000;
    static constexpr int storeWatchMs = 500;

    RoutedSinkVolume(SinkVolume *pulse, DacSinkPort *dac, AlsaVolume *alsa, int storeDelayMs = defaultStoreDelayMs,
                     QObject *parent = nullptr);

    void step(int percent) override;
    void set(int percent) override;
    void setCap(int percent) override;
    void setPowerOn(int percent);

    static QString capFile();
    static int lastCap(int fallback);
    static QString powerOnMarker();
    static QString systemPowerOnMarker();
    static QString bootId();

    void setRoute(Route route, const QString &cardId, const QString &sink) override;
    void setStoreAllowed(bool allowed) override;
    void storeSync(const QString &cardId) override;
    int ownedStep(const QString &cardId) const override;
    void setOwnedStep(const QString &cardId, int step) override;

    Route route() const;

private:
    void keepOwned();
    void markDirty();
    void applyPowerOn();
    bool mixerOn(bool requireOpen) const;

    SinkVolume *m_pulse;
    DacSinkPort *m_dac;
    AlsaVolume *m_alsa;
    QTimer m_store;
    QHash<QString, int> m_owned;
    QString m_card;
    QString m_sink;
    Route m_route = Route::Pulse;
    int m_powerOn = 0;
    bool m_powerOnDone = false;
    bool m_storeAllowed = false;
    bool m_dirty = false;
};

class VolumeKeys : public QObject
{
    Q_OBJECT

public:
    VolumeKeys(KeySource *keys, SinkVolume *sink, Settings *settings, QObject *parent = nullptr);

signals:
    void levelChanged(int percent);

private:
    KeySource *m_keys;
    SinkVolume *m_sink;
    Settings *m_settings;
};

}
