#include "BtOffer.h"

#include "Settings.h"

#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace microdap {

namespace {

const QString autoOffer = QStringLiteral("auto");

QString envPath(const char *name, const QString &fallback)
{
    const QString value = qEnvironmentVariable(name);
    return value.isEmpty() ? fallback : value;
}

}

BtOffer::BtOffer(QObject *parent)
    : QObject(parent)
    , m_file(file())
    , m_applied(autoOffer)
{
    auto changed = [this] {
        watch();
        read();
    };
    connect(&m_watch, &QFileSystemWatcher::directoryChanged, this, changed);
    connect(&m_watch, &QFileSystemWatcher::fileChanged, this, changed);
    watch();
    read();
}

QString BtOffer::file()
{
    const QString dir = envPath("MICRODAP_RUNTIME_DIR", qEnvironmentVariable("XDG_RUNTIME_DIR"));
    return dir.isEmpty() ? QString() : dir + QStringLiteral("/microos-audio/bt-codecs");
}

bool BtOffer::pending(const QString &role, const QString &pick, const QString &applied)
{
    if (role == QLatin1StringView("headphones"))
        return applied != autoOffer && pick != applied && pick != QLatin1StringView("sbc");
    return pick != applied;
}

QString BtOffer::applied() const
{
    return m_applied;
}

void BtOffer::read()
{
    QString applied = autoOffer;
    QFile f(m_file);
    if (!m_file.isEmpty() && f.open(QIODevice::ReadOnly)) {
        const QStringList lines = QString::fromUtf8(f.read(4096)).split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            const QStringList parts = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
            if (parts.size() == 2 && parts.at(0) == QLatin1StringView("applied")
                && Settings::codecs().contains(parts.at(1)))
                applied = parts.at(1);
        }
    }
    if (applied == m_applied)
        return;
    m_applied = applied;
    emit appliedChanged();
}

void BtOffer::watch()
{
    if (m_file.isEmpty())
        return;
    QString dir = QFileInfo(m_file).absolutePath();
    while (!QFileInfo(dir).isDir() && dir != QFileInfo(dir).absolutePath())
        dir = QFileInfo(dir).absolutePath();
    QStringList want{dir};
    if (QFileInfo(m_file).isFile())
        want.push_back(m_file);
    QStringList have = m_watch.directories() + m_watch.files();
    std::sort(want.begin(), want.end());
    std::sort(have.begin(), have.end());
    if (have == want)
        return;
    if (!have.isEmpty())
        m_watch.removePaths(have);
    m_watch.addPaths(want);
}

}
