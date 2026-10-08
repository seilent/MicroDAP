#include "GuideChord.h"

#include <QDateTime>
#include <QFile>
#include <QSaveFile>

#include <unistd.h>

namespace casdap {

GuideChord::GuideChord(QObject *parent)
    : QObject(parent)
    , m_dir(defaultDirectory())
{
}

QString GuideChord::defaultDirectory()
{
    const QString dir = qEnvironmentVariable("CASDAP_GUIDE_DIR");
    if (!dir.isEmpty())
        return dir;
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty())
        runtime = QStringLiteral("/run/user/") + QString::number(getuid());
    return runtime + QStringLiteral("/casdap-guide");
}

QString GuideChord::directory() const
{
    return m_dir;
}

bool GuideChord::take()
{
    QFile down(m_dir + QStringLiteral("/down"));
    if (!down.open(QIODevice::ReadOnly))
        return false;
    bool ok = false;
    const qint64 value = down.readAll().trimmed().toLongLong(&ok);
    down.close();
    if (!ok || value <= 0)
        return false;
    const qint64 age = QDateTime::currentMSecsSinceEpoch() - value;
    if (age < 0 || age > windowMs)
        return false;
    QSaveFile chord(m_dir + QStringLiteral("/chord"));
    if (!chord.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    chord.write(QByteArray::number(value) + '\n');
    return chord.commit();
}

}
