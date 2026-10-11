#include "VolumeKeys.h"

#include "AlsaVolume.h"
#include "Settings.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QMetaMethod>
#include <QPointer>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <linux/input.h>
#include <pulse/error.h>
#include <pulse/glib-mainloop.h>
#include <pulse/operation.h>
#include <unistd.h>

namespace casdap {

namespace {

constexpr int readBatch = 64;

QString envPath(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

void unref(pa_operation *operation)
{
    if (operation)
        pa_operation_unref(operation);
}

}

EvdevKeys::EvdevKeys(QObject *parent)
    : EvdevKeys(retryMs, parent)
{
}

EvdevKeys::EvdevKeys(int retry, QObject *parent)
    : KeySource(parent)
    , m_devRoot(envPath("CASDAP_DEV_INPUT", QStringLiteral("/dev/input")))
    , m_sysRoot(envPath("CASDAP_SYS_INPUT", QStringLiteral("/sys/class/input")))
{
    for (const QString &name : defaultNames())
        m_nodes.append(Node{name});
    m_retry.setSingleShot(true);
    m_retry.setInterval(retry);
    connect(&m_retry, &QTimer::timeout, this, &EvdevKeys::openMissing);
    m_repeat.setSingleShot(true);
    connect(&m_repeat, &QTimer::timeout, this, &EvdevKeys::repeat);
    openMissing();
}

EvdevKeys::~EvdevKeys()
{
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).fd >= 0)
            ::close(m_nodes.at(i).fd);
    }
}

QStringList EvdevKeys::defaultNames()
{
    if (!qEnvironmentVariableIsSet("CASDAP_VOLUME_NAMES"))
        return {QStringLiteral("gpio-keys"), QStringLiteral("pm8941_resin")};
    return qEnvironmentVariable("CASDAP_VOLUME_NAMES").split(QLatin1Char(':'), Qt::SkipEmptyParts);
}

int EvdevKeys::openCount() const
{
    return m_opens;
}

