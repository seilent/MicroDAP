#include "Keep.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace microdap {

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
    return qEnvironmentVariable("MICRODAP_KEEP_FILE",
                                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/microdap/keep"));
}

QString Keep::bootIdPath()
{
    return qEnvironmentVariable("MICRODAP_BOOT_ID_FILE", QStringLiteral("/proc/sys/kernel/random/boot_id"));
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
        qWarning() << "microdap keep: no boot id in" << bootIdPath();
        return false;
    }
    const QString path = markerPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "microdap keep: cannot create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "microdap keep: cannot write" << path;
        return false;
    }
    file.write(id.toUtf8() + '\n');
    if (!file.commit()) {
        qWarning() << "microdap keep: cannot commit" << path;
        return false;
    }
    qInfo() << "microdap keep: marker written" << path;
    return true;
}

bool Keep::clear()
{
    const QString path = markerPath();
    if (!QFile::exists(path))
        return true;
    if (!QFile::remove(path)) {
        qWarning() << "microdap keep: cannot remove" << path;
        return false;
    }
    qInfo() << "microdap keep: marker removed" << path;
    return true;
}

}
