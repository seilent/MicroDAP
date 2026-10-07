#include "CodecFragment.h"

#include "BluezModel.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace casdap {

namespace {

QString readAll(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};
    return QString::fromUtf8(file.readAll());
}

}

CodecFragment::CodecFragment(QObject *parent)
    : QObject(parent)
    , m_codec(QStringLiteral("auto"))
{
}

CodecFragment::~CodecFragment()
{
    dropProcess();
}

QStringList CodecFragment::codecs()
{
    return {QStringLiteral("auto"), QStringLiteral("aptx"), QStringLiteral("aac"),
            QStringLiteral("ldac"), QStringLiteral("sbc")};
}

QString CodecFragment::confDir()
{
    return qEnvironmentVariable("CASDAP_WP_CONF_DIR",
                                QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
                                    + QStringLiteral("/wireplumber/wireplumber.conf.d"));
}

QString CodecFragment::confPath()
{
    return confDir() + QStringLiteral("/50-casdap-codec.conf");
}

QString CodecFragment::systemctlPath()
{
    const QString value = qEnvironmentVariable("CASDAP_SYSTEMCTL_BIN");
    return value.isEmpty() ? QStringLiteral("systemctl") : value;
}

QStringList CodecFragment::codecsFor(const QString &codec)
{
    if (codec == QLatin1StringView("aptx"))
        return {QStringLiteral("sbc"), QStringLiteral("aptx"), QStringLiteral("aptx_hd")};
    if (codec == QLatin1StringView("aac"))
        return {QStringLiteral("sbc"), QStringLiteral("aac")};
    if (codec == QLatin1StringView("ldac"))
        return {QStringLiteral("sbc"), QStringLiteral("ldac")};
    if (codec == QLatin1StringView("sbc"))
        return {QStringLiteral("sbc")};
    return {};
}

QString CodecFragment::fragmentText(const QString &codec)
{
    const QStringList list = codecsFor(codec);
    if (list.isEmpty())
        return {};
    return QStringLiteral("monitor.bluez.properties = {\n    bluez5.codecs = [ ")
           + list.join(QLatin1Char(' ')) + QStringLiteral(" ]\n}\n");
}

void CodecFragment::setBluez(BluezModel *bluez)
{
    if (m_bluez == bluez)
        return;
    if (m_bluez)
        disconnect(m_bluez, nullptr, this, nullptr);
    m_bluez = bluez;
    if (!m_bluez)
        return;
    connect(m_bluez, &QObject::destroyed, this, [this] { m_bluez = nullptr; });
    connect(m_bluez, &BluezModel::transportActiveChanged, this, &CodecFragment::onTransportActiveChanged);
}

void CodecFragment::onTransportActiveChanged()
{
    if (!m_bluez || m_bluez->transportActive() || !m_pending || m_process)
        return;
    qInfo() << "casdap codec: transport ended, applying" << m_codec;
    restart();
}

BluezModel *CodecFragment::bluez() const
{
    return m_bluez;
}

QString CodecFragment::codec() const
{
    return m_codec;
}

bool CodecFragment::pending() const
{
    return m_pending;
}

bool CodecFragment::restarting() const
{
    return m_process != nullptr;
}

bool CodecFragment::apply(const QString &codec)
{
    if (!codecs().contains(codec)) {
        qWarning() << "casdap codec: unknown codec" << codec;
        return false;
    }
    if (m_codec != codec) {
        m_codec = codec;
        emit codecChanged();
    }
    const QString path = confPath();
    const QString wanted = fragmentText(codec);
    const bool exists = QFile::exists(path);
    if (wanted.isEmpty() ? !exists : exists && readAll(path) == wanted) {
        qInfo() << "casdap codec:" << codec << "already in place";
        return true;
    }
    const bool written = wanted.isEmpty() ? removeFragment() : writeFragment(wanted);
    if (!written)
        return false;
    if (m_bluez && m_bluez->transportActive()) {
        qInfo() << "casdap codec:" << codec << "written, transport active, applies at the next wireplumber start";
        setPending(true);
        return true;
    }
    restart();
    return true;
}

bool CodecFragment::writeFragment(const QString &text)
{
    const QString path = confPath();
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) {
        qWarning() << "casdap codec: cannot create" << QFileInfo(path).absolutePath();
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        qWarning() << "casdap codec: cannot write" << path;
        return false;
    }
    file.write(text.toUtf8());
    if (!file.commit()) {
        qWarning() << "casdap codec: cannot commit" << path;
        return false;
    }
    qInfo() << "casdap codec: fragment written" << path;
    return true;
}

bool CodecFragment::removeFragment()
{
    const QString path = confPath();
    if (!QFile::remove(path)) {
        qWarning() << "casdap codec: cannot remove" << path;
        return false;
    }
    qInfo() << "casdap codec: fragment removed" << path;
    return true;
}

void CodecFragment::restart()
{
    dropProcess();
    const QStringList args{QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("wireplumber")};
    qInfo() << "casdap codec: restart" << systemctlPath() << args;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok)
            qWarning() << "casdap codec: wireplumber restart exit" << code;
        m_process->deleteLater();
        m_process = nullptr;
        setPending(!ok);
        emit restartingChanged();
        emit restartFinished(ok);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "casdap codec: cannot start" << m_process->program();
        m_process->deleteLater();
        m_process = nullptr;
        setPending(true);
        emit restartingChanged();
        emit restartFinished(false);
    });
    m_process->start(systemctlPath(), args);
    emit restartingChanged();
}

void CodecFragment::dropProcess()
{
    if (!m_process)
        return;
    m_process->disconnect(this);
    if (m_process->state() != QProcess::NotRunning) {
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    m_process->deleteLater();
    m_process = nullptr;
}

void CodecFragment::setPending(bool pending)
{
    if (m_pending == pending)
        return;
    m_pending = pending;
    emit pendingChanged();
}

}