QString EvdevKeys::find(const QString &name) const
{
    const QStringList events = QDir(m_devRoot).entryList({QStringLiteral("event*")},
                                                         QDir::System | QDir::Files, QDir::Name);
    for (const QString &event : events) {
        QFile file(m_sysRoot + QLatin1Char('/') + event + QStringLiteral("/device/name"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (QString::fromUtf8(file.readAll()).trimmed() == name)
            return m_devRoot + QLatin1Char('/') + event;
    }
    return QString();
}

void EvdevKeys::openMissing()
{
    bool missing = false;
    for (int i = 0; i < m_nodes.size(); ++i) {
        Node &node = m_nodes[i];
        if (node.fd >= 0)
            continue;
        const QString path = find(node.name);
        const int fd = path.isEmpty() ? -1 : ::open(QFile::encodeName(path).constData(),
                                                    O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            if (!node.logged) {
                if (path.isEmpty())
                    qInfo().noquote() << "casdap volume:" << node.name << "not found, retrying";
                else
                    qWarning().noquote() << "casdap volume: cannot open" << path << std::strerror(errno);
                node.logged = true;
            }
            missing = true;
            continue;
        }
        node.fd = fd;
        node.logged = false;
        node.notifier = new QSocketNotifier(fd, QSocketNotifier::Read, this);
        connect(node.notifier, &QSocketNotifier::activated, this, [this, i] { drain(i); });
        ++m_opens;
        qInfo().noquote() << "casdap volume:" << node.name << "on" << path;
    }
    if (missing)
        m_retry.start();
}

void EvdevKeys::closeNode(int index)
{
    Node &node = m_nodes[index];
    if (node.notifier) {
        node.notifier->setEnabled(false);
        node.notifier->deleteLater();
        node.notifier = nullptr;
    }
    if (node.fd >= 0) {
        ::close(node.fd);
        node.fd = -1;
    }
    if (m_repeatNode == index) {
        m_repeat.stop();
        m_repeatCode = 0;
        m_repeatNode = -1;
    }
    qInfo().noquote() << "casdap volume:" << node.name << "closed";
    if (!m_retry.isActive())
        m_retry.start();
}

void EvdevKeys::drain(int index)
{
    input_event events[readBatch];
    while (m_nodes.at(index).fd >= 0) {
        const ssize_t got = ::read(m_nodes.at(index).fd, events, sizeof(events));
        if (got < 0) {
            if (errno == EINTR)
                continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                return;
            qWarning().noquote() << "casdap volume:" << m_nodes.at(index).name << "read failed"
                                 << std::strerror(errno);
            closeNode(index);
            return;
        }
        if (got == 0) {
            closeNode(index);
            return;
        }
        const qsizetype count = qsizetype(got) / qsizetype(sizeof(input_event));
        for (qsizetype i = 0; i < count; ++i) {
            const input_event &event = events[i];
            if (event.type != EV_KEY || (event.code != KEY_VOLUMEUP && event.code != KEY_VOLUMEDOWN))
                continue;
            if (event.value == 1) {
                m_repeatCode = event.code;
                m_repeatNode = index;
                m_repeat.start(holdMs);
                emit step(event.code == KEY_VOLUMEUP ? 1 : -1);
            } else if (event.value == 0 && event.code == m_repeatCode) {
                m_repeat.stop();
                m_repeatCode = 0;
                m_repeatNode = -1;
            }
        }
    }
}

void EvdevKeys::repeat()
{
    for (int i = 0; i < m_nodes.size(); ++i) {
        if (m_nodes.at(i).fd >= 0)
            drain(i);
    }
    if (m_repeat.isActive() || !m_repeatCode)
        return;
    m_repeat.start(repeatMs);
    emit step(m_repeatCode == KEY_VOLUMEUP ? 1 : -1);
}

PulseSinkVolume::PulseSinkVolume(QObject *parent)
    : SinkVolume(parent)
{
    pa_cvolume_init(&m_volume);
    m_clock.start();
    m_teardown.setSingleShot(true);
    m_teardown.setInterval(0);
    connect(&m_teardown, &QTimer::timeout, this, [this] { teardown(true); });
    m_reconnect.setSingleShot(true);
    m_reconnect.setInterval(reconnectMs);
    connect(&m_reconnect, &QTimer::timeout, this, &PulseSinkVolume::connectContext);
    m_send.setSingleShot(true);
    connect(&m_send, &QTimer::timeout, this, &PulseSinkVolume::send);

    QAbstractEventDispatcher *dispatcher = QAbstractEventDispatcher::instance();
    if (!dispatcher || !dispatcher->inherits("QEventDispatcherGlib")) {
        qWarning() << "casdap volume: no glib event loop";
        return;
    }
    m_mainloop = pa_glib_mainloop_new(nullptr);
    connectContext();
}

PulseSinkVolume::~PulseSinkVolume()
{
    m_teardown.stop();
    m_reconnect.stop();
    m_send.stop();
    teardown(false);
    if (m_mainloop)
        pa_glib_mainloop_free(m_mainloop);
}

pa_cvolume PulseSinkVolume::stepped(const pa_cvolume &volume, int percent)
{
    pa_cvolume out = volume;
    const qint64 delta = qRound64(double(PA_VOLUME_NORM) * percent / 100.0);
    for (quint8 i = 0; i < out.channels && i < PA_CHANNELS_MAX; ++i)
        out.values[i] = pa_volume_t(qBound<qint64>(PA_VOLUME_MUTED, qint64(out.values[i]) + delta, PA_VOLUME_NORM));
    return out;
}

int PulseSinkVolume::level(const pa_cvolume &volume)
{
    pa_volume_t max = PA_VOLUME_MUTED;
    for (quint8 i = 0; i < volume.channels && i < PA_CHANNELS_MAX; ++i)
        max = qMax(max, volume.values[i]);
    return qRound(100.0 * max / PA_VOLUME_NORM);
}

void PulseSinkVolume::step(int percent)
{
    if (percent == 0 || !m_mainloop)
        return;
    if (m_valid) {
        apply(percent);
        return;
    }
    const int direction = percent > 0 ? 1 : -1;
    if (qAbs(m_pendingSteps + direction) > maxPendingSteps)
        return;
    m_pendingSteps += direction;
    m_pendingPercent += percent;
}

void PulseSinkVolume::dropPending()
{
    const bool sending = m_send.isActive();
    m_send.stop();
    m_unmute = false;
    m_pendingSteps = 0;
    m_pendingPercent = 0;
    if (sending)
        validate();
}

void PulseSinkVolume::onState(pa_context *context, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context)
        return;
    switch (pa_context_get_state(context)) {
    case PA_CONTEXT_READY:
        qInfo() << "casdap volume: connected";
        self->m_failLogged = false;
        pa_context_set_subscribe_callback(context, &PulseSinkVolume::onEvent, self);
        unref(pa_context_subscribe(context,
                                   pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER
                                                          | PA_SUBSCRIPTION_MASK_CARD),
                                   nullptr, nullptr));
        self->validate();
        break;
    case PA_CONTEXT_FAILED:
    case PA_CONTEXT_TERMINATED:
        self->m_teardown.start();
        break;
    default:
        break;
    }
}

