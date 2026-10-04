#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

namespace casdap {

class Env : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString fontDir READ fontDir CONSTANT)

public:
    explicit Env(QObject *parent = nullptr);

    static QString defaultFontDir();
    QString fontDir() const;
};

}
