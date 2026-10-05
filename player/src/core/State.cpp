#include "State.h"

#include "Player.h"
#include "Queue.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <atomic>
#include <csignal>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

namespace casdap {

namespace {

constexpr int saveIntervalMs = 10000;
constexpr qint64 handoffWindowS = 60;

std::atomic<int> signalFd{-1};

void handleSignal(int)
{
    const int fd = signalFd.load(std::memory_order_relaxed);
    if (fd < 0)
        return;
    const char byte = 1;
    const ssize_t written = ::write(fd, &byte, 1);
    (void)written;
}

QString statusName(Player::Status status)
{
    switch (status) {
    case Player::Playing:
        return QStringLiteral("playing");
    case Player::Paused:
        return QStringLiteral("paused");
    case Player::Stopped:
        break;
    }
    return QStringLiteral("stopped");
}

}

State::State(Player *player, QObject *parent)
    : State(player, defaultPath(), parent)
{
}

State::State(Player *player, const QString &path, QObject *parent)
    : QObject(parent)
    , m_player(player)
    , m_path(path)
{
    m_timer.setInterval(saveIntervalMs);
    connect(&m_timer, &QTimer::timeout, this, &State::saveNow);
    connect(m_player, &Player::statusChanged, this, &State::onStatusChanged);
    onStatusChanged();
    if (QCoreApplication *app = QCoreApplication::instance())
        connect(app, &QCoreApplication::aboutToQuit, this, &State::saveExit);
}

State::~State()
{
    if (m_fds[0] >= 0) {
        signalFd.store(-1, std::memory_order_relaxed);
        struct sigaction action {};
        action.sa_handler = SIG_DFL;
        sigemptyset(&action.sa_mask);
        ::sigaction(SIGTERM, &action, nullptr);
        ::sigaction(SIGINT, &action, nullptr);
        ::sigaction(SIGHUP, &action, nullptr);
        ::close(m_fds[0]);
    }
    if (m_fds[1] >= 0)
        ::close(m_fds[1]);
}

QString State::defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
           + QStringLiteral("/casdap/state.json");
}

QString State::path() const
{
    return m_path;
}

void State::onStatusChanged()
{
    if (m_player->status() == Player::Playing)
        m_timer.start();
    else
        m_timer.stop();
}

QJsonObject State::snapshot(const Player *player, bool handoff)
{
    const Queue *queue = player->queue();
    QJsonArray items;
    for (const QueueEntry &entry : queue->entries())
        items.append(QJsonObject::fromVariantMap(entry.toMap()));
    QJsonObject object;
    object.insert(QStringLiteral("queue"), items);
    object.insert(QStringLiteral("index"), queue->currentIndex());
    object.insert(QStringLiteral("position"), player->position());
    object.insert(QStringLiteral("shuffle"), queue->shuffle());
    object.insert(QStringLiteral("repeat"), static_cast<int>(queue->repeat()));
    object.insert(QStringLiteral("status"), statusName(player->status()));
    object.insert(QStringLiteral("savedAt"), QDateTime::currentSecsSinceEpoch());
    object.insert(QStringLiteral("handoff"), handoff);
    return object;
}

bool State::write(const QString &path, const QJsonObject &object)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(QJsonDocument(object).toJson(QJsonDocument::Compact));
    return file.commit();
}

bool State::read(const QString &path, QJsonObject &object)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return false;
    object = document.object();
    return true;
}

bool State::save(bool handoff)
{
    const bool ok = write(m_path, snapshot(m_player, handoff));
    if (!ok)
        qWarning() << "casdap state: cannot write" << m_path;
    return ok;
}

bool State::saveNow()
{
    return save(false);
}

bool State::saveExit()
{
    return save(true);
}

void State::quit()
{
    saveExit();
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    disconnect(app, &QCoreApplication::aboutToQuit, this, &State::saveExit);
    app->quit();
}

bool State::loadInto(Player *player, const QString &path)
{
    QJsonObject object;
    if (!read(path, object))
        return false;
    QList<QueueEntry> entries;
    for (const QJsonValue &value : object.value(QStringLiteral("queue")).toArray())
        entries.append(QueueEntry::fromMap(value.toObject().toVariantMap()));
    if (entries.isEmpty())
        return false;
    Queue *queue = player->queue();
    queue->setShuffle(object.value(QStringLiteral("shuffle")).toBool(false));
    const int repeat = object.value(QStringLiteral("repeat")).toInt(0);
    queue->setRepeat(static_cast<Queue::Repeat>(qBound(0, repeat, 2)));
    const int index = object.value(QStringLiteral("index")).toInt(0);
    const qint64 position = static_cast<qint64>(object.value(QStringLiteral("position")).toDouble(0));
    const qint64 savedAt = static_cast<qint64>(object.value(QStringLiteral("savedAt")).toDouble(0));
    const qint64 age = QDateTime::currentSecsSinceEpoch() - savedAt;
    const bool playing = object.value(QStringLiteral("handoff")).toBool(false)
                         && object.value(QStringLiteral("status")).toString()
                                == QLatin1String("playing")
                         && age < handoffWindowS;
    player->restore(entries, index, position, playing);
    return true;
}

bool State::load()
{
    return loadInto(m_player, m_path);
}

void State::installSignalHandlers()
{
    if (m_fds[0] >= 0)
        return;
    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, m_fds) != 0) {
        qWarning() << "casdap state: socketpair failed";
        m_fds[0] = -1;
        m_fds[1] = -1;
        return;
    }
    ::fcntl(m_fds[0], F_SETFL, ::fcntl(m_fds[0], F_GETFL) | O_NONBLOCK);
    signalFd.store(m_fds[0], std::memory_order_relaxed);

    m_notifier = new QSocketNotifier(m_fds[1], QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &State::onSignal);

    struct sigaction action {};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGTERM, &action, nullptr);
    ::sigaction(SIGINT, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
}

void State::onSignal()
{
    char byte = 0;
    const ssize_t count = ::read(m_fds[1], &byte, 1);
    (void)count;
    m_notifier->setEnabled(false);
    quit();
}

}
