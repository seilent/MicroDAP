#include "Player.h"

#include "BluezModel.h"
#include "Library.h"

#include <QDebug>
#include <QFile>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QSocketNotifier>

#include <algorithm>
#include <utility>

namespace casdap {

namespace {

constexpr int positionIntervalMs = 250;
constexpr int phoneHoldMs = 1000;
constexpr qint64 restartThresholdMs = 3000;
constexpr guint audioOnlyFlags = 0x2;
constexpr const char *eqDescription =
    "audioconvert ! audio/x-raw,format=F32LE ! volume name=casdap-preamp "
    "! equalizer-nbands name=casdap-eq num-bands=10 ! audioconvert";
constexpr const char *bitPerfectFilter = "audioconvert ! capsfilter caps=audio/x-raw,format=F32LE";

GstElement *parseBin(const QString &description, const char *what)
{
    if (description.isEmpty()) {
        qWarning() << "casdap player:" << what << "empty";
        return nullptr;
    }
    const QByteArray text = description.toUtf8();
    GError *err = nullptr;
    GstElement *bin = gst_parse_bin_from_description(text.constData(), TRUE, &err);
    if (!bin)
        qWarning() << "casdap player:" << what << description << (err ? err->message : "failed");
    g_clear_error(&err);
    return bin;
}

void detach(GstElement *element)
{
    GstObject *parent = gst_object_get_parent(GST_OBJECT(element));
    if (!parent)
        return;
    gst_bin_remove(GST_BIN(parent), element);
    gst_object_unref(parent);
}

QVariantMap formatOf(GstCaps *caps)
{
    QVariantMap map;
    if (!caps || gst_caps_get_size(caps) == 0)
        return map;
    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    if (const gchar *format = gst_structure_get_string(structure, "format"))
        map.insert(QStringLiteral("format"), QString::fromUtf8(format));
    gint value = 0;
    if (gst_structure_get_int(structure, "rate", &value))
        map.insert(QStringLiteral("rate"), value);
    if (gst_structure_get_int(structure, "channels", &value))
        map.insert(QStringLiteral("channels"), value);
    return map;
}

QVariantMap padFormat(GstPad *pad)
{
    if (!pad)
        return {};
    GstCaps *caps = gst_pad_get_current_caps(pad);
    const QVariantMap map = formatOf(caps);
    if (caps)
        gst_caps_unref(caps);
    gst_object_unref(pad);
    return map;
}

int remapMoved(int r, int start, int end, int row)
{
    const int n = end - start + 1;
    if (row > end) {
        if (r >= start && r <= end)
            return r - start + row - n;
        if (r > end && r < row)
            return r - n;
        return r;
    }
    if (r >= start && r <= end)
        return r - start + row;
    if (r >= row && r < start)
        return r + n;
    return r;
}

}

void initGStreamer(int &argc, char **argv)
{
    gst_init(&argc, &argv);
}

Player::Player(Library *library, QObject *parent)
    : QObject(parent)
    , m_library(library)
    , m_queue(new Queue(this))
{
    m_playbin = gst_element_factory_make("playbin", "casdap");
    if (!m_playbin) {
        qWarning() << "casdap player: playbin not available";
    } else {
        g_object_set(m_playbin, "flags", audioOnlyFlags, nullptr);
        m_audioSinkEnv = qEnvironmentVariable("CASDAP_AUDIO_SINK");
        if (m_audioSinkEnv.isNull() && qEnvironmentVariableIsSet("CASDAP_AUDIO_SINK"))
            m_audioSinkEnv = QLatin1String("");
        m_exclusiveEnv = qEnvironmentVariable("CASDAP_EXCLUSIVE_SINK");
        if (qEnvironmentVariableIntValue("CASDAP_EQ_DISABLE") != 1)
            buildEq();
        configure(OutputChain::Pw);
        m_bus = gst_element_get_bus(m_playbin);
        GPollFD fd;
        gst_bus_get_pollfd(m_bus, &fd);
        m_busNotifier = new QSocketNotifier(fd.fd, QSocketNotifier::Read, this);
        connect(m_busNotifier, &QSocketNotifier::activated, this, &Player::pollBus);
        m_aboutToFinishId = g_signal_connect(m_playbin, "about-to-finish",
                                             G_CALLBACK(&Player::aboutToFinish), this);
    }

    m_positionTimer.setInterval(positionIntervalMs);
    connect(&m_positionTimer, &QTimer::timeout, this, &Player::pollPosition);
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(phoneHoldMs);
    connect(&m_holdTimer, &QTimer::timeout, this, &Player::releaseHold);

    connect(m_queue, &Queue::countChanged, this, &Player::onCountChanged);
    connect(m_queue, &QAbstractItemModel::modelReset, this, &Player::syncChain);
    connect(m_queue, &Queue::shuffleChanged, this, &Player::syncChain);
    connect(m_queue, &Queue::repeatChanged, this, &Player::syncChain);
    connect(m_queue, &Queue::orderChanged, this, &Player::syncChain);
    connect(m_queue, &QAbstractItemModel::rowsAboutToBeRemoved, this,
            &Player::onRowsAboutToBeRemoved);
    connect(m_queue, &QAbstractItemModel::rowsRemoved, this, &Player::syncChain);
    connect(m_queue, &QAbstractItemModel::rowsInserted, this, &Player::onRowsInserted);
    connect(m_queue, &QAbstractItemModel::rowsMoved, this, &Player::onRowsMoved);
    connect(m_queue, &QAbstractItemModel::modelReset, this, &Player::currentChanged);
    connect(m_queue, &Queue::countChanged, this, &Player::currentChanged);
    connect(m_queue, &Queue::indexChanged, this, &Player::currentChanged);
}

Player::~Player()
{
    delete m_busNotifier;
    m_busNotifier = nullptr;
    m_positionTimer.stop();
    if (m_playbin) {
        if (m_aboutToFinishId)
            g_signal_handler_disconnect(m_playbin, m_aboutToFinishId);
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    }
    for (GObject *band : std::as_const(m_eqBands))
        g_object_unref(band);
    m_eqBands.clear();
    if (m_preamp)
        gst_object_unref(m_preamp);
    if (m_bus) {
        gst_bus_set_flushing(m_bus, TRUE);
        gst_object_unref(m_bus);
    }
    if (m_playbin)
        gst_object_unref(m_playbin);
    if (m_eqFilter)
        gst_object_unref(m_eqFilter);
}

Player::Status Player::status() const
{
    return m_status;
}

Player::PlayState Player::playState() const
{
    switch (m_status) {
    case Playing:
        return PlayState::Playing;
    case Paused:
        return PlayState::Paused;
    case Stopped:
        break;
    }
    return PlayState::Stopped;
}

qint64 Player::positionMs() const
{
    return position();
}

qint64 Player::position() const
{
    if (m_positionPoll || !m_playbin || m_status != Playing || m_playAfterSeek)
        return m_position;
    gint64 position = 0;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &position) && position >= 0)
        return position / GST_MSECOND;
    return m_position;
}

