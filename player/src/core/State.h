#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariantList>

class QSocketNotifier;

namespace casdap {

class Player;

class State : public QObject
{
    Q_OBJECT

public:
    explicit State(Player *player, QObject *parent = nullptr);
    State(Player *player, const QString &path, QObject *parent = nullptr);
    ~State() override;

    static QString defaultPath();
    QString path() const;

    Q_INVOKABLE bool saveNow();
    bool save(bool handoff);
    bool saveExit();
    void quit(bool handoff = true);
    bool load();
    void installSignalHandlers();

    Q_INVOKABLE QVariantList stack() const;
    Q_INVOKABLE void setStack(const QVariantList &stack);

    static QJsonObject snapshot(const Player *player, bool handoff, const QJsonArray &stack = {});
    static bool write(const QString &path, const QJsonObject &object);
    static bool read(const QString &path, QJsonObject &object);
    static bool loadInto(Player *player, const QString &path);
    static QJsonArray savedStack(const QString &path);

private:
    void onSignal();
    void onStatusChanged();

    Player *m_player;
    QString m_path;
    QTimer m_timer;
    QJsonArray m_stack;
    QSocketNotifier *m_notifier = nullptr;
    int m_fds[2] = {-1, -1};
};

}
