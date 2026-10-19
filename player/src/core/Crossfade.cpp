#include "Crossfade.h"

#include <QDebug>
#include <QMutex>
#include <QMutexLocker>

#include <gst/controller/gstdirectcontrolbinding.h>
#include <gst/controller/gstinterpolationcontrolsource.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace microdap {

namespace {

constexpr int fadePoints = 128;
constexpr const char *unknownAlbum = "unknown album";
constexpr const char *mixerDescription =
    "audiomixer name=mix start-time-selection=first volume name=fadeA ! mix.sink_0 volume name=fadeB ! mix.sink_1";

GstPad *padOf(GstElement *element, const char *name)
{
    return element ? gst_element_get_static_pad(element, name) : nullptr;
}

GstElement *childOf(GstElement *bin, const char *name)
{
    GstElement *element = gst_bin_get_by_name(GST_BIN(bin), name);
    if (element)
        gst_object_unref(element);
    return element;
}

void ghost(GstElement *bin, const char *element, const char *pad, const char *name)
{
    GstPad *target = padOf(childOf(bin, element), pad);
    gst_element_add_pad(bin, gst_ghost_pad_new(name, target));
    gst_object_unref(target);
}

void unref(GstPad *&pad)
{
    if (pad)
        gst_object_unref(pad);
    pad = nullptr;
}

void unlinkPeer(GstPad *pad)
{
    if (!pad)
        return;
    GstPad *peer = gst_pad_get_peer(pad);
    if (!peer)
        return;
    if (GST_PAD_IS_SRC(pad))
        gst_pad_unlink(pad, peer);
    else
        gst_pad_unlink(peer, pad);
    gst_object_unref(peer);
}

bool sameShape(GstPad *a, GstPad *b)
{
    GstCaps *ca = gst_pad_get_current_caps(a);
    GstCaps *cb = gst_pad_get_current_caps(b);
    bool same = false;
    if (ca && cb && gst_caps_get_size(ca) > 0 && gst_caps_get_size(cb) > 0) {
        const GstStructure *sa = gst_caps_get_structure(ca, 0);
        const GstStructure *sb = gst_caps_get_structure(cb, 0);
        gint rateA = 0;
        gint rateB = 0;
        gint channelsA = 0;
        gint channelsB = 0;
        same = gst_structure_has_name(sa, "audio/x-raw") && gst_structure_has_name(sb, "audio/x-raw")
               && gst_structure_get_int(sa, "rate", &rateA) && gst_structure_get_int(sb, "rate", &rateB)
               && gst_structure_get_int(sa, "channels", &channelsA)
               && gst_structure_get_int(sb, "channels", &channelsB) && rateA == rateB && channelsA == channelsB;
    }
    if (ca)
        gst_caps_unref(ca);
    if (cb)
        gst_caps_unref(cb);
    return same;
}

void fade(GstElement *fader, GstClockTime start, GstClockTime length, bool outgoing)
{
    GstControlSource *source = gst_interpolation_control_source_new();
    g_object_set(source, "mode", GST_INTERPOLATION_MODE_LINEAR, nullptr);
    auto *timed = GST_TIMED_VALUE_CONTROL_SOURCE(source);
    gst_timed_value_control_source_set(timed, 0, crossfadeWeight(0.0, outgoing));
    for (int i = 0; i <= fadePoints; ++i)
        gst_timed_value_control_source_set(timed, start + gst_util_uint64_scale(length, i, fadePoints),
                                           crossfadeWeight(double(i) / fadePoints, outgoing));
    gst_object_add_control_binding(GST_OBJECT(fader),
                                   gst_direct_control_binding_new_absolute(GST_OBJECT(fader), "volume", source));
    gst_object_unref(source);
}

}

CrossfadeSchedule crossfadeSchedule(GstClockTime duration, GstClockTime length, GstClockTime lead)
{
    CrossfadeSchedule schedule;
    if (!GST_CLOCK_TIME_IS_VALID(duration) || length == 0 || duration < 2 * length)
        return schedule;
    schedule.start = duration - length;
    schedule.prepare = schedule.start > lead ? schedule.start - lead : 0;
    return schedule;
}

