#include "OverlayBridge.h"

#include "DbusPad.h"
#include "Player.h"

#include <QDebug>
#include <QtGlobal>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstddef>
#include <cstring>
#include <utility>

namespace casdap {

OverlayBridge::OverlayBridge(DbusPad *pad, Player *player, QObject *parent)
    : QObject(parent)
    , m_pad(pad)
    , m_player(player)
{
    m_fade.setStartValue(0.0);
    m_fade.setEndValue(0.0);
    connect(&m_fade, &QVariantAnimation::valueChanged, this,
            [this](const QVariant &value) { setOpacity(value.toReal()); });
    connect(&m_fade, &QVariantAnimation::finished, this, &OverlayBridge::onFadeFinished);
    connect(&m_fade, &QVariantAnimation::stateChanged, this, &OverlayBridge::onFadeState);

    if (m_player)
        m_player->setPositionPollEnabled(false);

    m_watchdog.setTimerType(Qt::CoarseTimer);
    m_watchdog.setInterval(WatchdogMs);
    connect(&m_watchdog, &QTimer::timeout, this, &OverlayBridge::ping);
    if (!notifySocket().isEmpty())
        m_watchdog.start();
}

OverlayBridge::~OverlayBridge()
{
    m_fade.stop();
    m_watchdog.stop();
    if (m_pad)
        m_pad->setShown(false);
}

QByteArray OverlayBridge::notifySocket()
{
    return qgetenv("NOTIFY_SOCKET");
}

bool OverlayBridge::notify(const QByteArray &state)
{
    const QByteArray path = notifySocket();
    if (path.isEmpty())
        return false;
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    if (path.size() >= static_cast<qsizetype>(sizeof(address.sun_path)))
        return false;
    std::memcpy(address.sun_path, path.constData(), static_cast<size_t>(path.size()));
    socklen_t length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() + 1);
    if (path.at(0) == '@') {
        address.sun_path[0] = '\0';
        length = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size());
    }
    const int fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return false;
    const ssize_t sent = sendto(fd, state.constData(), static_cast<size_t>(state.size()), MSG_NOSIGNAL,
                                reinterpret_cast<const sockaddr *>(&address), length);
    close(fd);
    return sent == state.size();
}

DbusPad *OverlayBridge::pad() const
{
    return m_pad;
}

Player *OverlayBridge::player() const
{
    return m_player;
}

QWindow *OverlayBridge::window() const
{
    return m_window;
}

void OverlayBridge::setWindow(QWindow *window, WindowPrepare prepare)
{
    m_window = window;
    m_prepare = prepare ? std::move(prepare) : WindowPrepare([](QWindow *target) { target->create(); });
    if (m_window && !m_window->handle())
        m_prepare(m_window);
    notifyReady();
}

bool OverlayBridge::ready() const
{
    return m_ready;
}

bool OverlayBridge::shown() const
{
    return m_shown;
}

bool OverlayBridge::mapped() const
{
    return m_mapped;
}

qreal OverlayBridge::opacity() const
{
    return m_opacity;
}

bool OverlayBridge::fading() const
{
    return m_fading;
}

int OverlayBridge::mapCount() const
{
    return m_mapCount;
}

bool OverlayBridge::watchdogActive() const
{
    return m_watchdog.isActive();
}

void OverlayBridge::show()
{
    if (m_shown)
        return;
    m_shown = true;
    qInfo() << "casdap overlay: shown";
    emit shownChanged();
    if (m_pad)
        m_pad->setShown(true);
    mapWindow();
    setMapped(true);
    if (m_player)
        m_player->setPositionPollEnabled(true);
    fadeTo(1.0, ShowMs, QEasingCurve::Linear);
}

void OverlayBridge::hide()
{
    if (!m_shown)
        return;
    m_shown = false;
    qInfo() << "casdap overlay: hidden";
    emit shownChanged();
    if (m_pad)
        m_pad->setShown(false);
    fadeTo(0.0, HideMs, QEasingCurve::OutExpo);
}

void OverlayBridge::toggle()
{
    if (m_shown)
        hide();
    else
        show();
}

void OverlayBridge::fadeTo(qreal to, int ms, QEasingCurve::Type curve)
{
    m_fade.stop();
    m_fade.setStartValue(m_opacity);
    m_fade.setEndValue(to);
    m_fade.setDuration(ms);
    m_fade.setEasingCurve(curve);
    m_fade.start();
}

void OverlayBridge::onFadeFinished()
{
    setOpacity(m_fade.endValue().toReal());
    if (m_shown || m_opacity > 0.0)
        return;
    setMapped(false);
    destroyWindow();
    if (m_player)
        m_player->setPositionPollEnabled(false);
}

void OverlayBridge::onFadeState()
{
    const bool fading = m_fade.state() == QAbstractAnimation::Running;
    if (fading == m_fading)
        return;
    m_fading = fading;
    emit fadingChanged();
}

void OverlayBridge::setOpacity(qreal value)
{
    if (qFuzzyCompare(m_opacity + 1.0, value + 1.0))
        return;
    m_opacity = value;
    emit opacityChanged();
}

void OverlayBridge::setMapped(bool mapped)
{
    if (m_mapped == mapped)
        return;
    m_mapped = mapped;
    if (mapped)
        ++m_mapCount;
    emit mappedChanged();
}

void OverlayBridge::mapWindow()
{
    if (!m_window)
        return;
    if (!m_window->handle())
        m_prepare(m_window);
    m_window->setVisible(true);
}

void OverlayBridge::destroyWindow()
{
    if (!m_window || !m_window->handle())
        return;
    m_window->setVisible(false);
    m_window->destroy();
}

void OverlayBridge::notifyReady()
{
    if (m_ready)
        return;
    m_ready = true;
    if (notifySocket().isEmpty())
        return;
    if (!notify(QByteArrayLiteral("READY=1\n")))
        qWarning() << "casdap overlay: ready notify failed" << notifySocket();
    ping();
}

void OverlayBridge::ping()
{
    if (notify(QByteArrayLiteral("WATCHDOG=1\n"))) {
        m_pingFailed = false;
        return;
    }
    if (m_pingFailed)
        return;
    m_pingFailed = true;
    qWarning() << "casdap overlay: watchdog ping failed" << notifySocket();
}

}
