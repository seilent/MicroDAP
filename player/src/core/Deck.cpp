#include "Deck.h"

#include <QByteArray>
#include <QDebug>
#include <QFile>
#include <QString>

#include <utility>

namespace microdap {

namespace {

constexpr const char *pcmJoin = "concat name=join ! ";
constexpr const char *dsdJoin = "concat name=join ! dsdconvert name=entry "
                                "! capsfilter caps=audio/x-dsd,format=DSDU8,layout=interleaved,reversed-bytes=false "
                                "! avdec_dsd_msbf ! audioresample name=resample ! capsfilter name=dsdrate ! ";
constexpr const char *pwStage = "audioconvert name=convert ! capsfilter caps=audio/x-raw,format=F32LE "
                                "! volume name=volume ! ";
constexpr const char *probeStage = "identity name=cueprobe";
constexpr const char *pcmCaps = "audio/x-raw(ANY)";
constexpr const char *dsdCaps = "audio/x-raw(ANY);audio/x-dsd";
constexpr int dsd64DecodedRate = 352800;
constexpr int dsd64PcmRate = 88200;
constexpr int dsdPcmRate = 176400;

void release(GstElement *bin)
{
    if (!bin)
        return;
    if (GstObject *parent = gst_object_get_parent(GST_OBJECT(bin))) {
        gst_bin_remove(GST_BIN(parent), bin);
        gst_object_unref(parent);
    }
    gst_object_unref(bin);
}

GstElement *child(GstElement *bin, const char *name)
{
    GstElement *element = gst_bin_get_by_name(GST_BIN(bin), name);
    if (element)
        gst_object_unref(element);
    return element;
}

}

Deck::Deck(PlayerPort::OutputChain chain, bool dsd)
{
    const bool pw = chain == PlayerPort::OutputChain::Pw;
    QByteArray description = dsd ? dsdJoin : pcmJoin;
    if (pw)
        description += pwStage;
    description += probeStage;
    GError *err = nullptr;
    GstElement *bin = gst_parse_bin_from_description(description.constData(), FALSE, &err);
    if (!bin) {
        qWarning() << "microdap player: deck" << (err ? err->message : "failed");
        g_clear_error(&err);
        return;
    }
    g_clear_error(&err);
    m_bin = GST_ELEMENT(gst_object_ref_sink(bin));
    gst_object_set_name(GST_OBJECT(m_bin), "deck");
    m_join = child(m_bin, "join");
    m_cueProbe = child(m_bin, "cueprobe");
    m_entry = dsd ? child(m_bin, "entry") : pw ? child(m_bin, "convert") : m_cueProbe;
    m_volume = pw ? child(m_bin, "volume") : nullptr;
    m_pw = pw;
    m_dsd = dsd;
    if (dsd) {
        m_dsdRate = child(m_bin, "dsdrate");
        GstPad *sink = gst_element_get_static_pad(child(m_bin, "resample"), "sink");
        gst_pad_add_probe(sink,
                          static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM | GST_PAD_PROBE_TYPE_BUFFER
                                                       | GST_PAD_PROBE_TYPE_BUFFER_LIST),
                          &Deck::capsProbe, this, nullptr);
        gst_object_unref(sink);
    }
    if (m_volume) {
        GstPad *sink = gst_element_get_static_pad(m_volume, "sink");
        gst_pad_add_probe(sink, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Deck::gainProbe, this, nullptr);
        gst_object_unref(sink);
    }
    GstPad *src = gst_element_get_static_pad(m_cueProbe, "src");
    gst_element_add_pad(m_bin, gst_ghost_pad_new("src", src));
    gst_object_unref(src);
    g_signal_connect(m_join, "notify::active-pad", G_CALLBACK(&Deck::activePad), this);
}

Deck::~Deck()
{
    for (const Item &item : std::as_const(m_items)) {
        gst_object_unref(item.pad);
        gst_object_unref(item.decoder);
    }
    release(m_bin);
}

bool Deck::available()
{
    for (const char *name : {"uridecodebin", "concat"}) {
        GstElementFactory *factory = gst_element_factory_find(name);
        if (!factory)
            return false;
        gst_object_unref(factory);
    }
    return true;
}

void Deck::setNext(Next next)
{
    QMutexLocker lock(&m_lock);
    m_next = std::move(next);
}

void Deck::setGain(Gain gain)
{
    QMutexLocker lock(&m_gainLock);
    m_gain = std::move(gain);
}

void Deck::start(const QueueEntry &entry)
{
    QList<Item> items;
    {
        QMutexLocker lock(&m_lock);
        items.swap(m_items);
        m_open = false;
    }
    for (const Item &item : std::as_const(items))
        drop(item);
    {
        QMutexLocker gainLock(&m_gainLock);
        m_starts.clear();
        m_playing.reset();
    }
    QMutexLocker lock(&m_lock);
    m_open = append(entry);
    if (!m_open && m_bin) {
        GError *err = g_error_new_literal(GST_RESOURCE_ERROR, GST_RESOURCE_ERROR_NOT_FOUND, "bad path");
        gst_element_post_message(m_bin, gst_message_new_error(GST_OBJECT(m_bin), err, nullptr));
        g_error_free(err);
    }
}

