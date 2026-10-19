#pragma once

#include "OutputPorts.h"
#include "Queue.h"

#include <QHash>
#include <QList>
#include <QMutex>
#include <QString>

#include <gst/gst.h>

#include <atomic>
#include <functional>
#include <optional>

namespace microdap {

class Deck
{
public:
    struct Mark
    {
        GstClockTime at = GST_CLOCK_TIME_NONE;
        QueueEntry entry;
    };

    struct Track
    {
        QueueEntry entry;
        QList<Mark> marks;
    };

    using Next = std::function<Track()>;
    using Gain = std::function<double(const QueueEntry &)>;

    Deck(PlayerPort::OutputChain chain, bool dsd);
    ~Deck();
    Deck(const Deck &) = delete;
    Deck &operator=(const Deck &) = delete;

    static bool available();
    static int dsdPcmRateFor(int decodedRate);
    static constexpr const char *dsdRefused = "dsd128 and up not supported";
    static constexpr const char *dsdAccepted = "microdap-dsd-accepted";
    static constexpr const char *switched = "microdap-deck-switched";
    static constexpr const char *cueMark = "microdap-cue-mark";

    void setNext(Next next);
    void setGain(Gain gain);
    bool start(const QueueEntry &entry, const QList<Mark> &marks = {});
    void setMarks(const QList<Mark> &marks, bool atFlush);
    void settleMarks();
    int markGeneration() const;
    void regain();
    void close();
    void reopen();
    void reap();
    void resetOffset();

    GstElement *element() const;
    GstElement *volume() const;
    GstElement *cueProbe() const;
    GstPad *decodedPad() const;
    bool rateAccepted() const;

private:
    struct Item {
        GstElement *decoder = nullptr;
        GstPad *pad = nullptr;
    };

    bool append(const QueueEntry &entry, const QList<Mark> &marks);
    void drop(const Item &item);
    static void padAdded(GstElement *decoder, GstPad *pad, gpointer data);
    static void drained(GstElement *decoder, gpointer data);
    static void activePad(GObject *join, GParamSpec *pspec, gpointer data);
    static GstPadProbeReturn capsProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    struct Start
    {
        Deck *deck = nullptr;
        QueueEntry entry;
        QList<Mark> marks;
        bool seen = false;
    };

    static QString streamOf(GstPadProbeInfo *info);
    static GstPadProbeReturn itemProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static GstPadProbeReturn gainProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static GstPadProbeReturn markProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    static GstPadProbeReturn flushProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
    void applyGain(const QueueEntry &entry);
    void accept();

    GstElement *m_bin = nullptr;
    GstElement *m_join = nullptr;
    GstElement *m_entry = nullptr;
    GstElement *m_volume = nullptr;
    GstElement *m_cueProbe = nullptr;
    GstElement *m_dsdRate = nullptr;
    bool m_pw = false;
    bool m_dsd = false;
    enum class Rate { Unknown, Accepted, Refused };
    std::atomic<Rate> m_rate{Rate::Unknown};
    QMutex m_lock;
    QList<Item> m_items;
    bool m_open = false;
    Next m_next;
    QMutex m_gainLock;
    QHash<QString, QList<QueueEntry>> m_starts;
    QHash<QString, QList<QList<Mark>>> m_marks;
    QList<Mark> m_current;
    std::optional<QList<Mark>> m_rearm;
    QString m_stream;
    GstSegment m_segment;
    std::atomic<bool> m_armed{false};
    std::atomic<int> m_markGeneration{0};
    std::optional<QueueEntry> m_playing;
    Gain m_gain;
};

class Tail
{
public:
    Tail(GstElement *filter, GstElement *sink);
    ~Tail();
    Tail(const Tail &) = delete;
    Tail &operator=(const Tail &) = delete;

    GstElement *element() const;

private:
    GstElement *m_bin = nullptr;
};

}