qint64 Player::duration() const
{
    return m_duration;
}

QVariantMap Player::current() const
{
    QVariantMap map = m_queue->get(m_queue->currentIndex());
    if (m_library && !map.isEmpty()) {
        const qint64 id = map.value(QStringLiteral("id")).toLongLong();
        map.insert(QStringLiteral("rating"), m_library->rating(id));
        const QVariantList rows = m_library->trackRows({id});
        if (!rows.isEmpty()) {
            const QVariantMap row = rows.first().toMap();
            map.insert(QStringLiteral("sampleRate"), row.value(QStringLiteral("sampleRate")));
            map.insert(QStringLiteral("bitDepth"), row.value(QStringLiteral("bitDepth")));
        }
    }
    return map;
}

Queue *Player::queue() const
{
    return m_queue;
}

Library *Player::library() const
{
    return m_library;
}

bool Player::isValid() const
{
    return m_playbin != nullptr;
}

void Player::buildEq()
{
    GError *err = nullptr;
    GstElement *filter = gst_parse_bin_from_description(eqDescription, TRUE, &err);
    if (!filter || err) {
        qWarning() << "casdap player: eq filter" << (err ? err->message : "failed");
        g_clear_error(&err);
        if (filter)
            gst_object_unref(gst_object_ref_sink(filter));
        return;
    }
    GstElement *preamp = gst_bin_get_by_name(GST_BIN(filter), "casdap-preamp");
    GstElement *eq = gst_bin_get_by_name(GST_BIN(filter), "casdap-eq");
    QList<GObject *> bands;
    if (eq) {
        for (guint i = 0; i < guint(EqProfile::bandCount); ++i) {
            if (GObject *band = gst_child_proxy_get_child_by_index(GST_CHILD_PROXY(eq), i))
                bands.append(band);
        }
        gst_object_unref(eq);
    }
    if (!preamp || bands.size() != EqProfile::bandCount) {
        qWarning() << "casdap player: eq filter incomplete";
        for (GObject *band : std::as_const(bands))
            g_object_unref(band);
        if (preamp)
            gst_object_unref(preamp);
        gst_object_unref(gst_object_ref_sink(filter));
        return;
    }
    m_eqFilter = GST_ELEMENT(gst_object_ref_sink(filter));
    m_preamp = preamp;
    m_eqBands = bands;
    applyEq(EqProfile::off());
}

QString Player::sinkDescription(OutputChain chain, const QString &audioSinkEnv,
                                const QString &exclusiveEnv, const QString &policyExclusive,
                                const QString &cardId)
{
    switch (chain) {
    case OutputChain::Bp:
        return audioSinkEnv.isNull() ? QStringLiteral("pulsesink volume=1.0 mute=false") : audioSinkEnv;
    case OutputChain::Ex:
        if (exclusiveEnv.isEmpty())
            return policyExclusive;
        return QString(exclusiveEnv).replace(QStringLiteral("%1"), cardId);
    case OutputChain::None:
    case OutputChain::Pw:
        break;
    }
    return audioSinkEnv.isNull() ? QStringLiteral("pulsesink") : audioSinkEnv;
}