double crossfadeWeight(double progress, bool outgoing)
{
    const double t = std::clamp(progress, 0.0, 1.0) * std::numbers::pi / 2.0;
    return outgoing ? std::cos(t) : std::sin(t);
}

bool albumContinues(const QueueEntry &from, const QueueEntry &to)
{
    return from.albumId > 0 && from.albumId == to.albumId && !from.album.isEmpty()
           && from.album != QLatin1StringView(unknownAlbum) && from.discNo == to.discNo && from.trackNo > 0
           && to.trackNo == from.trackNo + 1;
}

bool crossfadeFits(qint64 fromMs, qint64 toMs, qint64 lengthMs)
{
    return lengthMs > 0 && fromMs >= 2 * lengthMs && toMs >= 2 * lengthMs;
}

struct Crossfade::Shared
{
    enum class State { Planned, Preparing, Ready, Mixing, Draining, Finished, Refused, Cancelled };

    QMutex lock;
    State state = State::Planned;
    GstElement *pipeline = nullptr;
    GstElement *mixer = nullptr;
    GstPad *outSrc = nullptr;
    GstPad *inSrc = nullptr;
    GstPad *tailSink = nullptr;
    GstPad *mixOut = nullptr;
    GstPad *mixIn = nullptr;
    GstPad *mixSrc = nullptr;
    GstSegment segment;
    CrossfadeSchedule schedule;
    GstClockTime length = 0;
    GstClockTime offset = GST_CLOCK_TIME_NONE;
    int generation = 0;
    bool first = true;
    bool asked = false;
    bool waiting = false;
    bool eosSent = false;
    gulong watchProbe = 0;
    gulong eventProbe = 0;
    gulong firstProbe = 0;
    gulong blockProbe = 0;
    gulong eosProbe = 0;

    ~Shared()
    {
        for (GstPad **pad : {&outSrc, &inSrc, &tailSink, &mixOut, &mixIn, &mixSrc})
            unref(*pad);
        if (mixer)
            gst_object_unref(mixer);
        if (pipeline)
            gst_object_unref(pipeline);
    }

    void post(const char *name)
    {
        GstStructure *structure = gst_structure_new(name, "generation", G_TYPE_INT, generation, nullptr);
        gst_element_post_message(pipeline, gst_message_new_element(GST_OBJECT(pipeline), structure));
    }

    void removeProbe(GstPad *pad, gulong &id)
    {
        if (pad && id)
            gst_pad_remove_probe(pad, id);
        id = 0;
    }

    bool mixing() const { return state == State::Mixing || state == State::Draining || state == State::Finished; }

    void removeAll()
    {
        removeProbe(outSrc, watchProbe);
        removeProbe(outSrc, eventProbe);
        removeProbe(inSrc, firstProbe);
        removeProbe(inSrc, blockProbe);
        removeProbe(mixSrc, eosProbe);
    }

    void relinkIn()
    {
        gst_pad_unlink(outSrc, tailSink);
        if (gst_pad_link(mixSrc, tailSink) != GST_PAD_LINK_OK || gst_pad_link(outSrc, mixOut) != GST_PAD_LINK_OK)
            qWarning() << "microdap player: crossfade relink in failed";
        state = State::Mixing;
    }

    void refuse()
    {
        state = State::Refused;
        removeProbe(outSrc, watchProbe);
        post(refused);
    }

    static void drop(gpointer data) { delete static_cast<std::shared_ptr<Shared> *>(data); }
    static Shared &of(gpointer data) { return **static_cast<std::shared_ptr<Shared> *>(data); }

    static gpointer hold(const std::shared_ptr<Shared> &self) { return new std::shared_ptr<Shared>(self); }
    static gpointer again(gpointer data) { return hold(*static_cast<std::shared_ptr<Shared> *>(data)); }