void Deck::close()
{
    QMutexLocker lock(&m_lock);
    m_open = false;
}

void Deck::reap()
{
    QList<Item> done;
    {
        QMutexLocker lock(&m_lock);
        GstPad *active = nullptr;
        g_object_get(m_join, "active-pad", &active, nullptr);
        if (!active)
            return;
        int at = 0;
        while (at < m_items.size() && m_items.at(at).pad != active)
            ++at;
        gst_object_unref(active);
        if (at == m_items.size())
            return;
        done = m_items.mid(0, at);
        m_items.remove(0, at);
    }
    for (const Item &item : std::as_const(done))
        drop(item);
}

void Deck::regain()
{
    QMutexLocker lock(&m_gainLock);
    if (m_playing)
        applyGain(*m_playing);
}

void Deck::applyGain(const QueueEntry &entry)
{
    if (m_volume)
        g_object_set(m_volume, "volume", m_gain && !m_dsd ? m_gain(entry) : 1.0, nullptr);
}

QString Deck::streamOf(GstPadProbeInfo *info)
{
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) != GST_EVENT_STREAM_START)
        return {};
    const gchar *id = nullptr;
    gst_event_parse_stream_start(event, &id);
    return id ? QString::fromUtf8(id) : QStringLiteral("-");
}

GstPadProbeReturn Deck::itemProbe(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    const QString id = streamOf(info);
    if (id.isEmpty())
        return GST_PAD_PROBE_OK;
    auto *start = static_cast<Start *>(data);
    QMutexLocker lock(&start->deck->m_gainLock);
    if (!std::exchange(start->seen, true))
        start->deck->m_starts[id].append(start->entry);
    return GST_PAD_PROBE_OK;
}

GstPadProbeReturn Deck::gainProbe(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    const QString id = streamOf(info);
    if (id.isEmpty())
        return GST_PAD_PROBE_OK;
    auto *self = static_cast<Deck *>(data);
    QMutexLocker lock(&self->m_gainLock);
    const auto it = self->m_starts.find(id);
    if (it == self->m_starts.end())
        return GST_PAD_PROBE_OK;
    self->m_playing = it->takeFirst();
    if (it->isEmpty())
        self->m_starts.erase(it);
    self->applyGain(*self->m_playing);
    return GST_PAD_PROBE_OK;
}

bool Deck::append(const QueueEntry &entry)
{
    if (!m_bin)
        return false;
    const QString &path = entry.path;
    gchar *uri = gst_filename_to_uri(QFile::encodeName(path).constData(), nullptr);
    if (!uri) {
        qWarning() << "microdap player: bad path" << path;
        return false;
    }
    GstElement *decoder = gst_element_factory_make("uridecodebin", nullptr);
    if (!decoder) {
        g_free(uri);
        return false;
    }
    gst_object_ref_sink(decoder);
    GstCaps *caps = gst_caps_from_string(m_dsd ? dsdCaps : pcmCaps);
    g_object_set(decoder, "uri", uri, "caps", caps, "async-handling", TRUE, nullptr);
    gst_caps_unref(caps);
    g_free(uri);
    GstPad *pad = gst_element_request_pad_simple(m_join, "sink_%u");
    g_signal_connect_data(decoder, "pad-added", G_CALLBACK(&Deck::padAdded), gst_object_ref(pad),
                          [](gpointer data, GClosure *) { gst_object_unref(data); }, GConnectFlags(0));
    g_signal_connect(decoder, "drained", G_CALLBACK(&Deck::drained), this);
    if (m_volume)
        gst_pad_add_probe(pad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Deck::itemProbe, new Start{this, entry},
                          [](gpointer data) { delete static_cast<Start *>(data); });
    gst_bin_add(GST_BIN(m_bin), decoder);
    m_items.append({decoder, pad});
    gst_element_sync_state_with_parent(decoder);
    return true;
}

void Deck::drop(const Item &item)
{
    gst_element_set_state(item.decoder, GST_STATE_NULL);
    gst_element_release_request_pad(m_join, item.pad);
    gst_object_unref(item.pad);
    gst_bin_remove(GST_BIN(m_bin), item.decoder);
    gst_object_unref(item.decoder);
}

void Deck::drained(GstElement *decoder, gpointer data)
{
    auto *self = static_cast<Deck *>(data);
    QMutexLocker lock(&self->m_lock);
    if (!self->m_open || !self->m_next || self->m_items.isEmpty() || self->m_items.constLast().decoder != decoder)
        return;
    const QueueEntry next = self->m_next();
    if (!next.path.isEmpty())
        self->append(next);
}