GstElement *Player::pwFilter(int rate)
{
    if (m_eqFilter)
        detach(m_eqFilter);
    if (rate <= 0)
        return m_eqFilter;
    GstElement *tail =
        parseBin(QStringLiteral("audioresample ! capsfilter caps=audio/x-raw,rate=%1").arg(rate), "rate filter");
    if (!tail)
        return m_eqFilter;
    if (!m_eqFilter)
        return tail;
    GstElement *bin = gst_bin_new(nullptr);
    gst_bin_add_many(GST_BIN(bin), m_eqFilter, tail, nullptr);
    GstPad *sink = gst_element_get_static_pad(m_eqFilter, "sink");
    GstPad *src = gst_element_get_static_pad(tail, "src");
    const bool ok = sink && src && gst_element_link(m_eqFilter, tail)
                    && gst_element_add_pad(bin, gst_ghost_pad_new("sink", sink))
                    && gst_element_add_pad(bin, gst_ghost_pad_new("src", src));
    if (sink)
        gst_object_unref(sink);
    if (src)
        gst_object_unref(src);
    if (ok)
        return bin;
    qWarning() << "casdap player: rate filter link failed" << rate;
    detach(m_eqFilter);
    gst_object_unref(gst_object_ref_sink(bin));
    return m_eqFilter;
}

bool Player::configure(OutputChain chain)
{
    const bool exclusive = chain == OutputChain::Ex && m_policy;
    const QString card = exclusive ? m_policy->exclusiveCard() : QString();
    const int rate = chain == OutputChain::Pw && m_policy ? m_policy->fixedRate() : 0;
    const QString description = sinkDescription(chain, m_audioSinkEnv, m_exclusiveEnv,
                                                exclusive ? m_policy->exclusiveSink() : QString(), card);
    if (m_configChain != OutputChain::None && chain == m_configChain && description == m_configSink
        && card == m_configCard && rate == m_configRate)
        return true;
    GstElement *sink = parseBin(description, "audio sink");
    GstElement *filter = nullptr;
    if (chain == OutputChain::Bp)
        filter = parseBin(QString::fromLatin1(bitPerfectFilter), "bit perfect filter");
    if (chain != OutputChain::Pw && (!sink || (chain == OutputChain::Bp && !filter))) {
        if (sink)
            gst_object_unref(gst_object_ref_sink(sink));
        if (filter)
            gst_object_unref(gst_object_ref_sink(filter));
        m_configChain = OutputChain::None;
        m_configSink.clear();
        m_configCard.clear();
        m_configRate = 0;
        return false;
    }
    m_configChain = chain;
    m_configSink = description;
    m_configCard = card;
    m_configRate = rate;
    if (sink || m_sink) {
        g_object_set(m_playbin, "audio-sink", sink, nullptr);
        m_sink = sink;
    }
    if (chain == OutputChain::Pw)
        filter = pwFilter(rate);
    g_object_set(m_playbin, "audio-filter", filter, nullptr);
    m_filter = filter;
    return true;
}

GstElement *Player::audioFilter() const
{
    return m_filter;
}

GstElement *Player::audioSink() const
{
    return m_sink;
}

void Player::setPolicy(OutputPolicy *policy)
{
    m_policy = policy;
}

Player::OutputChain Player::builtChain() const
{
    return m_builtChain;
}

bool Player::parked() const
{
    return m_parked;
}

std::optional<Player::PendingBuild> Player::pendingBuild() const
{
    return m_pending;
}

QVariantMap Player::sourceFormat() const
{
    return m_sourceFormat;
}

QVariantMap Player::sinkFormat() const
{
    return m_sinkFormat;
}

void Player::setFormats(const QVariantMap &source, const QVariantMap &sink)
{
    if (source == m_sourceFormat && sink == m_sinkFormat)
        return;
    m_sourceFormat = source;
    m_sinkFormat = sink;
    emit formatChanged();
}

void Player::readFormats()
{
    GstPad *audio = nullptr;
    if (g_signal_lookup("get-audio-pad", G_OBJECT_TYPE(m_playbin))) {
        gint current = 0;
        g_object_get(m_playbin, "current-audio", &current, nullptr);
        g_signal_emit_by_name(m_playbin, "get-audio-pad", qMax(0, current), &audio);
    }
    setFormats(padFormat(audio), padFormat(m_sink ? gst_element_get_static_pad(m_sink, "sink") : nullptr));
}

void Player::applyEq(const EqProfile &profile)
{
    if (!m_preamp || m_eqBands.size() != EqProfile::bandCount)
        return;
    g_object_set(m_preamp, "volume", profile.preampVolume(), nullptr);
    for (int i = 0; i < EqProfile::bandCount; ++i) {
        const EqBand &band = profile.bands[i];
        g_object_set(m_eqBands.at(i),
                     "type", static_cast<gint>(band.type),
                     "freq", band.freq,
                     "bandwidth", EqProfile::bandwidth(band),
                     "gain", band.enabled ? band.gain : 0.0,
                     nullptr);
    }
}

