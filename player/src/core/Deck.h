#pragma once

#include "OutputPorts.h"

#include <QList>
#include <QMutex>
#include <QString>

#include <gst/gst.h>

#include <functional>

namespace microdap {

class Deck
{
public:
    using Next = std::function<QString()>;

    explicit Deck(PlayerPort::OutputChain chain);
    ~Deck();
    Deck(const Deck &) = delete;
    Deck &operator=(const Deck &) = delete;

    static bool available();
    static constexpr const char *switched = "microdap-deck-switched";

    void setNext(Next next);
    void start(const QString &path);
    void close();
    void reap();

    GstElement *element() const;
    GstElement *volume() const;
    GstElement *cueProbe() const;
    GstPad *decodedPad() const;

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

    GstElement *m_bin = nullptr;
    GstElement *m_join = nullptr;
    GstElement *m_entry = nullptr;
    GstElement *m_volume = nullptr;
    GstElement *m_cueProbe = nullptr;
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