void PulseSinkVolume::onEvent(pa_context *context, pa_subscription_event_type_t type, quint32 index, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context)
        return;
    const int facility = type & PA_SUBSCRIPTION_EVENT_FACILITY_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_SERVER) {
        self->validate();
        return;
    }
    const int kind = type & PA_SUBSCRIPTION_EVENT_TYPE_MASK;
    if (facility == PA_SUBSCRIPTION_EVENT_CARD) {
        if (kind == PA_SUBSCRIPTION_EVENT_REMOVE)
            emit self->cardRemoved(index);
        return;
    }
    if (facility != PA_SUBSCRIPTION_EVENT_SINK)
        return;
    if (kind == PA_SUBSCRIPTION_EVENT_NEW || kind == PA_SUBSCRIPTION_EVENT_CHANGE)
        self->watchSink(index, kind == PA_SUBSCRIPTION_EVENT_NEW);
    if (kind != PA_SUBSCRIPTION_EVENT_CHANGE)
        return;
    if (!self->m_valid || self->m_validating || index != self->m_sinkIndex || !self->quiet())
        return;
    unref(pa_context_get_sink_info_by_index(context, index, &PulseSinkVolume::onOutsideSink, self));
}

void PulseSinkVolume::onServerInfo(pa_context *context, const pa_server_info *info, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context)
        return;
    if (!info || !info->default_sink_name) {
        self->m_validating = false;
        if (self->m_revalidate)
            self->validate();
        return;
    }
    self->m_sink = QByteArray(info->default_sink_name);
    pa_operation *operation = pa_context_get_sink_info_by_name(context, self->m_sink.constData(),
                                                               &PulseSinkVolume::onValidSink, self);
    if (!operation) {
        self->m_validating = false;
        self->m_teardown.start();
        return;
    }
    pa_operation_unref(operation);
}

void PulseSinkVolume::onValidSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context || eol > 0)
        return;
    self->m_validating = false;
    if (self->m_revalidate) {
        self->validate();
        return;
    }
    if (eol < 0 || !info)
        return;
    self->m_sinkIndex = info->index;
    self->m_volume = info->volume;
    self->m_valid = true;
    const int percent = self->m_pendingPercent;
    self->m_pendingSteps = 0;
    self->m_pendingPercent = 0;
    if (percent != 0)
        self->apply(percent);
}

void PulseSinkVolume::onOutsideSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context || eol != 0 || !info)
        return;
    if (!self->m_valid || self->m_validating || info->index != self->m_sinkIndex || !self->quiet())
        return;
    self->m_volume = info->volume;
}