BluezModel *Player::bluez() const
{
    return m_bluez;
}

void Player::setBluez(BluezModel *bluez)
{
    if (m_bluez == bluez)
        return;
    if (m_bluez) {
        disconnect(m_bluez, nullptr, this, nullptr);
        disconnect(this, nullptr, m_bluez, nullptr);
    }
    m_bluez = bluez;
    if (!m_bluez) {
        setTransportActive(false);
        setTransportReady(true);
        return;
    }
    connect(m_bluez, &BluezModel::transportActiveChanged, this, &Player::onTransportActiveChanged);
    connect(m_bluez, &BluezModel::readyChanged, this,
            [this] { setTransportReady(m_bluez->ready()); });
    connect(this, &Player::localPlayRequested, m_bluez, &BluezModel::pause);
    connect(m_bluez, &QObject::destroyed, this, [this] {
        m_bluez = nullptr;
        setTransportActive(false);
        setTransportReady(true);
    });
    setTransportReady(m_bluez->ready());
    onTransportActiveChanged();
}

bool Player::transportActive() const
{
    return m_transportActive;
}

void Player::setTransportActive(bool active)
{
    if (m_transportActive == active)
        return;
    m_transportActive = active;
    if (active)
        pause();
    else if (m_playPending)
        releaseHold();
}

bool Player::transportReady() const
{
    return m_transportReady;
}

void Player::setTransportReady(bool ready)
{
    if (m_transportReady == ready)
        return;
    m_transportReady = ready;
    if (!ready || !m_resumeOnReady)
        return;
    m_resumeOnReady = false;
    if (!m_transportActive && m_status == Paused)
        resumeFrom();
}

bool Player::playPending() const
{
    return m_playPending;
}

bool Player::holdForPhone()
{
    if (!m_transportActive)
        return false;
    if (!m_playPending) {
        m_playPending = true;
        qInfo() << "casdap player: pausing the phone before local playback";
        emit localPlayRequested();
        m_holdTimer.start();
    }
    return true;
}

void Player::releaseHold()
{
    if (!m_playPending)
        return;
    m_playPending = false;
    m_holdTimer.stop();
    if (m_status == Paused)
        resumeFrom();
}

void Player::cancelHold()
{
    m_playPending = false;
    m_holdTimer.stop();
    m_resumeOnReady = false;
}

void Player::resumeFrom()
{
    if (!m_playbin || m_status != Paused)
        return;
    const OutputChain wanted = m_policy ? m_policy->chainFor(GST_STATE_PLAYING) : OutputChain::Pw;
    if (!m_parked && !m_pending && m_builtChain == wanted) {
        resumeNow();
        return;
    }
    const int index = m_queue->currentIndex();
    if (index < 0 || index >= m_queue->count()) {
        stop();
        return;
    }
    request(index, GST_STATE_PLAYING, m_pending ? m_pending->positionMs : m_position, false,
            !m_pending || m_pending->sameListen);
}

void Player::rebuildAt(qint64 positionMs, GstState state)
{
    startAt(m_queue->currentIndex(), state, positionMs, true);
}

void Player::park(qint64 positionMs)
{
    const int index = m_queue->currentIndex();
    if (!m_playbin || m_status == Stopped || index < 0 || index >= m_queue->count())
        return;
    cancelHold();
    m_pending.reset();
    setNull();
    m_parked = true;
    setStatus(Paused);
    setPosition(qMax<qint64>(0, positionMs));
}

void Player::buildPending(OutputChain chain)
{
    if (!m_pending)
        return;
    PendingBuild pending = *std::exchange(m_pending, std::nullopt);
    const int index = m_queue->currentIndex();
    if (!m_playbin || index < 0 || index >= m_queue->count()) {
        stop();
        return;
    }
    const QueueEntry current = m_queue->entry(index);
    if (current.id != pending.entry.id || current.path != pending.entry.path) {
        pending.entry = current;
        pending.positionMs = 0;
        pending.sameListen = false;
    }
    build(index, pending, chain, false);
}

void Player::resumeNow()
{
    if (!m_playbin || m_status != Paused)
        return;
    m_resumeOnReady = false;
    gst_element_set_state(m_playbin, GST_STATE_PLAYING);
    setStatus(Playing);
    if (!m_historyCounted)
        addHistoryNow();
}

bool Player::positionPollEnabled() const
{
    return m_positionPoll;
}

void Player::setPositionPollEnabled(bool on)
{
    if (m_positionPoll == on)
        return;
    m_positionPoll = on;
    if (on && m_status == Playing) {
        pollPosition();
        m_positionTimer.start();
    } else {
        m_positionTimer.stop();
    }
    emit positionPollEnabledChanged();
}

void Player::onTransportActiveChanged()
{
    setTransportActive(m_bluez && m_bluez->transportActive());
}

