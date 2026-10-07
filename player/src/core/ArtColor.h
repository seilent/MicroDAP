#pragma once

#include <QColor>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QThreadPool>

namespace casdap {

struct ArtPick
{
    int hue = -1;
    int level = 0;
    QColor color;

    bool valid() const { return hue >= 0; }
};

class ArtColor : public QObject
{
    Q_OBJECT

public:
    static constexpr int side = 48;
    static constexpr int clusters = 5;
    static constexpr int iterations = 10;
    static constexpr int minPercent = 3;
    static constexpr int cacheLimit = 512;
    static constexpr double neutralSaturation = 0.18;
    static constexpr double darkValue = 0.12;
    static constexpr double whiteValue = 0.97;
    static constexpr double whiteSaturation = 0.25;
    static constexpr double midValue = 0.6;
    static constexpr double mergeDistance = 0.2;
    static constexpr double level2Saturation = 0.35;
    static constexpr double level3Saturation = 0.55;
    static constexpr double level4Saturation = 0.75;

    explicit ArtColor(QObject *parent = nullptr);
    ~ArtColor() override;

    static ArtPick pick(const QImage &image);
    static ArtPick pickFile(const QString &path);
    static int hueOf(const QImage &image);
    static int hueOfFile(const QString &path);
    static int levelFor(double saturation);
    static QString localPath(const QString &path);

    bool contains(const QString &path) const;
    ArtPick cached(const QString &path) const;
    int cachedHue(const QString &path) const;
    int cacheSize() const;
    bool pending(const QString &path) const;
    void request(const QString &path);
    void clear();
    void waitForDone();

signals:
    void found(const QString &path, int hue, int level, const QColor &color);

private:
    void store(const QString &path, const ArtPick &result);

    QHash<QString, ArtPick> m_cache;
    QSet<QString> m_pending;
    int m_generation = 0;
    QThreadPool m_pool;
};

}
