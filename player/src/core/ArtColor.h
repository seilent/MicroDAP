#pragma once

#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

namespace casdap {

class ArtColor : public QObject
{
    Q_OBJECT

public:
    static constexpr int side = 32;
    static constexpr int bins = 36;
    static constexpr int minPixels = 21;
    static constexpr int cacheLimit = 512;
    static constexpr double minSaturation = 0.25;
    static constexpr double minValue = 0.20;

    explicit ArtColor(QObject *parent = nullptr);
    ~ArtColor() override;

    static int hueOf(const QImage &image);
    static int hueOfFile(const QString &path);
    static QString localPath(const QString &path);

    bool contains(const QString &path) const;
    int cachedHue(const QString &path) const;
    int cacheSize() const;
    bool pending(const QString &path) const;
    void request(const QString &path);
    void clear();
    void waitForDone();

signals:
    void found(const QString &path, int hue);

private:
    void store(const QString &path, int hue);

    QHash<QString, int> m_cache;
    QSet<QString> m_pending;
    int m_generation = 0;
    QThreadPool m_pool;
};

}