void Player::aboutToFinish(GstElement *element, gpointer data)
{
    auto *self = static_cast<Player *>(data);
    QString path;
    {
        QMutexLocker lock(&self->m_chainMutex);
        Chain &chain = self->m_chain;
        const int next = Queue::nextOf(chain.last, static_cast<int>(chain.entries.size()), chain.order,
                                       chain.slot, chain.shuffle, chain.repeat, false);
        if (next < 0 || next >= chain.entries.size())
            return;
        path = chain.entries.at(next).path;
        chain.last = next;
        chain.pending.append(next);
    }
    gchar *uri = gst_filename_to_uri(QFile::encodeName(path).constData(), nullptr);
    if (!uri)
        return;
    g_object_set(element, "uri", uri, nullptr);
    g_free(uri);
}

void Player::syncChain()
{
    QList<QueueEntry> entries = m_queue->entries();
    QList<int> order = m_queue->order();
    QList<int> slot = m_queue->slotIndex();
    {
        QMutexLocker lock(&m_chainMutex);
        m_chain.entries.swap(entries);
        m_chain.order.swap(order);
        m_chain.slot.swap(slot);
        m_chain.shuffle = m_queue->shuffle();
        m_chain.repeat = m_queue->repeat();
        if (m_chain.pending.isEmpty())
            m_chain.last = m_queue->currentIndex();
    }
}

