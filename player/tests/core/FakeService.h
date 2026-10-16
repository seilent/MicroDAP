#pragma once

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusVariant>
#include <QElapsedTimer>
#include <QProcess>
#include <QString>
#include <QStringList>
#include <QTest>
#include <QVariant>

class FakeService
{
public:
    static constexpr int timeoutMs = 5000;

    FakeService(QString script, QString service, QString path, QString interface)
        : m_script(std::move(script))
        , m_service(std::move(service))
        , m_path(std::move(path))
        , m_interface(std::move(interface))
    {
    }

    ~FakeService() { stop(); }

    static FakeService microSet(const QString &script)
    {
        return FakeService(script, QStringLiteral("net.seilent.MicroSet"), QStringLiteral("/net/seilent/MicroDap/FakeMicroSet"),
                           QStringLiteral("net.seilent.MicroDap.FakeMicroSet"));
    }

    static FakeService powerd(const QString &script)
    {
        return FakeService(script, QStringLiteral("net.seilent.MicroOs.Power"), QStringLiteral("/net/seilent/MicroDap/FakePowerd"),
                           QStringLiteral("net.seilent.MicroDap.FakePowerd"));
    }

    bool start(const QStringList &args = {})
    {
        stop();
        m_process.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_process.start(QStringLiteral("python3"), QStringList{m_script} + args);
        if (!m_process.waitForStarted(timeoutMs))
            return false;
        QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs) {
            if (bus->isServiceRegistered(m_service))
                return true;
            if (m_process.state() != QProcess::Running)
                return false;
            QTest::qWait(20);
        }
        return false;
    }

    void stop()
    {
        if (m_process.state() == QProcess::NotRunning)
            return;
        call(QStringLiteral("Quit"));
        if (!m_process.waitForFinished(timeoutMs)) {
            m_process.terminate();
            m_process.waitForFinished(timeoutMs);
        }
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < timeoutMs && QDBusConnection::sessionBus().interface()->isServiceRegistered(m_service))
            QTest::qWait(20);
    }

    bool waitExit()
    {
        return m_process.state() == QProcess::NotRunning || m_process.waitForFinished(timeoutMs);
    }

    QDBusMessage call(const QString &method, const QVariantList &args = {}) const
    {
        QDBusMessage message = QDBusMessage::createMethodCall(m_service, m_path, m_interface, method);
        message.setArguments(args);
        return QDBusConnection::sessionBus().call(message, QDBus::Block, timeoutMs);
    }

    QVariant value(const QString &method, const QVariantList &args = {}) const
    {
        return call(method, args).arguments().value(0);
    }

    QStringList calls() const { return value(QStringLiteral("Calls")).toStringList(); }

    static QVariant variant(const QVariant &value) { return QVariant::fromValue(QDBusVariant(value)); }

private:
    QString m_script;
    QString m_service;
    QString m_path;
    QString m_interface;
    QProcess m_process;
};