void Deck::activePad(GObject *, GParamSpec *, gpointer data)
{
    auto *self = static_cast<Deck *>(data);
    gst_element_post_message(self->m_bin,
                             gst_message_new_element(GST_OBJECT(self->m_bin), gst_structure_new_empty(switched)));
}

int Deck::dsdPcmRateFor(int decodedRate)
{
    return decodedRate <= dsd64DecodedRate ? dsd64PcmRate : dsdPcmRate;
}

void Deck::accept()
{
    m_rate = Rate::Accepted;
    gst_element_post_message(m_bin, gst_message_new_element(GST_OBJECT(m_bin), gst_structure_new_empty(dsdAccepted)));
}

GstPadProbeReturn Deck::capsProbe(GstPad *, GstPadProbeInfo *info, gpointer data)
{
    auto *self = static_cast<Deck *>(data);
    if (GST_PAD_PROBE_INFO_TYPE(info) & (GST_PAD_PROBE_TYPE_BUFFER | GST_PAD_PROBE_TYPE_BUFFER_LIST)) {
        if (self->m_rate == Rate::Unknown)
            self->accept();
        return GST_PAD_PROBE_OK;
    }
    GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
    if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START)
        self->m_rate = Rate::Unknown;
    if (GST_EVENT_TYPE(event) != GST_EVENT_CAPS)
        return GST_PAD_PROBE_OK;
    GstCaps *caps = nullptr;
    gst_event_parse_caps(event, &caps);
    gint rate = 0;
    if (!caps || gst_caps_get_size(caps) == 0
        || !gst_structure_get_int(gst_caps_get_structure(caps, 0), "rate", &rate))
        return GST_PAD_PROBE_OK;
    if (self->m_pw && rate > dsd64DecodedRate) {
        self->m_rate = Rate::Refused;
        GError *err = g_error_new_literal(GST_STREAM_ERROR, GST_STREAM_ERROR_FORMAT, dsdRefused);
        gst_element_post_message(self->m_bin, gst_message_new_error(GST_OBJECT(self->m_bin), err, nullptr));
        g_error_free(err);
        return GST_PAD_PROBE_OK;
    }
    GstCaps *filter = gst_caps_new_simple("audio/x-raw", "rate", G_TYPE_INT, dsdPcmRateFor(rate), nullptr);
    g_object_set(self->m_dsdRate, "caps", filter, nullptr);
    gst_caps_unref(filter);
    self->accept();
    return GST_PAD_PROBE_OK;
}

GstElement *Deck::element() const
{
    return m_bin;
}

GstElement *Deck::volume() const
{
    return m_volume;
}

GstElement *Deck::cueProbe() const
{
    return m_cueProbe;
}

GstPad *Deck::decodedPad() const
{
    return m_entry ? gst_element_get_static_pad(m_entry, "sink") : nullptr;
}

bool Deck::rateAccepted() const
{
    return !m_pw || !m_dsdRate || m_rate == Rate::Accepted;
}

void Deck::padAdded(GstElement *, GstPad *pad, gpointer data)
{
    auto *sink = static_cast<GstPad *>(data);
    if (!gst_pad_is_linked(sink) && gst_pad_link(pad, sink) != GST_PAD_LINK_OK)
        qWarning() << "microdap player: deck pad not linked" << GST_PAD_NAME(pad);
}

Tail::Tail(GstElement *filter, GstElement *sink)
{
    m_bin = GST_ELEMENT(gst_object_ref_sink(gst_bin_new("tail")));
    GstElement *convert = gst_element_factory_make("audioconvert", "convert");
    GstElement *resample = gst_element_factory_make("audioresample", "resample");
    GstElement *out = sink ? sink : gst_element_factory_make("autoaudiosink", nullptr);
    if (filter) {
        gst_object_set_name(GST_OBJECT(filter), "filter");
        gst_bin_add(GST_BIN(m_bin), filter);
    }
    if (sink)
        gst_object_set_name(GST_OBJECT(sink), "sink");
    for (GstElement *element : {convert, resample, out}) {
        if (element)
            gst_bin_add(GST_BIN(m_bin), element);
    }
    if (!convert || !resample || !out) {
        qWarning() << "microdap player: tail elements not available";
        return;
    }
    gst_element_link_many(convert, resample, out, nullptr);
    GstElement *first = convert;
    if (filter && gst_element_link(filter, convert))
        first = filter;
    GstPad *target = gst_element_get_static_pad(first, "sink");
    gst_element_add_pad(m_bin, gst_ghost_pad_new("sink", target));
    gst_object_unref(target);
}

Tail::~Tail()
{
    release(m_bin);
}

GstElement *Tail::element() const
{
    return m_bin;
}

}
