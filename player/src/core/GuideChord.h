#pragma once

#include <QObject>
#include <QString>

namespace casdap {

class GuideChord : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 windowMs = 500;

    explicit GuideChord(QObject *parent = nullptr);

    static QString defaultDirectory();
    QString directory() const;

    Q_INVOKABLE bool take();

private:
    QString m_dir;
};

}
