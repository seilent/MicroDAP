#include "OutputWatch.h"

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QScreen>

namespace casdap {

namespace {

bool skippedConnector(const QString &connector)
{
    return connector.startsWith(QLatin1StringView("DSI-")) || connector.startsWith(QLatin1StringView("eDP-"))
           || connector.startsWith(QLatin1StringView("LVDS-")) || connector.startsWith(QLatin1StringView("Writeback-"));
}

}

OutputWatch::OutputWatch(QObject *parent)
    : OutputWatch(defaultRoot(), parent)
{
}

OutputWatch::OutputWatch(const QString &root, QObject *parent)
    : QObject(parent)
    , m_root(root)
{
    m_poll.setInterval(pollMs);
    connect(&m_poll, &QTimer::timeout, this, &OutputWatch::refresh);
    m_settleShort.setSingleShot(true);
    m_settleShort.setInterval(settleShortMs);
    connect(&m_settleShort, &QTimer::timeout, this, &OutputWatch::refresh);
    m_settleLong.setSingleShot(true);
    m_settleLong.setInterval(settleLongMs);
    connect(&m_settleLong, &QTimer::timeout, this, &OutputWatch::refresh);
    if (qGuiApp) {
        const QList<QScreen *> screens = QGuiApplication::screens();
        for (QScreen *screen : screens)
            watchScreen(screen);
        connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *screen) {
            watchScreen(screen);
            scheduleReads();
        });
        connect(qGuiApp, &QGuiApplication::screenRemoved, this, &OutputWatch::scheduleReads);
    }
    m_internal = readInternal();
    m_poll.start();
}

QString OutputWatch::defaultRoot()
{
    return qEnvironmentVariable("CASDAP_DRM_ROOT", QStringLiteral("/sys/class/drm"));
}

QString OutputWatch::root() const
{
    return m_root;
}

bool OutputWatch::internal() const
{
    return m_internal;
}

void OutputWatch::refresh()
{
    const bool value = readInternal();
    if (m_internal == value)
        return;
    m_internal = value;
    emit internalChanged();
}

void OutputWatch::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    m_screenOff = off;
    if (off)
        m_poll.stop();
    else
        m_poll.start();
}

void OutputWatch::scheduleReads()
{
    refresh();
    m_settleShort.start();
    m_settleLong.start();
}

void OutputWatch::watchScreen(QScreen *screen)
{
    connect(screen, &QScreen::geometryChanged, this, &OutputWatch::scheduleReads);
}

bool OutputWatch::readInternal() const
{
    const QStringList entries = QDir(m_root).entryList({QStringLiteral("card*-*")},
                                                       QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (skippedConnector(entry.section(QLatin1Char('-'), 1)))
            continue;
        QFile file(m_root + QLatin1Char('/') + entry + QStringLiteral("/enabled"));
        if (!file.open(QIODevice::ReadOnly))
            continue;
        if (file.readAll().trimmed() == "enabled")
            return false;
    }
    return true;
}

}
