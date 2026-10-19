#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantList>

namespace microdap {

class Env : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString fontDir READ fontDir CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(int landscapeTurn READ landscapeTurn CONSTANT)
    Q_PROPERTY(bool rotateMotion READ rotateMotion CONSTANT)
    Q_PROPERTY(QVariantList settingsRows READ settingsRows CONSTANT)

public:
    explicit Env(QObject *parent = nullptr);

    static QString defaultFontDir();
    QString fontDir() const;
    QString version() const;
    int landscapeTurn() const;
    bool rotateMotion() const;
    QVariantList settingsRows() const;
};

}
