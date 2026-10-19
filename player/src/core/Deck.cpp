#include "Deck.h"

#include <QDebug>
#include <QFile>

#include <utility>

namespace microdap {

namespace {

constexpr const char *pwDeck = "concat name=join ! audioconvert name=entry "
                               "! capsfilter caps=audio/x-raw,format=F32LE ! volume name=volume ! identity name=cueprobe";
constexpr const char *plainDeck = "concat name=join ! identity name=cueprobe";
constexpr const char *pcmCaps = "audio/x-raw(ANY)";

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

Deck::Deck(PlayerPort::OutputChain chain)
{
    const bool pw = chain == PlayerPort::OutputChain::Pw;
    GError *err = nullptr;
    GstElement *bin = gst_parse_bin_from_description(pw ? pwDeck : plainDeck, FALSE, &err);
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
    m_entry = pw ? child(m_bin, "entry") : m_cueProbe;
    m_volume = pw ? child(m_bin, "volume") : nullptr;
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

void Deck::start(const QString &path)
{
    QList<Item> items;
    {
        QMutexLocker lock(&m_lock);
        items.swap(m_items);
        m_open = false;
    }
    for (const Item &item : std::as_const(items))
        drop(item);
    QMutexLocker lock(&m_lock);
    m_open = append(path);
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

bool Deck::append(const QString &path)
{
    if (!m_bin)
        return false;
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
    GstCaps *caps = gst_caps_from_string(pcmCaps);
    g_object_set(decoder, "uri", uri, "caps", caps, "async-handling", TRUE, nullptr);
    gst_caps_unref(caps);
    g_free(uri);
    GstPad *pad = gst_element_request_pad_simple(m_join, "sink_%u");
    g_signal_connect_data(decoder, "pad-added", G_CALLBACK(&Deck::padAdded), gst_object_ref(pad),
                          [](gpointer data, GClosure *) { gst_object_unref(data); }, GConnectFlags(0));
    g_signal_connect(decoder, "drained", G_CALLBACK(&Deck::drained), this);
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
    const QString path = self->m_next();
    if (!path.isEmpty())
        self->append(path);
}

void Deck::activePad(GObject *, GParamSpec *, gpointer data)
{
    auto *self = static_cast<Deck *>(data);
    gst_element_post_message(self->m_bin,
                             gst_message_new_element(GST_OBJECT(self->m_bin), gst_structure_new_empty(switched)));
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
