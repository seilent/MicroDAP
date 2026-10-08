#include "Env.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>

namespace casdap {

Env::Env(QObject *parent)
    : QObject(parent)
{
}

QString Env::defaultFontDir()
{
    return QStringLiteral("/usr/share/fonts/casdap");
}

QString Env::defaultManifestFile()
{
    return QStringLiteral("/usr/share/casdap/build-manifest");
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
    const QString dir = qEnvironmentVariable("CASDAP_FONT_DIR");
    return dir.isEmpty() ? defaultFontDir() : dir;
}

QString Env::version() const
{
    return QCoreApplication::applicationVersion();
}

QVariantMap Env::manifest() const
{
    const QString path = qEnvironmentVariable("CASDAP_MANIFEST_FILE");
    return readManifest(path.isEmpty() ? defaultManifestFile() : path);
}

int Env::landscapeTurn() const
{
    return qEnvironmentVariable("CASDAP_LANDSCAPE_TURN") == QLatin1StringView("3") ? 3 : 1;
}

}
