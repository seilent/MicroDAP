#include "AccentControl.h"

#include "Settings.h"

#include <QDebug>
#include <QtMath>

#include <cmath>

namespace casdap {

namespace {

int channel(double c)
{
    return qBound(0, qRound(255.0 * c), 255);
}

double linear(int c)
{
    const double v = c / 255.0;
    return v <= 0.04045 ? v / 12.92 : qPow((v + 0.055) / 1.055, 2.4);
}

}

AccentControl::AccentControl(QObject *parent)
    : QObject(parent)
{
    connect(&m_art, &ArtColor::found, this, &AccentControl::onFound);
}

QColor AccentControl::hsv(int hue, double saturation, double value)
{
    const double h = Settings::normalizeHue(hue) / 60.0;
    const int k = int(h) % 6;
    const double f = h - std::floor(h);
    const double p = value * (1.0 - saturation);
    const double q = value * (1.0 - saturation * f);
    const double t = value * (1.0 - saturation * (1.0 - f));
    double r = value;
    double g = t;
    double b = p;
    switch (k) {
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    case 5:
        r = value;
        g = p;
        b = q;
        break;
    default:
        break;
    }
    return QColor(channel(r), channel(g), channel(b));
}

double AccentControl::luminance(const QColor &color)
{
    return 0.2126 * linear(color.red()) + 0.7152 * linear(color.green()) + 0.0722 * linear(color.blue());
}

double AccentControl::saturationFor(int hue)
{
    for (int step = 83; step >= 40; --step) {
        const double s = step / 100.0;
        if (luminance(hsv(hue, s, accentValue)) >= minLuminance)
            return s;
    }
    return 0.40;
}

QColor AccentControl::accentFor(int hue)
{
    return hsv(hue, saturationFor(hue), accentValue);
}

QColor AccentControl::brightFor(int hue)
{
    return hsv(hue, brightScale * saturationFor(hue), 1.0);
}

int AccentControl::sliderHue() const
{
    return m_sliderHue;
}

void AccentControl::setSliderHue(int hue)
{
    const int value = Settings::normalizeHue(hue);
    if (m_sliderHue == value)
        return;
    m_sliderHue = value;
    emit sliderHueChanged();
    updateHue();
}

bool AccentControl::fromArt() const
{
    return m_fromArt;
}

void AccentControl::setFromArt(bool on)
{
    if (m_fromArt == on)
        return;
    m_fromArt = on;
    emit fromArtChanged();
    refresh();
}

int AccentControl::artHue() const
{
    return m_artHue;
}

int AccentControl::hue() const
{
    return m_hue;
}

QColor AccentControl::color() const
{
    return accentFor(m_hue);
}

QColor AccentControl::brightColor() const
{
    return brightFor(m_hue);
}

QString AccentControl::artPath() const
{
    return m_artPath;
}

bool AccentControl::phoneActive() const
{
    return m_phoneActive;
}

ArtColor *AccentControl::artColor()
{
    return &m_art;
}

QColor AccentControl::colorFor(int hue) const
{
    return accentFor(hue);
}

QColor AccentControl::brightColorFor(int hue) const
{
    return brightFor(hue);
}

void AccentControl::setArtPath(const QString &path)
{
    if (m_artPath == path)
        return;
    m_artPath = path;
    emit artPathChanged();
    refresh();
}

void AccentControl::setPhoneActive(bool active)
{
    if (m_phoneActive == active)
        return;
    m_phoneActive = active;
    emit phoneActiveChanged();
    refresh();
}

void AccentControl::clearCache()
{
    m_art.clear();
}

void AccentControl::refresh()
{
    if (!m_fromArt || m_phoneActive || m_artPath.isEmpty()) {
        setArtHue(-1);
        return;
    }
    if (m_art.contains(m_artPath)) {
        const int hue = m_art.cachedHue(m_artPath);
        logArt(m_artPath, hue);
        setArtHue(hue);
        return;
    }
    m_art.request(m_artPath);
}

void AccentControl::onFound(const QString &path, int hue)
{
    if (path != m_artPath || !m_fromArt || m_phoneActive)
        return;
    logArt(path, hue);
    setArtHue(hue);
}

void AccentControl::logArt(const QString &path, int hue)
{
    if (path == m_loggedPath)
        return;
    m_loggedPath = path;
    if (hue >= 0)
        qInfo().noquote() << QStringLiteral("casdap accent: art hue %1 for %2").arg(hue).arg(path);
    else
        qInfo().noquote() << QStringLiteral("casdap accent: no art hue for %1").arg(path);
}

void AccentControl::setArtHue(int hue)
{
    if (m_artHue == hue)
        return;
    m_artHue = hue;
    emit artHueChanged();
    updateHue();
}

void AccentControl::updateHue()
{
    const bool art = m_fromArt && m_artHue >= 0;
    const int value = art ? m_artHue : m_sliderHue;
    if (m_hue == value)
        return;
    m_hue = value;
    qInfo().noquote() << QStringLiteral("casdap accent: hue %1 (%2)")
                             .arg(value)
                             .arg(art ? QStringLiteral("art") : QStringLiteral("slider"));
    emit hueChanged();
}

}
