#pragma once

#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

namespace microdap {

class BtOffer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString applied READ applied NOTIFY appliedChanged)

public:
    explicit BtOffer(QObject *parent = nullptr);

    static QString file();
    static bool pending(const QString &role, const QString &pick, const QString &applied);
    QString applied() const;

signals:
    void appliedChanged();

private:
    void read();
    void watch();

    QFileSystemWatcher m_watch;
    QString m_file;
    QString m_applied;
};

}
