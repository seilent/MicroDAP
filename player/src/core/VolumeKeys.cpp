#include "VolumeKeys.h"

#include "Settings.h"

#include <QAbstractEventDispatcher>
#include <QDebug>
#include <QDir>
#include <QFile>
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
                                   pa_subscription_mask_t(PA_SUBSCRIPTION_MASK_SINK | PA_SUBSCRIPTION_MASK_SERVER),
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
    if (facility != PA_SUBSCRIPTION_EVENT_SINK || (type & PA_SUBSCRIPTION_EVENT_TYPE_MASK) != PA_SUBSCRIPTION_EVENT_CHANGE)
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
