#pragma once

#include "OutputPorts.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <gst/gst.h>

#include <atomic>
#include <functional>

namespace microdap {

class Deck
{
public:
    using Next = std::function<QString()>;

    Deck(PlayerPort::OutputChain chain, bool dsd);
    ~Deck();
    Deck(const Deck &) = delete;
    Deck &operator=(const Deck &) = delete;

    static bool available();
    static int dsdPcmRateFor(int decodedRate);
    static constexpr const char *dsdRefused = "dsd128 and up not supported";
    static constexpr const char *dsdAccepted = "microdap-dsd-accepted";
    static constexpr const char *switched = "microdap-deck-switched";

    void setNext(Next next);
    void start(const QString &path);
    void close();
    void reap();

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

    bool append(const QString &path);
    void drop(const Item &item);
    static void padAdded(GstElement *decoder, GstPad *pad, gpointer data);
    static void drained(GstElement *decoder, gpointer data);
    static void activePad(GObject *join, GParamSpec *pspec, gpointer data);
    static GstPadProbeReturn capsProbe(GstPad *pad, GstPadProbeInfo *info, gpointer data);
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
