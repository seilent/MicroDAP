#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>
#include <QVariantMap>

namespace casdap {

class Env : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(QString fontDir READ fontDir CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(QVariantMap manifest READ manifest CONSTANT)
    Q_PROPERTY(int landscapeTurn READ landscapeTurn CONSTANT)
    Q_PROPERTY(bool rotateMotion READ rotateMotion CONSTANT)

public:
    explicit Env(QObject *parent = nullptr);

    static QString defaultFontDir();
    static QString defaultManifestFile();
    static QVariantMap readManifest(const QString &path);
    QString fontDir() const;
    QString version() const;
    QVariantMap manifest() const;
    int landscapeTurn() const;
    bool rotateMotion() const;
};

}
