#pragma once

#include "Queue.h"

#include <gst/gst.h>

#include <memory>

namespace microdap {

struct CrossfadeSchedule
{
    GstClockTime prepare = GST_CLOCK_TIME_NONE;
    GstClockTime start = GST_CLOCK_TIME_NONE;

    bool valid() const { return GST_CLOCK_TIME_IS_VALID(start); }
};

CrossfadeSchedule crossfadeSchedule(GstClockTime duration, GstClockTime length, GstClockTime lead);
double crossfadeWeight(double progress, bool outgoing);
bool albumContinues(const QueueEntry &from, const QueueEntry &to);
bool crossfadeFits(qint64 fromMs, qint64 toMs, qint64 lengthMs);

class Crossfade
{
public:
    static constexpr const char *prepareDue = "microdap-xfade-prepare";
    static constexpr const char *refused = "microdap-xfade-refused";
    static constexpr const char *finished = "microdap-xfade-done";
    static constexpr const char *early = "microdap-xfade-early";
    static constexpr const char *mixerName = "xfade";

    Crossfade(GstElement *pipeline, GstElement *tail, GstElement *out, const CrossfadeSchedule &schedule,
              GstClockTime length, int generation);
    ~Crossfade();
    Crossfade(const Crossfade &) = delete;
    Crossfade &operator=(const Crossfade &) = delete;

    static bool available();
    static int generationOf(GstMessage *message);

    bool prepare(GstElement *in, GstClockTime inStart = 0);
    void cancel();
    bool abort();
    void stop();
    void release(GstElement *keep);
    bool mixing() const;
    GstClockTime offset() const;
    GstClockTime length() const;

private:
    struct Shared;
    std::shared_ptr<Shared> m;
};

}