void PulseSinkVolume::onSet(pa_context *context, int success, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context || success)
        return;
    qWarning() << "casdap volume: set failed" << pa_strerror(pa_context_errno(context));
    self->validate();
}

void PulseSinkVolume::connectContext()
{
    if (!m_mainloop || m_context)
        return;
    m_context = pa_context_new(pa_glib_mainloop_get_api(m_mainloop), "casdap-player");
    if (!m_context) {
        m_reconnect.start();
        return;
    }
    pa_context_set_state_callback(m_context, &PulseSinkVolume::onState, this);
    if (pa_context_connect(m_context, nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) < 0)
        m_teardown.start();
}

void PulseSinkVolume::teardown(bool reconnect)
{
    if (m_context) {
        if (reconnect && !m_failLogged) {
            qWarning() << "casdap volume: pulse unavailable, retrying" << pa_strerror(pa_context_errno(m_context));
            m_failLogged = true;
        }
        pa_context_set_state_callback(m_context, nullptr, nullptr);
        pa_context_set_subscribe_callback(m_context, nullptr, nullptr);
        pa_context_disconnect(m_context);
        pa_context_unref(m_context);
        m_context = nullptr;
    }
    failPending();
    m_send.stop();
    m_unmute = false;
    m_valid = false;
    m_validating = false;
    m_revalidate = false;
    m_pendingSteps = 0;
    m_pendingPercent = 0;
    if (reconnect)
        m_reconnect.start();
}

void PulseSinkVolume::validate()
{
    m_valid = false;
    if (!m_context)
        return;
    if (m_validating) {
        m_revalidate = true;
        return;
    }
    m_revalidate = false;
    pa_operation *operation = pa_context_get_server_info(m_context, &PulseSinkVolume::onServerInfo, this);
    if (!operation) {
        m_teardown.start();
        return;
    }
    m_validating = true;
    pa_operation_unref(operation);
}

void PulseSinkVolume::apply(int percent)
{
    m_volume = stepped(m_volume, percent);
    if (percent > 0)
        m_unmute = true;
    const qint64 now = m_clock.elapsed();
    m_lastSetMs = now;
    const int percentLevel = level(m_volume);
    qInfo() << "casdap volume:" << percentLevel;
    emit levelChanged(percentLevel);
    if (m_send.isActive())
        return;
    const qint64 wait = sendGapMs - (now - m_sentMs);
    if (wait > 0)
        m_send.start(int(wait));
    else
        send();
}

void PulseSinkVolume::send()
{
    if (!m_context || !m_valid)
        return;
    m_sentMs = m_clock.elapsed();
    m_lastSetMs = m_sentMs;
    pa_operation *operation = pa_context_set_sink_volume_by_name(m_context, m_sink.constData(), &m_volume,
                                                                 &PulseSinkVolume::onSet, this);
    if (!operation) {
        validate();
        return;
    }
    pa_operation_unref(operation);
    if (m_unmute)
        unref(pa_context_set_sink_mute_by_name(m_context, m_sink.constData(), 0, nullptr, nullptr));
    m_unmute = false;
}

bool PulseSinkVolume::quiet() const
{
    return m_clock.elapsed() - m_lastSetMs >= quietMs;
}

int PulseSinkVolume::alsaCardOf(const pa_proplist *props)
{
    const char *value = props ? pa_proplist_gets(props, "alsa.card") : nullptr;
    if (!value)
        return -1;
    bool ok = false;
    const int card = QByteArray(value).toInt(&ok);
    return ok && card >= 0 ? card : -1;
}

DacCard PulseSinkVolume::dacCardOf(const pa_proplist *props)
{
    const int card = alsaCardOf(props);
    const char *reserve = props ? pa_proplist_gets(props, "api.dbus.ReserveDevice1") : nullptr;
    if (card < 0 || !reserve || !*reserve)
        return DacCard{};
    DacCard out;
    out.alsaCard = card;
    out.reserveName = QString::fromUtf8(reserve);
    return out;
}

