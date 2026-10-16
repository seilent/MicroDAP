#include "WirePlumberRestart.h"

#include "BluezModel.h"

#include <QDebug>

namespace microdap {

WirePlumberRestart::WirePlumberRestart(QObject *parent)
    : QObject(parent)
{
}

WirePlumberRestart::~WirePlumberRestart()
{
    dropProcess();
}

QString WirePlumberRestart::systemctlPath()
{
    const QString value = qEnvironmentVariable("MICRODAP_SYSTEMCTL_BIN");
    return value.isEmpty() ? QStringLiteral("systemctl") : value;
}

void WirePlumberRestart::setBluez(BluezModel *bluez)
{
    if (m_bluez == bluez)
        return;
    if (m_bluez)
        disconnect(m_bluez, nullptr, this, nullptr);
    m_bluez = bluez;
    if (m_bluez)
        connect(m_bluez, &QObject::destroyed, this, [this] { m_bluez = nullptr; });
}

BluezModel *WirePlumberRestart::bluez() const
{
    return m_bluez;
}

bool WirePlumberRestart::restarting() const
{
    return m_process != nullptr;
}

bool WirePlumberRestart::restartForNet(const QString &reason)
{
    if (m_process) {
        qInfo() << "microdap wireplumber: restart for" << reason << "joins the running restart";
        return true;
    }
    if (m_bluez && m_bluez->transportState() != QLatin1StringView("none")) {
        qInfo() << "microdap wireplumber: restart for" << reason << "refused, transport" << m_bluez->transportState();
        return false;
    }
    const QStringList args{QStringLiteral("--user"), QStringLiteral("restart"), QStringLiteral("wireplumber")};
    qInfo() << "microdap wireplumber: restart for" << reason << systemctlPath() << args;
    m_process = new QProcess(this);
    connect(m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        if (!ok)
            qWarning() << "microdap wireplumber: restart exit" << code;
        finish(ok);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        qWarning() << "microdap wireplumber: cannot start" << m_process->program();
        finish(false);
    });
    m_process->start(systemctlPath(), args);
    emit restartingChanged();
    return true;
}

void WirePlumberRestart::finish(bool ok)
{
    m_process->deleteLater();
    m_process = nullptr;
    emit restartingChanged();
    emit restartFinished(ok);
}

void WirePlumberRestart::dropProcess()
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

}