void Player::onCountChanged()
{
    if (!m_removingCurrent)
        return;
    m_removingCurrent = false;
    if (m_status == Stopped)
        return;
    if (m_removeTarget < 0) {
        stop();
        return;
    }
    startAt(m_removeTarget, m_status == Playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
}

void Player::onRowsAboutToBeRemoved(const QModelIndex &, int first, int last)
{
    const int n = last - first + 1;
    {
        QMutexLocker lock(&m_chainMutex);
        bool hit = false;
        for (int row : std::as_const(m_chain.pending))
            hit = hit || (row >= first && row <= last);
        if (hit) {
            m_chain.pending.clear();
            m_chain.stale = true;
        } else {
            for (int &row : m_chain.pending) {
                if (row > last)
                    row -= n;
            }
        }
        if (m_chain.last > last)
            m_chain.last -= n;
    }

    const int current = m_queue->currentIndex();
    if (current < first || current > last)
        return;
    m_removingCurrent = true;
    m_removeTarget = -1;
    const int count = m_queue->count();
    const QList<int> order = m_queue->order();
    const QList<int> slot = m_queue->slotIndex();
    int row = current;
    for (int i = 0; i <= count; ++i) {
        row = Queue::nextOf(row, count, order, slot, m_queue->shuffle(), m_queue->repeat(), true);
        if (row < 0 || row < first || row > last)
            break;
    }
    if (row >= first && row <= last)
        row = -1;
    if (row >= 0)
        m_removeTarget = row > last ? row - n : row;
}

void Player::onRowsInserted(const QModelIndex &, int first, int last)
{
    const int n = last - first + 1;
    {
        QMutexLocker lock(&m_chainMutex);
        for (int &row : m_chain.pending) {
            if (row >= first)
                row += n;
        }
        if (m_chain.last >= first)
            m_chain.last += n;
    }
    syncChain();
}

void Player::onRowsMoved(const QModelIndex &, int start, int end, const QModelIndex &, int row)
{
    {
        QMutexLocker lock(&m_chainMutex);
        for (int &pending : m_chain.pending)
            pending = remapMoved(pending, start, end, row);
        if (m_chain.last >= 0)
            m_chain.last = remapMoved(m_chain.last, start, end, row);
    }
    syncChain();
}

void Player::setUri(const QString &path)
{
    gchar *uri = gst_filename_to_uri(QFile::encodeName(path).constData(), nullptr);
    if (!uri) {
        qWarning() << "casdap player: bad path" << path;
        return;
    }
    g_object_set(m_playbin, "uri", uri, nullptr);
    g_free(uri);
}

void Player::setNull()
{
    m_positionTimer.stop();
    if (m_playbin)
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    if (m_bus) {
        while (GstMessage *message = gst_bus_pop(m_bus))
            gst_message_unref(message);
    }
    m_builtChain = OutputChain::None;
    m_parked = false;
    setFormats({}, {});
    QMutexLocker lock(&m_chainMutex);
    m_chain.pending.clear();
    m_chain.stale = false;
    m_prerolled = false;
    m_pendingSeek = -1;
    m_playAfterSeek = false;
}

void Player::startAt(int index, GstState target, qint64 positionMs, bool sameListen)
{
    if (!m_playbin || index < 0 || index >= m_queue->count()) {
        stop();
        return;
    }
    request(index, target, positionMs, true, sameListen);
}

void Player::request(int index, GstState target, qint64 positionMs, bool guard, bool sameListen)
{
    PendingBuild pending{m_queue->entry(index), target, positionMs, sameListen};
    if (!m_policy) {
        m_pending.reset();
        build(index, pending, OutputChain::Pw, guard);
        return;
    }
    if (guard && pending.target == GST_STATE_PLAYING && holdForPhone())
        pending.target = GST_STATE_PAUSED;
    const Admit admit = m_policy->admit(pending);
    if (admit.now) {
        m_pending.reset();
        build(index, pending, admit.chain, false);
        return;
    }
    hold(index, pending);
}

void Player::hold(int index, const PendingBuild &pending)
{
    setNull();
    m_pending = pending;
    m_queue->setCurrentIndex(index);
    if (!pending.sameListen) {
        m_historyCounted = true;
        m_listenStarted = false;
    }
    setPosition(pending.positionMs);
    setStatus(pending.target == GST_STATE_PLAYING ? Playing : Paused);
    emit currentChanged();
}

void Player::build(int index, const PendingBuild &pending, OutputChain chain, bool guard)
{
    setNull();
    if (!configure(chain)) {
        if (chain == OutputChain::Ex && m_policy) {
            hold(index, pending);
            qWarning() << "casdap player: output error" << int(GST_RESOURCE_ERROR_FAILED) << "exclusive sink";
            m_policy->outputError(GST_RESOURCE_ERROR_FAILED);
            return;
        }
        chain = OutputChain::Pw;
        configure(chain);
    }
    m_builtChain = chain;
    m_sameListen = pending.sameListen && m_listenStarted;
    m_listenCounted = m_historyCounted;
    m_listenStarted = m_sameListen;
    m_outputErrorSent = false;
    m_queue->setCurrentIndex(index);
    {
        QMutexLocker lock(&m_chainMutex);
        m_chain.last = index;
        m_chain.pending = {index};
    }
    GstState target = pending.target;
    if (guard && target == GST_STATE_PLAYING && holdForPhone())
        target = GST_STATE_PAUSED;
    m_historyCounted = true;
    setPosition(0);
    setUri(pending.entry.path);
    m_playAfterSeek = m_policy && target == GST_STATE_PLAYING && pending.positionMs > 0;
    gst_element_set_state(m_playbin, m_playAfterSeek ? GST_STATE_PAUSED : target);
    setStatus(target == GST_STATE_PLAYING ? Playing : Paused);
    emit currentChanged();
    if (pending.positionMs > 0) {
        m_pendingSeek = pending.positionMs;
        setPosition(pending.positionMs);
    }
}

void Player::play(const QList<QueueEntry> &entries, int start)
{
    playEntries(entries, start, m_queue->shuffle());
}

void Player::playEntries(const QList<QueueEntry> &entries, int start, bool shuffle)
{
    m_errorStreak = 0;
    m_firstErrorRow = -1;
    m_queue->set(entries, start, {}, shuffle);
    startAt(m_queue->currentIndex(), GST_STATE_PLAYING);
}

int Player::playSource(const QString &kind, qint64 id, const QString &mode, qint64 startId)
{
    if (!m_library)
        return 0;
    const bool add = mode == QLatin1String("add");
    const bool next = mode == QLatin1String("next");
    const bool shuffle = mode == QLatin1String("shuffle");
    if (!add && !next && !shuffle && mode != QLatin1String("play")) {
        qWarning() << "casdap player: unknown source mode" << mode;
        return 0;
    }
    const QList<QueueEntry> entries = kind == QLatin1String("song")
                                          ? m_library->queueEntries({QVariant(id)})
                                          : m_library->entriesFor(kind, id);
    if (entries.isEmpty())
        return 0;
    if (add || next)
        return addEntries(entries, next);
    const int n = static_cast<int>(entries.size());
    if (shuffle || (startId < 0 && m_queue->shuffle())) {
        playEntries(entries, QRandomGenerator::global()->bounded(n), true);
        return n;
    }
    int start = 0;
    if (startId >= 0) {
        for (int i = 0; i < n; ++i) {
            if (entries.at(i).id == startId) {
                start = i;
                break;
            }
        }
    }
    playEntries(entries, start, m_queue->shuffle());
    return n;
}

void Player::playTracks(const QVariantList &trackIds, int start)
{
    if (!m_library)
        return;
    const QList<QueueEntry> entries = m_library->queueEntries(trackIds);
    if (entries.isEmpty())
        return;
    play(entries, std::max(0, std::min(static_cast<int>(entries.size()) - 1, start)));
}

int Player::enqueueTracks(const QVariantList &trackIds)
{
    return addTracks(trackIds, false);
}

int Player::playNextTracks(const QVariantList &trackIds)
{
    return addTracks(trackIds, true);
}

int Player::addTracks(const QVariantList &trackIds, bool next)
{
    if (!m_library)
        return 0;
    return addEntries(m_library->queueEntries(trackIds), next);
}

int Player::addEntries(const QList<QueueEntry> &entries, bool next)
{
    if (entries.isEmpty())
        return 0;
    const int count = static_cast<int>(entries.size());
    if (m_queue->count() == 0 || m_status == Stopped) {
        m_errorStreak = 0;
        m_firstErrorRow = -1;
        m_queue->set(entries, 0);
        startAt(0, GST_STATE_PAUSED);
    } else if (next) {
        m_queue->playNext(entries);
    } else {
        m_queue->enqueue(entries);
    }
    emit added(count);
    return count;
}

void Player::restore(const QList<QueueEntry> &entries, int index, qint64 positionMs, bool playing,
                     const QList<int> &order)
{
    m_errorStreak = 0;
    m_firstErrorRow = -1;
    m_queue->set(entries, index, order);
    if (m_queue->count() == 0)
        return;
    const bool resume = playing && m_transportReady && !m_transportActive;
    startAt(m_queue->currentIndex(), resume ? GST_STATE_PLAYING : GST_STATE_PAUSED, positionMs);
    m_resumeOnReady = playing && !m_transportReady;
    if (positionMs > 0 && m_status == Stopped) {
        m_pendingSeek = positionMs;
        setPosition(positionMs);
    }
}

void Player::playAt(int index)
{
    startAt(index, GST_STATE_PLAYING);
}

void Player::playPause()
{
    if (m_status == Playing)
        pause();
    else
        resume();
}

void Player::resume()
{
    if (!m_playbin)
        return;
    switch (m_status) {
    case Playing:
        break;
    case Paused:
        if (!holdForPhone())
            resumeFrom();
        break;
    case Stopped:
        if (m_queue->count() > 0)
            startAt(qMax(0, m_queue->currentIndex()), GST_STATE_PLAYING);
        break;
    }
}

void Player::pause()
{
    cancelHold();
    if (m_pending) {
        park(m_pending->positionMs);
        return;
    }
    if (!m_playbin || m_status != Playing)
        return;
    pollPosition();
    m_playAfterSeek = false;
    gst_element_set_state(m_playbin, GST_STATE_PAUSED);
    setStatus(Paused);
}

void Player::stop()
{
    cancelHold();
    m_pending.reset();
    setNull();
    setStatus(Stopped);
    setPosition(0);
}

void Player::next()
{
    const int index = m_queue->nextIndex(true);
    if (index < 0) {
        stop();
        return;
    }
    startAt(index, GST_STATE_PLAYING);
}

void Player::previous()
{
    if (position() > restartThresholdMs && m_status != Stopped) {
        seek(0);
        return;
    }
    const int index = m_queue->previousIndex(true);
    startAt(index < 0 ? m_queue->currentIndex() : index, GST_STATE_PLAYING);
}

bool Player::doSeek(qint64 ms)
{
    const auto flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT);
    const bool ok = gst_element_seek_simple(m_playbin, GST_FORMAT_TIME, flags, ms * GST_MSECOND);
    if (!ok)
        qWarning() << "casdap player: seek failed" << ms;
    setPosition(ms);
    return ok;
}

