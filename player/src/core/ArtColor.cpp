#include "ArtColor.h"

#include <QColor>
#include <QImageReader>
#include <QMetaObject>
#include <QSize>

#include <array>

namespace casdap {

ArtColor::ArtColor(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

ArtColor::~ArtColor()
{
    m_pool.waitForDone();
}

int ArtColor::hueOf(const QImage &image)
{
    if (image.isNull())
        return -1;
    QImage source = image.size() == QSize(side, side)
                        ? image
                        : image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    source = source.convertToFormat(QImage::Format_RGB32);
    std::array<double, bins> weight{};
    int count = 0;
    for (int y = 0; y < source.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            float h = 0;
            float s = 0;
            float v = 0;
            QColor(line[x]).getHsvF(&h, &s, &v);
            if (h < 0 || s < minSaturation || v < minValue)
                continue;
            const int bin = qRound(h * bins) % bins;
            weight[bin] += double(s) * double(v);
            ++count;
        }
    }
    if (count < minPixels)
        return -1;
    int top = 0;
    for (int bin = 1; bin < bins; ++bin) {
        if (weight[bin] > weight[top])
            top = bin;
    }
    return top * 10;
}

int ArtColor::hueOfFile(const QString &path)
{
    QImageReader reader(path);
    reader.setScaledSize(QSize(side, side));
    const QImage image = reader.read();
    if (image.isNull())
        return -1;
    return hueOf(image.convertToFormat(QImage::Format_RGB32));
}

bool ArtColor::contains(const QString &path) const
{
    return m_cache.contains(path);
}

int ArtColor::cachedHue(const QString &path) const
{
    return m_cache.value(path, -1);
}

int ArtColor::cacheSize() const
{
    return int(m_cache.size());
}

bool ArtColor::pending(const QString &path) const
{
    return m_pending.contains(path);
}

void ArtColor::request(const QString &path)
{
    if (path.isEmpty())
        return;
    const auto hit = m_cache.constFind(path);
    if (hit != m_cache.constEnd()) {
        emit found(path, hit.value());
        return;
    }
    if (m_pending.contains(path))
        return;
    m_pending.insert(path);
    const int generation = m_generation;
    m_pool.start([this, path, generation] {
        const int hue = hueOfFile(path);
        QMetaObject::invokeMethod(
            this,
            [this, path, generation, hue] {
                m_pending.remove(path);
                if (generation == m_generation)
                    store(path, hue);
                emit found(path, hue);
            },
            Qt::QueuedConnection);
    });
}

void ArtColor::clear()
{
    m_cache.clear();
    m_pending.clear();
    ++m_generation;
}

void ArtColor::waitForDone()
{
    m_pool.waitForDone();
}

void ArtColor::store(const QString &path, int hue)
{
    if (m_cache.size() >= cacheLimit)
        m_cache.clear();
    m_cache.insert(path, hue);
}

}
