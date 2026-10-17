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

QString RgbControl::hueKey()
{
    return QStringLiteral("rgb.hue");
}

QString RgbControl::saturationKey()
{
    return QStringLiteral("rgb.saturation");
}

int RgbControl::saturationFor(int level)
{
    return saturation[level >= 1 && level <= levels ? level : 0];
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
    const QString schema = m_client ? m_client->schema(QStringLiteral("rgb."), &error) : QString();
    const bool supported = m_client && error.ok() && schema.contains(hueKey());
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
    if (key == hueKey() && accent)
        return QVariant::fromValue(uint(m_hue));
    if (key == saturationKey() && accent)
        return QVariant::fromValue(uint(saturationFor(m_level)));
    return std::nullopt;
}

void RgbControl::apply()
{
    if (!m_probed || !m_supported)
        return;
    send(enabledKey());
    if (m_hueTimer.isActive())
        return;
    send(hueKey());
    send(saturationKey());
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
