#pragma once

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QTimer>

class QSocketNotifier;

namespace casdap {

class Keep;
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
    Keep *keep() const;
    void setKeep(Keep *keep);

    Q_INVOKABLE bool saveNow();
    bool saveExit();
    void quit();
    bool load();
    void installSignalHandlers();

    static QJsonObject snapshot(const Player *player, bool handoff);
    static bool write(const QString &path, const QJsonObject &object);
    static bool read(const QString &path, QJsonObject &object);
    static bool loadInto(Player *player, const QString &path);

private:
    bool save(bool handoff);
    void onSignal();
    void onStatusChanged();

    Player *m_player;
    QString m_path;
    Keep *m_keep = nullptr;
    QTimer m_timer;
    QSocketNotifier *m_notifier = nullptr;
    int m_fds[2] = {-1, -1};
};

}
