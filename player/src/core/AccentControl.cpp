#include "AccentControl.h"

#include "Settings.h"

#include <QDebug>
#include <QtMath>

#include <cmath>
#include <utility>

namespace microdap {

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

bool lit(int hue, int saturation, int value)
{
    return AccentControl::luminance(AccentControl::hsv(hue, saturation / 100.0, value / 100.0))
           >= AccentControl::minLuminance;
}

std::pair<int, int> fitArt(int hue, const QColor &tone)
{
    const int top = qRound(AccentControl::accentValue * 100);
    const int least = qRound(AccentControl::minSaturation * 100);
    int s = qBound(0, qRound(tone.hsvSaturationF() * 100), 100);
    int v = qBound(0, qRound(tone.valueF() * 100), top);
    while (v < top && !lit(hue, s, v))
        ++v;
    while (s > least && !lit(hue, s, v))
        --s;
    return {s, v};
}

}

AccentControl::AccentControl(QObject *parent)
    : QObject(parent)
{
    connect(&m_art, &ArtColor::found, this, &AccentControl::onFound);
}

QColor AccentControl::hsv(int hue, double saturation, double value)
{
    const double h = (((hue % 360) + 360) % 360) / 60.0;
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
    const int h = Settings::normalizeHue(hue);
    for (int step = qRound(maxSaturation * 100); step >= qRound(minSaturation * 100); --step) {
        const double s = step / 100.0;
        if (luminance(hsv(h, s, accentValue)) >= minLuminance)
            return s;
    }
    return minSaturation;
}

QColor AccentControl::accentFor(int hue)
{
    const int h = Settings::normalizeHue(hue);
    return hsv(h, saturationFor(h), accentValue);
}

QColor AccentControl::brightFor(int hue)
{
    const int h = Settings::normalizeHue(hue);
    return hsv(h, brightScale * saturationFor(h), 1.0);
}

QColor AccentControl::artAccentFor(int hue, const QColor &tone)
{
    const auto [s, v] = fitArt(hue, tone);
    return hsv(hue, s / 100.0, v / 100.0);
}

QColor AccentControl::artBrightFor(int hue, const QColor &tone)
{
    const int s = fitArt(hue, tone).first;
    return hsv(hue, brightScale * s / 100.0, 1.0);
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

int AccentControl::artLevel() const
{
    return m_artLevel;
}

QColor AccentControl::artTone() const
{
    return m_artTone;
}

int AccentControl::hue() const
{
    return m_hue;
}

int AccentControl::level() const
{
    return m_level;
}

QColor AccentControl::color() const
{
    return m_level > 0 ? artAccentFor(m_hue, m_tone) : accentFor(m_hue);
}

QColor AccentControl::brightColor() const
{
    return m_level > 0 ? artBrightFor(m_hue, m_tone) : brightFor(m_hue);
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
        setArt(ArtPick());
        return;
    }
    if (m_art.contains(m_artPath)) {
        const ArtPick pick = m_art.cached(m_artPath);
        logArt(m_artPath, pick);
        setArt(pick);
        return;
    }
    m_art.request(m_artPath);
}

void AccentControl::onFound(const QString &path, int hue, int level, const QColor &color)
{
    if (path != m_artPath || !m_fromArt || m_phoneActive)
        return;
    ArtPick pick;
    pick.hue = hue;
    pick.level = level;
    pick.color = color;
    logArt(path, pick);
    setArt(pick);
}

void AccentControl::logArt(const QString &path, const ArtPick &pick)
{
    if (path == m_loggedPath)
        return;
    m_loggedPath = path;
    if (pick.valid())
        qInfo().noquote() << QStringLiteral("microdap accent: art hue %1 s %2 for %3").arg(pick.hue).arg(pick.level).arg(path);
    else
        qInfo().noquote() << QStringLiteral("microdap accent: no art hue for %1").arg(path);
}

void AccentControl::setArt(const ArtPick &pick)
{
    const int hue = pick.valid() ? pick.hue : -1;
    const int level = pick.valid() ? pick.level : 0;
    const QColor tone = pick.valid() ? pick.color : QColor();
    if (m_artHue == hue && m_artLevel == level && m_artTone == tone)
        return;
    m_artHue = hue;
    m_artLevel = level;
    m_artTone = tone;
    emit artHueChanged();
    updateHue();
}

void AccentControl::updateHue()
{
    const bool art = m_fromArt && m_artHue >= 0;
    const int value = art ? m_artHue : m_sliderHue;
    const int level = art ? m_artLevel : 0;
    const QColor tone = art ? m_artTone : QColor();
    if (m_hue == value && m_level == level && m_tone == tone)
        return;
    m_hue = value;
    m_level = level;
    m_tone = tone;
    qInfo().noquote() << QStringLiteral("microdap accent: hue %1 s %2 (%3)")
                             .arg(value)
                             .arg(art ? level : 4)
                             .arg(art ? QStringLiteral("art") : QStringLiteral("slider"));
    emit hueChanged();
}

}