bool PulseSinkVolume::isBluezInput(const pa_proplist *props)
{
    const char *name = props ? pa_proplist_gets(props, "node.name") : nullptr;
    return name && QByteArray(name).startsWith("bluez_input.");
}

void PulseSinkVolume::cardInfo(quint32 sinkIndex, std::function<void(const DacCard &)> done)
{
    lookupCard(sinkIndex, QByteArray(), std::move(done));
}

void PulseSinkVolume::sinkCard(const QString &name, std::function<void(const DacCard &)> done)
{
    if (name.isEmpty()) {
        QTimer::singleShot(0, this, [done] { done(DacCard{}); });
        return;
    }
    lookupCard(PA_INVALID_INDEX, name.toUtf8(), std::move(done));
}

void PulseSinkVolume::lookupCard(quint32 sinkIndex, const QByteArray &name, std::function<void(const DacCard &)> done)
{
    querySink(sinkIndex, name, [this, done](const pa_sink_info *info) {
        if (!info || info->card == PA_INVALID_INDEX) {
            done(DacCard{});
            return;
        }
        queryCard(info->card, [done](const pa_card_info *card) {
            if (!card) {
                done(DacCard{});
                return;
            }
            DacCard out = dacCardOf(card->proplist);
            if (out.isValid())
                out.cardIndex = card->index;
            done(out);
        });
    });
}

void PulseSinkVolume::setDefaultSink(const QString &name)
{
    const QByteArray sink = name.toUtf8();
    dacCall("default sink", [this, sink] {
        return pa_context_set_default_sink(m_context, sink.constData(), &PulseSinkVolume::onDacSet, this);
    });
}

void PulseSinkVolume::sinkVolumeDb(const QString &name, std::function<void(bool ok, double db)> done)
{
    querySink(PA_INVALID_INDEX, name.toUtf8(), [done](const pa_sink_info *info) {
        if (!info) {
            done(false, 0.0);
            return;
        }
        done(true, pa_sw_volume_to_dB(pa_cvolume_max(&info->volume)));
    });
}

void PulseSinkVolume::sinkMuted(const QString &name, std::function<void(bool ok, bool muted)> done)
{
    querySink(PA_INVALID_INDEX, name.toUtf8(), [done](const pa_sink_info *info) {
        if (!info) {
            done(false, false);
            return;
        }
        done(true, info->mute != 0);
    });
}

void PulseSinkVolume::setSinkVolumeDb(const QString &name, double db, std::function<void(bool ok)> done)
{
    const QByteArray sink = name.toUtf8();
    querySink(PA_INVALID_INDEX, sink, [this, sink, db, done](const pa_sink_info *info) {
        if (!info || !pa_cvolume_valid(&info->volume)) {
            qWarning().noquote() << "casdap volume: no sink" << QString::fromUtf8(sink);
            if (done)
                done(false);
            return;
        }
        pa_cvolume volume;
        pa_cvolume_set(&volume, info->volume.channels, pa_sw_volume_from_dB(db));
        auto request = std::make_unique<Request>();
        request->set = done ? done : [](bool) {};
        issue(std::move(request), [this, sink, volume](Request *raw) {
            return pa_context_set_sink_volume_by_name(m_context, sink.constData(), &volume,
                                                      &PulseSinkVolume::onRequestSet, raw);
        });
    });
}

void PulseSinkVolume::unmuteSink(const QString &name)
{
    const QByteArray sink = name.toUtf8();
    dacCall("sink unmute", [this, sink] {
        return pa_context_set_sink_mute_by_name(m_context, sink.constData(), 0, &PulseSinkVolume::onDacSet, this);
    });
}

void PulseSinkVolume::bluezInputs(std::function<void(const QList<quint32> &)> done)
{
    queryInputs([done](const QList<quint32> *found) { done(found ? *found : QList<quint32>{}); });
}