void Player::seek(qint64 ms)
{
    if (!m_playbin || m_status == Stopped)
        return;
    const qint64 target = qMax<qint64>(0, ms);
    if (m_pending) {
        m_pending->positionMs = target;
        setPosition(target);
        return;
    }
    if (m_parked) {
        setPosition(target);
        return;
    }
    if (!m_prerolled) {
        m_pendingSeek = target;
        setPosition(target);
        return;
    }
    doSeek(target);
}

void Player::toggleShuffle()
{
    m_queue->setShuffle(!m_queue->shuffle());
}

void Player::cycleRepeat()
{
    switch (m_queue->repeat()) {
    case Queue::Off:
        m_queue->setRepeat(Queue::All);
        break;
    case Queue::All:
        m_queue->setRepeat(Queue::One);
        break;
    case Queue::One:
        m_queue->setRepeat(Queue::Off);
        break;
    }
}

void Player::rate()
{
    if (!m_library)
        return;
    const qint64 id = m_queue->entry(m_queue->currentIndex()).id;
    if (id <= 0)
        return;
    m_library->setRating(id, m_library->rating(id) ? 0 : 1);
    emit currentChanged();
}

void Player::addHistoryNow()
{
    m_historyCounted = true;
    if (!m_library)
        return;
    const qint64 id = m_queue->entry(m_queue->currentIndex()).id;
    if (id > 0)
        m_library->addHistory(id);
}

void Player::setStatus(Status status)
{
    if (status == Playing && m_positionPoll)
        m_positionTimer.start();
    else
        m_positionTimer.stop();
    if (m_status == status)
        return;
    m_status = status;
    emit statusChanged();
}

void Player::setPosition(qint64 ms)
{
    if (m_position == ms)
        return;
    m_position = ms;
    emit positionChanged();
}

void Player::updateDuration()
{
    if (!m_playbin)
        return;
    gint64 duration = 0;
    if (!gst_element_query_duration(m_playbin, GST_FORMAT_TIME, &duration) || duration < 0)
        return;
    const qint64 ms = duration / GST_MSECOND;
    if (ms == m_duration)
        return;
    m_duration = ms;
    emit durationChanged();
}

void Player::pollPosition()
{
    if (!m_playbin || m_status != Playing || m_playAfterSeek)
        return;
    gint64 position = 0;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &position) && position >= 0)
        setPosition(position / GST_MSECOND);
}

