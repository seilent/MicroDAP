#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>

#include <pulse/context.h>
#include <pulse/introspect.h>
#include <pulse/subscribe.h>
#include <pulse/volume.h>

struct pa_glib_mainloop;

class QSocketNotifier;

namespace casdap {

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

signals:
    void levelChanged(int percent);
};

class PulseSinkVolume : public SinkVolume
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
    static int level(const pa_cvolume &volume);

    void step(int percent) override;

private:
    static void onState(pa_context *context, void *userdata);
    static void onEvent(pa_context *context, pa_subscription_event_type_t type, quint32 index, void *userdata);
    static void onServerInfo(pa_context *context, const pa_server_info *info, void *userdata);
    static void onValidSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void onOutsideSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata);
    static void onSet(pa_context *context, int success, void *userdata);

    void connectContext();
    void teardown(bool reconnect);
    void validate();
    void apply(int percent);
    void send();
    bool quiet() const;

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
    bool m_valid = false;
    bool m_validating = false;
    bool m_revalidate = false;
    bool m_failLogged = false;
    bool m_unmute = false;
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
