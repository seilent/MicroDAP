#include "Env.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace microdap {

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::defaultFontDir()
{
    return QStringLiteral("/usr/share/fonts/microdap");
}

QString Env::defaultManifestFile()
{
    return QStringLiteral("/usr/share/microdap/build-manifest");
}

QVariantMap Env::readManifest(const QString &path)
{
    QVariantMap out;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return out;
    QTextStream in(&file);
    while (!in.atEnd()) {
        const QString line = in.readLine();
        if (line.trimmed().isEmpty())
            break;
        const qsizetype eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        out.insert(line.left(eq), line.mid(eq + 1));
    }
    return out;
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

QVariantMap Env::manifest() const
{
    const QString path = qEnvironmentVariable("MICRODAP_MANIFEST_FILE");
    return readManifest(path.isEmpty() ? defaultManifestFile() : path);
}

int Env::landscapeTurn() const
{
    return qEnvironmentVariable("MICRODAP_LANDSCAPE_TURN") == QLatin1StringView("3") ? 3 : 1;
}

bool Env::rotateMotion() const
{
    return qEnvironmentVariable("MICRODAP_ROTATE_MOTION") != QLatin1StringView("0");
}

}
