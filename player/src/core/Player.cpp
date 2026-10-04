#include "Player.h"

#include "Library.h"

#include <QDebug>
#include <QFile>
#include <QMutexLocker>

namespace casdap {

namespace {

constexpr int busIntervalMs = 20;
constexpr int positionIntervalMs = 250;
constexpr qint64 restartThresholdMs = 3000;
constexpr guint audioOnlyFlags = 0x2;

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
        const QByteArray description =
            qEnvironmentVariable("CASDAP_AUDIO_SINK", QStringLiteral("pulsesink")).toUtf8();
        GError *err = nullptr;
        GstElement *sink = gst_parse_bin_from_description(description.constData(), TRUE, &err);
        if (!sink) {
            qWarning() << "casdap player: audio sink" << description
                       << (err ? err->message : "failed");
        } else {
            g_object_set(m_playbin, "audio-sink", sink, nullptr);
        }
        g_clear_error(&err);
        m_bus = gst_element_get_bus(m_playbin);
        m_aboutToFinishId = g_signal_connect(m_playbin, "about-to-finish",
                                             G_CALLBACK(&Player::aboutToFinish), this);
    }

    m_busTimer.setInterval(busIntervalMs);
    connect(&m_busTimer, &QTimer::timeout, this, &Player::pollBus);
    m_positionTimer.setInterval(positionIntervalMs);
    connect(&m_positionTimer, &QTimer::timeout, this, &Player::pollPosition);

    connect(m_queue, &Queue::countChanged, this, &Player::syncChain);
    connect(m_queue, &Queue::indexChanged, this, &Player::syncChain);
    connect(m_queue, &Queue::shuffleChanged, this, &Player::syncChain);
    connect(m_queue, &Queue::repeatChanged, this, &Player::syncChain);
    connect(m_queue, &QAbstractItemModel::rowsMoved, this, &Player::syncChain);
    connect(m_queue, &Queue::countChanged, this, &Player::currentChanged);
    connect(m_queue, &Queue::indexChanged, this, &Player::currentChanged);
}

Player::~Player()
{
    m_busTimer.stop();
    m_positionTimer.stop();
    if (m_playbin) {
        if (m_aboutToFinishId)
            g_signal_handler_disconnect(m_playbin, m_aboutToFinishId);
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    }
    if (m_bus) {
        gst_bus_set_flushing(m_bus, TRUE);
        gst_object_unref(m_bus);
    }
    if (m_playbin)
        gst_object_unref(m_playbin);
}

Player::Status Player::status() const
{
    return m_status;
}

qint64 Player::position() const
{
    return m_position;
}

qint64 Player::duration() const
{
    return m_duration;
}