void Player::handleStreamStart()
{
    int index = -1;
    bool stale = false;
    {
        QMutexLocker lock(&m_chainMutex);
        if (!m_chain.pending.isEmpty())
            index = m_chain.pending.takeFirst();
        else
            stale = std::exchange(m_chain.stale, false);
    }
    if (stale) {
        const int following = m_queue->nextIndex(false);
        if (following < 0)
            stop();
        else
            startAt(following, m_status == Playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
        return;
    }
    const bool same = std::exchange(m_sameListen, false);
    if (index >= 0 && index < m_queue->count())
        m_queue->setCurrentIndex(index);
    m_errorStreak = 0;
    m_firstErrorRow = -1;
    m_historyCounted = same && m_listenCounted;
    m_listenStarted = true;
    if (!m_policy || m_pendingSeek < 0)
        setPosition(0);
    updateDuration();
    readFormats();
    emit currentChanged();
    if (!same)
        emit trackStarted(m_queue->currentIndex());
    if (m_status == Playing && !m_historyCounted)
        addHistoryNow();
}

void Player::handleEos()
{
    setNull();
    setStatus(Stopped);
    setPosition(0);
    emit endOfStream();
}

void Player::handleAsyncDone()
{
    m_prerolled = true;
    bool seeking = false;
    if (m_pendingSeek >= 0) {
        const qint64 target = m_pendingSeek;
        m_pendingSeek = -1;
        seeking = doSeek(target);
        if (seeking && m_playAfterSeek)
            return;
    }
    if (std::exchange(m_playAfterSeek, false))
        gst_element_set_state(m_playbin, GST_STATE_PLAYING);
    updateDuration();
    if (!seeking)
        readFormats();
    pollPosition();
}

void Player::handleError(GstMessage *message)
{
    GError *err = nullptr;
    gchar *debug = nullptr;
    gst_message_parse_error(message, &err, &debug);
    const bool exclusive = m_builtChain == OutputChain::Ex && m_policy;
    const bool fromSink = m_sink && err && err->domain == GST_RESOURCE_ERROR
                          && gst_object_has_as_ancestor(GST_MESSAGE_SRC(message), GST_OBJECT(m_sink));
    if (exclusive && (m_outputErrorSent || fromSink)) {
        if (!std::exchange(m_outputErrorSent, true)) {
            qWarning() << "casdap player: output error" << err->code << err->message;
            m_policy->outputError(err->code);
        }
        g_clear_error(&err);
        g_free(debug);
        return;
    }
    const QString text = err ? QString::fromUtf8(err->message) : QStringLiteral("playback error");
    qWarning() << "casdap player:" << text << (debug ? debug : "");
    g_clear_error(&err);
    g_free(debug);
    emit error(text);

    ++m_errorStreak;
    const int current = m_queue->currentIndex();
    if (m_errorStreak == 1)
        m_firstErrorRow = current;
    const int index = m_queue->nextIndex(true);
    const bool exhausted = m_errorStreak >= m_queue->count() || index == current
                           || (index >= 0 && index == m_firstErrorRow);
    if (index < 0 && !exhausted) {
        m_errorStreak = 0;
        m_firstErrorRow = -1;
        stop();
        return;
    }
    if (exhausted) {
        qWarning() << "casdap: no playable track";
        const int first = m_firstErrorRow;
        m_errorStreak = 0;
        m_firstErrorRow = -1;
        stop();
        m_queue->setCurrentIndex(first);
        return;
    }
    startAt(index, m_status == Playing ? GST_STATE_PLAYING : GST_STATE_PAUSED);
}

void Player::pollBus()
{
    if (!m_bus || !m_playbin)
        return;
    const auto types = static_cast<GstMessageType>(
        GST_MESSAGE_ERROR | GST_MESSAGE_EOS | GST_MESSAGE_STATE_CHANGED | GST_MESSAGE_STREAM_START
        | GST_MESSAGE_DURATION_CHANGED | GST_MESSAGE_ASYNC_DONE);
    while (GstMessage *message = gst_bus_pop_filtered(m_bus, types)) {
        const bool fromPlaybin = GST_MESSAGE_SRC(message) == GST_OBJECT_CAST(m_playbin);
        switch (GST_MESSAGE_TYPE(message)) {
        case GST_MESSAGE_ERROR:
            handleError(message);
            break;
        case GST_MESSAGE_EOS:
            handleEos();
            break;
        case GST_MESSAGE_STREAM_START:
            if (fromPlaybin)
                handleStreamStart();
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (fromPlaybin) {
                GstState newState = GST_STATE_VOID_PENDING;
                gst_message_parse_state_changed(message, nullptr, &newState, nullptr);
                if (newState == GST_STATE_PAUSED || newState == GST_STATE_PLAYING)
                    updateDuration();
            }
            break;
        case GST_MESSAGE_DURATION_CHANGED:
            updateDuration();
            break;
        case GST_MESSAGE_ASYNC_DONE:
            if (fromPlaybin)
                handleAsyncDone();
            break;
        default:
            break;
        }
        gst_message_unref(message);
    }
}

}