    static GstPadProbeReturn watch(GstPad *, GstPadProbeInfo *info, gpointer data)
    {
        Shared &self = of(data);
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        QMutexLocker lock(&self.lock);
        if (self.state != State::Planned && self.state != State::Preparing && self.state != State::Ready) {
            self.watchProbe = 0;
            return GST_PAD_PROBE_REMOVE;
        }
        if (!buffer || !GST_BUFFER_PTS_IS_VALID(buffer))
            return GST_PAD_PROBE_PASS;
        const guint64 begin = gst_segment_to_stream_time(&self.segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
        if (begin == guint64(-1))
            return GST_PAD_PROBE_PASS;
        const guint64 end = begin + (GST_BUFFER_DURATION_IS_VALID(buffer) ? GST_BUFFER_DURATION(buffer) : 0);
        if (std::exchange(self.first, false) && begin >= self.schedule.start && self.state == State::Planned) {
            self.state = State::Refused;
            self.watchProbe = 0;
            self.post(refused);
            return GST_PAD_PROBE_REMOVE;
        }
        if (self.state == State::Planned && !self.asked && end > self.schedule.prepare) {
            self.asked = true;
            self.post(prepareDue);
        }
        if (end <= self.schedule.start)
            return GST_PAD_PROBE_PASS;
        if (self.state == State::Ready) {
            self.relinkIn();
            self.watchProbe = 0;
            return GST_PAD_PROBE_REMOVE;
        }
        self.waiting = true;
        return GST_PAD_PROBE_OK;
    }

    static GstPadProbeReturn events(GstPad *, GstPadProbeInfo *info, gpointer data)
    {
        Shared &self = of(data);
        GstEvent *event = GST_PAD_PROBE_INFO_EVENT(info);
        if (GST_EVENT_TYPE(event) == GST_EVENT_SEGMENT) {
            const GstSegment *segment = nullptr;
            gst_event_parse_segment(event, &segment);
            QMutexLocker lock(&self.lock);
            gst_segment_copy_into(segment, &self.segment);
            self.first = true;
            return GST_PAD_PROBE_OK;
        }
        if (GST_EVENT_TYPE(event) == GST_EVENT_STREAM_START) {
            QMutexLocker lock(&self.lock);
            if (self.state == State::Planned)
                self.refuse();
            return GST_PAD_PROBE_OK;
        }
        if (GST_EVENT_TYPE(event) != GST_EVENT_EOS)
            return GST_PAD_PROBE_OK;
        QMutexLocker lock(&self.lock);
        if (self.state == State::Planned) {
            self.refuse();
            return GST_PAD_PROBE_OK;
        }
        if (self.state == State::Preparing || self.state == State::Ready) {
            self.state = State::Cancelled;
            self.removeProbe(self.outSrc, self.watchProbe);
            self.removeProbe(self.inSrc, self.firstProbe);
            self.post(early);
            return GST_PAD_PROBE_DROP;
        }
        if (self.state != State::Mixing)
            return GST_PAD_PROBE_OK;
        self.state = State::Draining;
        self.blockProbe = gst_pad_add_probe(self.inSrc, GST_PAD_PROBE_TYPE_BLOCK_DOWNSTREAM, &Shared::block,
                                            again(data), &Shared::drop);
        self.eventProbe = 0;
        return GST_PAD_PROBE_REMOVE;
    }

    static GstPadProbeReturn firstBuffer(GstPad *, GstPadProbeInfo *, gpointer data)
    {
        Shared &self = of(data);
        QMutexLocker lock(&self.lock);
        self.firstProbe = 0;
        if (self.state != State::Preparing)
            return GST_PAD_PROBE_REMOVE;
        if (!sameShape(self.outSrc, self.inSrc)) {
            self.refuse();
            return GST_PAD_PROBE_REMOVE;
        }
        self.state = State::Ready;
        if (self.waiting) {
            self.relinkIn();
            self.removeProbe(self.outSrc, self.watchProbe);
        }
        return GST_PAD_PROBE_REMOVE;
    }

    static GstPadProbeReturn block(GstPad *pad, GstPadProbeInfo *info, gpointer data)
    {
        if (!(GST_PAD_PROBE_INFO_TYPE(info) & GST_PAD_PROBE_TYPE_BUFFER))
            return GST_PAD_PROBE_PASS;
        Shared &self = of(data);
        GstBuffer *buffer = GST_PAD_PROBE_INFO_BUFFER(info);
        guint64 begin = guint64(-1);
        if (GstEvent *sticky = buffer && GST_BUFFER_PTS_IS_VALID(buffer)
                                   ? gst_pad_get_sticky_event(pad, GST_EVENT_SEGMENT, 0)
                                   : nullptr) {
            const GstSegment *segment = nullptr;
            gst_event_parse_segment(sticky, &segment);
            begin = gst_segment_to_stream_time(segment, GST_FORMAT_TIME, GST_BUFFER_PTS(buffer));
            gst_event_unref(sticky);
        }
        GstPad *peer = nullptr;
        {
            QMutexLocker lock(&self.lock);
            if (self.state == State::Draining && !self.eosSent && begin != guint64(-1) && begin < self.length)
                return GST_PAD_PROBE_PASS;
            if (self.state != State::Draining || self.eosSent)
                return GST_PAD_PROBE_OK;
            self.eosSent = true;
            self.eosProbe = gst_pad_add_probe(self.mixSrc, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Shared::drained,
                                              again(data), &Shared::drop);
            peer = gst_pad_get_peer(pad);
        }
        if (peer) {
            gst_pad_send_event(peer, gst_event_new_eos());
            gst_object_unref(peer);
        }
        return GST_PAD_PROBE_OK;
    }

    static GstPadProbeReturn drained(GstPad *, GstPadProbeInfo *info, gpointer data)
    {
        if (GST_EVENT_TYPE(GST_PAD_PROBE_INFO_EVENT(info)) != GST_EVENT_EOS)
            return GST_PAD_PROBE_OK;
        Shared &self = of(data);
        QMutexLocker lock(&self.lock);
        if (self.state != State::Draining)
            return GST_PAD_PROBE_OK;
        gst_pad_unlink(self.inSrc, self.mixIn);
        gst_pad_unlink(self.mixSrc, self.tailSink);
        if (gst_pad_link(self.inSrc, self.tailSink) != GST_PAD_LINK_OK)
            qWarning() << "microdap player: crossfade relink out failed";
        self.eosProbe = 0;
        self.state = State::Finished;
        self.post(finished);
        self.removeProbe(self.inSrc, self.blockProbe);
        return GST_PAD_PROBE_DROP;
    }
};

Crossfade::Crossfade(GstElement *pipeline, GstElement *tail, GstElement *out, const CrossfadeSchedule &schedule,
                     GstClockTime length, int generation)
    : m(std::make_shared<Shared>())
{
    m->pipeline = GST_ELEMENT(gst_object_ref(pipeline));
    m->tailSink = padOf(tail, "sink");
    m->outSrc = padOf(out, "src");
    m->schedule = schedule;
    m->length = length;
    m->generation = generation;
    gst_segment_init(&m->segment, GST_FORMAT_TIME);
    if (GstEvent *sticky = gst_pad_get_sticky_event(m->outSrc, GST_EVENT_SEGMENT, 0)) {
        const GstSegment *segment = nullptr;
        gst_event_parse_segment(sticky, &segment);
        gst_segment_copy_into(segment, &m->segment);
        gst_event_unref(sticky);
    }
    m->eventProbe = gst_pad_add_probe(m->outSrc, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM, &Shared::events, Shared::hold(m),
                                      &Shared::drop);
    m->watchProbe = gst_pad_add_probe(
        m->outSrc, static_cast<GstPadProbeType>(GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER), &Shared::watch,
        Shared::hold(m), &Shared::drop);
}

Crossfade::~Crossfade()
{
    cancel();
}

bool Crossfade::available()
{
    for (const char *name : {"audiomixer", "volume"}) {
        GstElementFactory *factory = gst_element_factory_find(name);
        if (!factory)
            return false;
        gst_object_unref(factory);
    }
    return true;
}

int Crossfade::generationOf(GstMessage *message)
{
    gint generation = -1;
    if (const GstStructure *structure = gst_message_get_structure(message))
        gst_structure_get_int(structure, "generation", &generation);
    return generation;
}

bool Crossfade::prepare(GstElement *in)
{
    GstElement *mixer = nullptr;
    {
        QMutexLocker lock(&m->lock);
        if (m->state != Shared::State::Planned)
            return false;
        GstSegment segment;
        gst_segment_copy_into(&m->segment, &segment);
        if (GstEvent *sticky = gst_pad_get_sticky_event(m->outSrc, GST_EVENT_SEGMENT, 0)) {
            const GstSegment *current = nullptr;
            gst_event_parse_segment(sticky, &current);
            gst_segment_copy_into(current, &segment);
            gst_event_unref(sticky);
        }
        const guint64 running = gst_segment_to_running_time(&segment, GST_FORMAT_TIME, m->schedule.start);
        if (running == guint64(-1))
            return false;
        GError *err = nullptr;
        mixer = gst_parse_bin_from_description(mixerDescription, FALSE, &err);
        if (!mixer) {
            qWarning() << "microdap player: crossfade mixer" << (err ? err->message : "failed");
            g_clear_error(&err);
            return false;
        }
        g_clear_error(&err);
        gst_object_set_name(GST_OBJECT(mixer), mixerName);
        ghost(mixer, "fadeA", "sink", "sink_a");
        ghost(mixer, "fadeB", "sink", "sink_b");
        ghost(mixer, "mix", "src", "src");
        fade(childOf(mixer, "fadeA"), m->schedule.start, m->length, true);
        fade(childOf(mixer, "fadeB"), 0, m->length, false);
        m->mixer = GST_ELEMENT(gst_object_ref(mixer));
        m->mixOut = padOf(mixer, "sink_a");
        m->mixIn = padOf(mixer, "sink_b");
        m->mixSrc = padOf(mixer, "src");
        m->inSrc = padOf(in, "src");
        const gchar *outName = GST_OBJECT_NAME(GST_PAD_PARENT(m->outSrc));
        gst_object_set_name(GST_OBJECT(in), g_strcmp0(outName, "deck") == 0 ? "deck1" : "deck");
        m->offset = running + gst_pad_get_offset(m->outSrc);
        gst_pad_set_offset(m->inSrc, gint64(m->offset));
        gst_bin_add_many(GST_BIN(m->pipeline), in, mixer, nullptr);
        if (gst_pad_link(m->inSrc, m->mixIn) != GST_PAD_LINK_OK)
            qWarning() << "microdap player: crossfade link failed";
        m->firstProbe = gst_pad_add_probe(m->inSrc, GST_PAD_PROBE_TYPE_BUFFER, &Shared::firstBuffer, Shared::hold(m),
                                          &Shared::drop);
        m->state = Shared::State::Preparing;
    }
    gst_element_sync_state_with_parent(mixer);
    gst_element_sync_state_with_parent(in);
    return true;
}

void Crossfade::cancel()
{
    QMutexLocker lock(&m->lock);
    if (m->state != Shared::State::Finished)
        m->state = Shared::State::Cancelled;
    m->removeAll();
}

bool Crossfade::abort()
{
    QMutexLocker lock(&m->lock);
    if (m->mixing())
        return false;
    m->state = Shared::State::Cancelled;
    m->removeAll();
    return true;
}

void Crossfade::stop()
{
    cancel();
    GstElement *mixer = nullptr;
    {
        QMutexLocker lock(&m->lock);
        if (m->mixer)
            mixer = GST_ELEMENT(gst_object_ref(m->mixer));
    }
    if (mixer) {
        gst_element_set_state(mixer, GST_STATE_NULL);
        gst_object_unref(mixer);
    }
}

void Crossfade::release(GstElement *keep)
{
    stop();
    GstElement *mixer = nullptr;
    {
        QMutexLocker lock(&m->lock);
        mixer = std::exchange(m->mixer, nullptr);
    }
    if (mixer) {
        for (GstPad *pad : {m->mixOut, m->mixIn, m->mixSrc})
            unlinkPeer(pad);
        gst_bin_remove(GST_BIN(m->pipeline), mixer);
        gst_object_unref(mixer);
    }
    GstPad *src = padOf(keep, "src");
    if (src && m->tailSink && !gst_pad_is_linked(src)) {
        unlinkPeer(m->tailSink);
        if (gst_pad_link(src, m->tailSink) != GST_PAD_LINK_OK)
            qWarning() << "microdap player: crossfade relink failed";
    }
    if (src)
        gst_object_unref(src);
}

bool Crossfade::mixing() const
{
    QMutexLocker lock(&m->lock);
    return m->mixing();
}

GstClockTime Crossfade::offset() const
{
    QMutexLocker lock(&m->lock);
    return m->offset;
}

GstClockTime Crossfade::length() const
{
    return m->length;
}

}