void PulseSinkVolume::moveSinkInput(quint32 index, const QString &sink)
{
    const QByteArray name = sink.toUtf8();
    dacCall("sink input move", [this, index, name] {
        return pa_context_move_sink_input_by_name(m_context, index, name.constData(), &PulseSinkVolume::onDacSet,
                                                  this);
    });
}

void PulseSinkVolume::onRequestSink(pa_context *context, const pa_sink_info *info, int eol, void *userdata)
{
    auto *request = static_cast<Request *>(userdata);
    if (context != request->self->m_context)
        return;
    if (eol == 0) {
        if (info && !request->delivered) {
            request->delivered = true;
            request->sink(info);
        }
        return;
    }
    std::unique_ptr<Request> owned = request->self->take(request);
    if (owned && !owned->delivered)
        owned->sink(nullptr);
}

void PulseSinkVolume::onRequestCard(pa_context *context, const pa_card_info *info, int eol, void *userdata)
{
    auto *request = static_cast<Request *>(userdata);
    if (context != request->self->m_context)
        return;
    if (eol == 0) {
        if (info && !request->delivered) {
            request->delivered = true;
            request->card(info);
        }
        return;
    }
    std::unique_ptr<Request> owned = request->self->take(request);
    if (owned && !owned->delivered)
        owned->card(nullptr);
}

void PulseSinkVolume::onRequestInput(pa_context *context, const pa_sink_input_info *info, int eol, void *userdata)
{
    auto *request = static_cast<Request *>(userdata);
    if (context != request->self->m_context)
        return;
    if (eol == 0) {
        if (info && isBluezInput(info->proplist))
            request->found.append(info->index);
        return;
    }
    std::unique_ptr<Request> owned = request->self->take(request);
    if (owned)
        owned->inputs(eol > 0 ? &owned->found : nullptr);
}

void PulseSinkVolume::onDacSet(pa_context *context, int success, void *userdata)
{
    auto *self = static_cast<PulseSinkVolume *>(userdata);
    if (context != self->m_context || success)
        return;
    qWarning() << "casdap volume: dac sink call failed" << pa_strerror(pa_context_errno(context));
}

void PulseSinkVolume::onRequestSet(pa_context *context, int success, void *userdata)
{
    auto *request = static_cast<Request *>(userdata);
    if (context != request->self->m_context)
        return;
    if (!success)
        qWarning() << "casdap volume: dac sink call failed" << pa_strerror(pa_context_errno(context));
    std::unique_ptr<Request> owned = request->self->take(request);
    if (owned)
        owned->set(success != 0);
}

void PulseSinkVolume::querySink(quint32 index, const QByteArray &name,
                                std::function<void(const pa_sink_info *)> done)
{
    auto request = std::make_unique<Request>();
    request->sink = std::move(done);
    issue(std::move(request), [this, index, name](Request *raw) {
        if (index != PA_INVALID_INDEX)
            return pa_context_get_sink_info_by_index(m_context, index, &PulseSinkVolume::onRequestSink, raw);
        return pa_context_get_sink_info_by_name(m_context, name.constData(), &PulseSinkVolume::onRequestSink, raw);
    });
}

void PulseSinkVolume::queryCard(quint32 index, std::function<void(const pa_card_info *)> done)
{
    auto request = std::make_unique<Request>();
    request->card = std::move(done);
    issue(std::move(request), [this, index](Request *raw) {
        return pa_context_get_card_info_by_index(m_context, index, &PulseSinkVolume::onRequestCard, raw);
    });
}

void PulseSinkVolume::queryInputs(std::function<void(const QList<quint32> *)> done)
{
    auto request = std::make_unique<Request>();
    request->inputs = std::move(done);
    issue(std::move(request), [this](Request *raw) {
        return pa_context_get_sink_input_info_list(m_context, &PulseSinkVolume::onRequestInput, raw);
    });
}