QVariantMap Player::current() const
{
    QVariantMap map = m_queue->get(m_queue->currentIndex());
    if (m_library && !map.isEmpty())
        map.insert(QStringLiteral("rating"),
                   m_library->rating(map.value(QStringLiteral("id")).toLongLong()));
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

void Player::aboutToFinish(GstElement *element, gpointer data)
{
    auto *self = static_cast<Player *>(data);
    QString path;
    {
        QMutexLocker lock(&self->m_chainMutex);
        Chain &chain = self->m_chain;
        const int next = Queue::nextOf(chain.last, static_cast<int>(chain.paths.size()),
                                       chain.order, chain.shuffle, chain.repeat, false);
        if (next < 0 || next >= chain.paths.size())
            return;
        path = chain.paths.at(next);
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
    QMutexLocker lock(&m_chainMutex);
    m_chain.paths.clear();
    const QList<QueueEntry> entries = m_queue->entries();
    m_chain.paths.reserve(entries.size());
    for (const QueueEntry &entry : entries)
        m_chain.paths.append(entry.path);
    m_chain.order = m_queue->order();
    m_chain.shuffle = m_queue->shuffle();
    m_chain.repeat = m_queue->repeat();
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
    m_busTimer.stop();
    m_positionTimer.stop();
    if (m_playbin)
        gst_element_set_state(m_playbin, GST_STATE_NULL);
    if (m_bus) {
        while (GstMessage *message = gst_bus_pop(m_bus))
            gst_message_unref(message);
    }
    QMutexLocker lock(&m_chainMutex);
    m_chain.pending.clear();
    m_prerolled = false;
    m_pendingSeek = -1;
}

void Player::startAt(int index, GstState target)
{
    if (!m_playbin || index < 0 || index >= m_queue->count()) {
        stop();
        return;
    }
    setNull();
    m_queue->setCurrentIndex(index);
    {
        QMutexLocker lock(&m_chainMutex);
        m_chain.last = index;
        m_chain.pending = {index};
    }
    m_historyCounted = true;
    setPosition(0);
    setUri(m_queue->entry(index).path);
    gst_element_set_state(m_playbin, target);
    m_busTimer.start();
    setStatus(target == GST_STATE_PLAYING ? Playing : Paused);
    emit currentChanged();
}

void Player::play(const QList<QueueEntry> &entries, int start)
{
    m_queue->set(entries, start);
    startAt(m_queue->currentIndex(), GST_STATE_PLAYING);
}

void Player::restore(const QList<QueueEntry> &entries, int index, qint64 positionMs)
{
    m_queue->set(entries, index);
    if (m_queue->count() == 0)
        return;
    startAt(m_queue->currentIndex(), GST_STATE_PAUSED);
    if (positionMs > 0) {
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
    if (!m_playbin)
        return;
    switch (m_status) {
    case Playing:
        pause();
        break;
    case Paused:
        gst_element_set_state(m_playbin, GST_STATE_PLAYING);
        setStatus(Playing);
        if (!m_historyCounted)
            addHistoryNow();
        break;
    case Stopped:
        if (m_queue->count() > 0)
            startAt(qMax(0, m_queue->currentIndex()), GST_STATE_PLAYING);
        break;
    }
}

void Player::pause()
{
    if (!m_playbin || m_status != Playing)
        return;
    gst_element_set_state(m_playbin, GST_STATE_PAUSED);
    setStatus(Paused);
}

void Player::stop()
{
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
    if (m_position > restartThresholdMs && m_status != Stopped) {
        seek(0);
        return;
    }
    const int index = m_queue->previousIndex(true);
    startAt(index < 0 ? m_queue->currentIndex() : index, GST_STATE_PLAYING);
}

void Player::doSeek(qint64 ms)
{
    const auto flags = static_cast<GstSeekFlags>(GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_KEY_UNIT);
    if (!gst_element_seek_simple(m_playbin, GST_FORMAT_TIME, flags, ms * GST_MSECOND))
        qWarning() << "casdap player: seek failed" << ms;
    setPosition(ms);
}

void Player::seek(qint64 ms)
{
    if (!m_playbin || m_status == Stopped)
        return;
    const qint64 target = qMax<qint64>(0, ms);
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
    if (status == Playing)
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
    if (!m_playbin || m_status != Playing)
        return;
    gint64 position = 0;
    if (gst_element_query_position(m_playbin, GST_FORMAT_TIME, &position) && position >= 0)
        setPosition(position / GST_MSECOND);
}

void Player::handleStreamStart()
{
    int index = -1;
    {
        QMutexLocker lock(&m_chainMutex);
        if (!m_chain.pending.isEmpty())
            index = m_chain.pending.takeFirst();
    }
    if (index >= 0 && index < m_queue->count())
        m_queue->setCurrentIndex(index);
    m_errorStreak = 0;
    m_historyCounted = false;
    setPosition(0);
    updateDuration();
    emit currentChanged();
    emit trackStarted(m_queue->currentIndex());
    if (m_status == Playing)
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
    if (m_pendingSeek >= 0) {
        const qint64 target = m_pendingSeek;
        m_pendingSeek = -1;
        doSeek(target);
    }
    updateDuration();
    pollPosition();
}

void Player::handleError(GstMessage *message)
{
    GError *err = nullptr;
    gchar *debug = nullptr;
    gst_message_parse_error(message, &err, &debug);
    const QString text = err ? QString::fromUtf8(err->message) : QStringLiteral("playback error");
    qWarning() << "casdap player:" << text << (debug ? debug : "");
    g_clear_error(&err);
    g_free(debug);
    emit error(text);

    ++m_errorStreak;
    const int current = m_queue->currentIndex();
    const int index = m_queue->nextIndex(true);
    if (index < 0 || index == current || m_errorStreak >= m_queue->count()) {
        stop();
        return;
    }
    startAt(index, GST_STATE_PLAYING);
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
