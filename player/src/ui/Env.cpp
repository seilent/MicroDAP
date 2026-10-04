#include "Env.h"

namespace casdap {

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::defaultFontDir()
{
    return QStringLiteral("/usr/share/fonts/casdap");
}

QString Env::fontDir() const
{
    const QString dir = qEnvironmentVariable("CASDAP_FONT_DIR");
    return dir.isEmpty() ? defaultFontDir() : dir;
}

}