void PulseSinkVolume::issue(std::unique_ptr<Request> request, const std::function<pa_operation *(Request *)> &start)
{
    request->self = this;
    if (!m_context) {
        fail(std::move(request));
        return;
    }
    Request *raw = request.get();
    m_requests.push_back(std::move(request));
    pa_operation *operation = start(raw);
    if (!operation) {
        fail(take(raw));
        return;
    }
    pa_operation_unref(operation);
}

std::unique_ptr<PulseSinkVolume::Request> PulseSinkVolume::take(Request *request)
{
    for (auto it = m_requests.begin(); it != m_requests.end(); ++it) {
        if (it->get() == request) {
            std::unique_ptr<Request> owned = std::move(*it);
            m_requests.erase(it);
            return owned;
        }
    }
    return nullptr;
}

void PulseSinkVolume::fail(std::unique_ptr<Request> request)
{
    if (!request || request->delivered)
        return;
    QMetaObject::invokeMethod(
        this,
        [sink = std::move(request->sink), card = std::move(request->card), inputs = std::move(request->inputs),
         set = std::move(request->set)] {
            if (sink)
                sink(nullptr);
            if (card)
                card(nullptr);
            if (inputs)
                inputs(nullptr);
            if (set)
                set(false);
        },
        Qt::QueuedConnection);
}

void PulseSinkVolume::failPending()
{
    std::vector<std::unique_ptr<Request>> pending = std::move(m_requests);
    m_requests.clear();
    for (std::unique_ptr<Request> &request : pending)
        fail(std::move(request));
}

void PulseSinkVolume::watchSink(quint32 index, bool appeared)
{
    const QMetaMethod signal = appeared ? QMetaMethod::fromSignal(&PulseSinkVolume::sinkAppeared)
                                        : QMetaMethod::fromSignal(&PulseSinkVolume::sinkChanged);
    if (!isSignalConnected(signal))
        return;
    querySink(index, QByteArray(), [this, appeared](const pa_sink_info *info) {
        if (!info)
            return;
        const int card = alsaCardOf(info->proplist);
        if (card < 0)
            return;
        if (appeared)
            emit sinkAppeared(info->index, QString::fromUtf8(info->name), card);
        else
            emit sinkChanged(info->index);
    });
}

void PulseSinkVolume::dacCall(const char *what, const std::function<pa_operation *()> &start)
{
    pa_operation *operation = m_context ? start() : nullptr;
    if (!operation) {
        qWarning() << "casdap volume: dac sink call failed" << what;
        return;
    }
    pa_operation_unref(operation);
}

RoutedSinkVolume::RoutedSinkVolume(SinkVolume *pulse, DacSinkPort *dac, AlsaVolume *alsa, int storeDelayMs,
                                   QObject *parent)
    : SinkVolume(parent)
    , m_pulse(pulse)
    , m_dac(dac)
    , m_alsa(alsa)
{
    m_pulse->setParent(this);
    m_store.setSingleShot(true);
    m_store.setInterval(storeDelayMs);
    connect(&m_store, &QTimer::timeout, this, [this] { storeSync(m_card); });
    connect(m_pulse, &SinkVolume::levelChanged, this, [this](int percent) {
        if (m_route == Route::Pulse)
            emit levelChanged(percent);
    });
    connect(m_alsa, &AlsaVolume::levelChanged, this, [this](int percent) {
        if (m_route == Route::Alsa)
            emit levelChanged(percent);
    });
}

void RoutedSinkVolume::step(int percent)
{
    if (m_route == Route::Pulse) {
        m_pulse->step(percent);
        return;
    }
    if (percent == 0)
        return;
    if (!mixerOn(false)) {
        qWarning().noquote() << "casdap volume: press skipped, mixer not open on" << m_card;
        return;
    }
    m_alsa->pressed(percent);
    keepOwned();
    if (percent > 0 && !m_sink.isEmpty())
        m_dac->unmuteSink(m_sink);
    m_dirty = true;
    if (m_storeAllowed)
        m_store.start();
}

