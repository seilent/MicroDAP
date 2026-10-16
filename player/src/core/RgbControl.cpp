#include "RgbControl.h"

#include "Settings.h"

#include <microset/client.h>

#include <QDebug>

#include <array>

namespace microdap {

namespace {

constexpr std::array<int, RgbControl::levels + 1> saturation{100, 40, 60, 80, 100};

}

RgbControl::RgbControl(MicroSet::Client *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_mode(QStringLiteral("system"))
{
    m_hueTimer.setSingleShot(true);
    m_hueTimer.setInterval(hueDelayMs);
    connect(&m_hueTimer, &QTimer::timeout, this, &RgbControl::apply);
}

RgbControl::~RgbControl() = default;

QString RgbControl::enabledKey()
{
    return QStringLiteral("rgb.enabled");
}

QString RgbControl::colorKey()
{
    return QStringLiteral("rgb.color");
}

uint RgbControl::colorFor(int hue, int level)
{
    const int h = Settings::normalizeHue(hue);
    const int l = saturation[level >= 1 && level <= levels ? level : 0];
    const int k = h / 60;
    const int f = h % 60;
    const int lo = (255 * (100 - l) + 50) / 100;
    const int span = 255 - lo;
    const int up = lo + (span * f + 30) / 60;
    const int down = lo + (span * (60 - f) + 30) / 60;
    const auto rgb = [](int r, int g, int b) { return uint(r) << 16 | uint(g) << 8 | uint(b); };
    switch (k) {
    case 0:
        return rgb(255, up, lo);
    case 1:
        return rgb(down, 255, lo);
    case 2:
        return rgb(lo, 255, up);
    case 3:
        return rgb(lo, down, 255);
    case 4:
        return rgb(up, lo, 255);
    default:
        return rgb(255, lo, down);
    }
}

bool RgbControl::supported() const
{
    return m_supported;
}

bool RgbControl::probed() const
{
    return m_probed;
}

bool RgbControl::busy() const
{
    return m_busy;
}

QString RgbControl::mode() const
{
    return m_mode;
}

int RgbControl::hue() const
{
    return m_hue;
}

int RgbControl::level() const
{
    return m_level;
}

bool RgbControl::screenOff() const
{
    return m_screenOff;
}

bool RgbControl::saver() const
{
    return m_saver;
}

std::optional<QVariant> RgbControl::sent(const QString &key) const
{
    const auto it = m_sent.constFind(key);
    if (it == m_sent.constEnd())
        return std::nullopt;
    return *it;
}

void RgbControl::start(const QString &mode, int hue, int level)
{
    const QString m = mode.toLower();
    if (!Settings::rgbModes().contains(m))
        qWarning() << "microdap rgb: unknown mode" << mode;
    else if (m_mode != m) {
        m_mode = m;
        emit modeChanged();
    }
    const int h = Settings::normalizeHue(hue);
    if (m_hue != h) {
        m_hue = h;
        emit hueChanged();
    }
    const int l = level >= 1 && level <= levels ? level : 0;
    if (m_level != l) {
        m_level = l;
        emit levelChanged();
    }
    MicroSet::Error error;
    const QString schema = m_client ? m_client->schema(enabledKey(), &error) : QString();
    const bool supported = m_client && error.ok() && schema.contains(enabledKey());
    qInfo() << "microdap rgb: supported" << supported;
    if (m_supported != supported) {
        m_supported = supported;
        emit supportedChanged();
    }
    m_probed = true;
    emit probedChanged();
    apply();
}

void RgbControl::setMode(const QString &mode)
{
    const QString m = mode.toLower();
    if (!Settings::rgbModes().contains(m)) {
        qWarning() << "microdap rgb: unknown mode" << mode;
        return;
    }
    if (m_mode == m)
        return;
    m_mode = m;
    emit modeChanged();
    m_hueTimer.stop();
    apply();
}

void RgbControl::setHue(int hue)
{
    setColor(hue, 0);
}

void RgbControl::setColor(int hue, int level)
{
    const int h = Settings::normalizeHue(hue);
    const int l = level >= 1 && level <= levels ? level : 0;
    if (m_hue == h && m_level == l)
        return;
    const bool hueMoved = m_hue != h;
    const bool levelMoved = m_level != l;
    m_hue = h;
    m_level = l;
    if (hueMoved)
        emit hueChanged();
    if (levelMoved)
        emit levelChanged();
    if (m_mode == QLatin1StringView("accent"))
        m_hueTimer.start();
}

void RgbControl::setScreenOff(bool off)
{
    if (m_screenOff == off)
        return;
    m_screenOff = off;
    emit screenOffChanged();
    apply();
}

void RgbControl::setSaver(bool on)
{
    if (m_saver == on)
        return;
    m_saver = on;
    emit saverChanged();
    apply();
}

std::optional<QVariant> RgbControl::wanted(const QString &key) const
{
    const bool accent = m_mode == QLatin1StringView("accent");
    if (key == enabledKey()) {
        if (m_mode == QLatin1StringView("off") || m_screenOff || m_saver)
            return QVariant::fromValue(false);
        if (accent)
            return QVariant::fromValue(true);
        return std::nullopt;
    }
    if (key == colorKey() && accent)
        return QVariant::fromValue(colorFor(m_hue, m_level));
    return std::nullopt;
}

void RgbControl::apply()
{
    if (!m_probed || !m_supported)
        return;
    send(enabledKey());
    if (!m_hueTimer.isActive())
        send(colorKey());
}

void RgbControl::send(const QString &key)
{
    const std::optional<QVariant> value = wanted(key);
    const std::optional<QVariant> current = sent(key);
    if (value == current)
        return;
    ++m_inflight;
    updateBusy();
    if (!value) {
        m_sent.remove(key);
        qInfo() << "microdap rgb: clear" << key;
        m_client->clearOverride(key, [this, key](const MicroSet::Error &error) { finished(key, error.ok()); });
        return;
    }
    m_sent.insert(key, *value);
    qInfo() << "microdap rgb: override" << key << *value;
    m_client->override(key, *value, [this, key, sentValue = *value](const MicroSet::Error &error) {
        if (!error.ok()) {
            qWarning().noquote() << "microdap rgb: override" << key << "refused," << error.name << error.message;
            if (m_sent.value(key) == sentValue)
                m_sent.remove(key);
        }
        finished(key, error.ok());
    });
}

void RgbControl::finished(const QString &key, bool ok)
{
    --m_inflight;
    updateBusy();
    emit applied(key, ok);
}

void RgbControl::updateBusy()
{
    const bool busy = m_inflight > 0;
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged();
}

}
