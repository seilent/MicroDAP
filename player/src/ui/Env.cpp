#include "Env.h"

#include "SettingsRows.h"

#include <QCoreApplication>

namespace microdap {

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::defaultFontDir()
{
    return QStringLiteral("/usr/share/fonts/microset");
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

}