void RoutedSinkVolume::setRoute(Route route, const QString &cardId, const QString &sink)
{
    const QString card = route == Route::Alsa ? cardId : QString();
    if (route == m_route && card == m_card) {
        m_sink = route == Route::Alsa ? sink : QString();
        return;
    }
    keepOwned();
    m_store.stop();
    m_dirty = false;
    if (route == Route::Alsa)
        m_pulse->dropPending();
    m_route = route;
    m_card = card;
    m_sink = route == Route::Alsa ? sink : QString();
    if (route == Route::Alsa)
        qInfo().noquote() << "casdap volume: route alsa hw:CARD=" + card;
    else
        qInfo() << "casdap volume: route pulse";
}

void RoutedSinkVolume::setStoreAllowed(bool allowed)
{
    m_storeAllowed = allowed;
    if (!allowed)
        m_store.stop();
    else if (m_dirty && m_route == Route::Alsa && !m_store.isActive())
        m_store.start();
}

void RoutedSinkVolume::storeSync(const QString &cardId)
{
    if (m_route != Route::Alsa || cardId != m_card || m_sink.isEmpty()) {
        qWarning().noquote() << "casdap volume: store sync skipped for" << cardId;
        return;
    }
    if (!mixerOn(true)) {
        qWarning().noquote() << "casdap volume: store sync skipped, mixer not open on" << cardId;
        return;
    }
    m_store.stop();
    m_dirty = false;
    const double db = m_alsa->stepDb();
    qInfo().noquote() << "casdap volume: store sync" << cardId << "step" << m_alsa->step() << db << "dB";
    QPointer<RoutedSinkVolume> self(this);
    m_dac->setSinkVolumeDb(m_sink, db, [self, cardId](bool ok) {
        if (!self || self->m_route != Route::Alsa || self->m_card != cardId || !self->mixerOn(true))
            return;
        if (!ok) {
            qWarning().noquote() << "casdap volume: store sync failed on" << cardId;
            return;
        }
        self->m_alsa->nudge();
        self->m_alsa->watchReturn(storeWatchMs);
    });
}

int RoutedSinkVolume::ownedStep(const QString &cardId) const
{
    if (!cardId.isEmpty() && cardId == m_alsa->cardId() && m_alsa->rangeDb() > 0.0)
        return m_alsa->step();
    return m_owned.value(cardId, -1);
}

void RoutedSinkVolume::setOwnedStep(const QString &cardId, int step)
{
    m_owned.insert(cardId, step);
    if (!cardId.isEmpty() && cardId == m_alsa->cardId()) {
        m_alsa->setOwnedStep(step);
        keepOwned();
    }
}

DacVolumePort::Route RoutedSinkVolume::route() const
{
    return m_route;
}

void RoutedSinkVolume::keepOwned()
{
    const QString card = m_alsa->cardId();
    if (!card.isEmpty() && m_alsa->rangeDb() > 0.0)
        m_owned.insert(card, m_alsa->step());
}

bool RoutedSinkVolume::mixerOn(bool requireOpen) const
{
    return (!requireOpen || m_alsa->isOpen()) && !m_card.isEmpty() && m_alsa->cardId() == m_card
        && m_alsa->rangeDb() > 0.0;
}

VolumeKeys::VolumeKeys(KeySource *keys, SinkVolume *sink, Settings *settings, QObject *parent)
    : QObject(parent)
    , m_keys(keys)
    , m_sink(sink)
    , m_settings(settings)
{
    m_keys->setParent(this);
    m_sink->setParent(this);
    connect(m_keys, &KeySource::step, this,
            [this](int direction) { m_sink->step(direction * m_settings->volumeStep()); });
    connect(m_sink, &SinkVolume::levelChanged, this, &VolumeKeys::levelChanged);
}

}
