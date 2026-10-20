#include "Env.h"

#include "PathFit.h"
#include "SettingsRows.h"

#include <QCoreApplication>
#include <QFontMetricsF>
#include <QTextLayout>

namespace microdap {

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::defaultFontDir()
{
    return QStringLiteral("/usr/share/fonts/microset");
}

QString Env::shotPhoto() const
{
    return qEnvironmentVariable("MICRODAP_SHOT_PHOTO");
}

QString Env::fontDir() const
{
    const QString dir = qEnvironmentVariable("MICRODAP_FONT_DIR");
    return dir.isEmpty() ? defaultFontDir() : dir;
}

QString Env::version() const
{
    return QCoreApplication::applicationVersion();
}

int Env::landscapeTurn() const
{
    return qEnvironmentVariable("MICRODAP_LANDSCAPE_TURN") == QLatin1StringView("3") ? 3 : 1;
}

bool Env::rotateMotion() const
{
    return qEnvironmentVariable("MICRODAP_ROTATE_MOTION") != QLatin1StringView("0");
}

QVariantList Env::settingsRows() const
{
    return SettingsRows::maps();
}

QStringList Env::settingsPivots() const
{
    return SettingsRows::pivots();
}

QString Env::fitPath(const QStringList &parts, const QFont &font, qreal width) const
{
    const QFontMetricsF metrics(font);
    return fitPathParts(parts, [&metrics](const QString &text) { return metrics.horizontalAdvance(text); }, width);
}

QStringList Env::wrapLines(const QString &text, const QFont &font, qreal width) const
{
    QStringList out;
    if (text.isEmpty())
        return out;
    QTextLayout layout(text, font);
    QTextOption option;
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    layout.setTextOption(option);
    layout.beginLayout();
    for (QTextLine line = layout.createLine(); line.isValid(); line = layout.createLine()) {
        line.setLineWidth(width);
        out.append(text.mid(line.textStart(), line.textLength()).trimmed());
    }
    layout.endLayout();
    return out;
}

}
