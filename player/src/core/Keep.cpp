#include "Keep.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace casdap {

namespace {

QString firstLine(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readLine()).trimmed();
}

}

QString Keep::markerPath()
{
    return qEnvironmentVariable("CASDAP_KEEP_FILE",
                                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/casdap/keep"));
}

QString Keep::bootIdPath()
{
    return qEnvironmentVariable("CASDAP_BOOT_ID_FILE", QStringLiteral("/proc/sys/kernel/random/boot_id"));
}

QString Keep::bootId()
{
    return firstLine(bootIdPath());
}

QString Keep::marker()
{
    return firstLine(markerPath());
}

bool Keep::write()
{
    const QString id = bootId();
    if (id.isEmpty()) {
        qWarning() << "casdap keep: no boot id in" << bootIdPath();
        return false;
    }
    const QString path = markerPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "casdap keep: cannot create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "casdap keep: cannot write" << path;
        return false;
    }
    file.write(id.toUtf8() + '\n');
    if (!file.commit()) {
        qWarning() << "casdap keep: cannot commit" << path;
        return false;
    }
    qInfo() << "casdap keep: marker written" << path;
    return true;
}

bool Keep::clear()
{
    const QString path = markerPath();
    if (!QFile::exists(path))
        return true;
    if (!QFile::remove(path)) {
        qWarning() << "casdap keep: cannot remove" << path;
        return false;
    }
    qInfo() << "casdap keep: marker removed" << path;
    return true;
}

}
