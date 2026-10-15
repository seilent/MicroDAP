#include "ArtColor.h"

#include <QImageReader>
#include <QMetaObject>
#include <QSize>
#include <QUrl>
#include <QtMath>

#include <cmath>
#include <vector>

namespace microdap {

namespace {

struct Point
{
    double x = 0;
    double y = 0;
    double s = 0;
    double v = 0;
};

struct Group
{
    int count = 0;
    double weight = 0;
    double x = 0;
    double y = 0;
    double s = 0;
    double v = 0;
    double sumS = 0;
    double sumV = 0;

    void add(const Point &p)
    {
        ++count;
        weight += p.s;
        x += p.s * p.x;
        y += p.s * p.y;
        s += p.s * p.s;
        v += p.s * p.v;
        sumS += p.s;
        sumV += p.v;
    }

    void merge(const Group &other)
    {
        count += other.count;
        weight += other.weight;
        x += other.x;
        y += other.y;
        s += other.s;
        v += other.v;
        sumS += other.sumS;
        sumV += other.sumV;
    }

    Point center() const
    {
        if (weight <= 0)
            return {};
        return {x / weight, y / weight, s / weight, v / weight};
    }
};

double distance(const Point &a, const Point &b)
{
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;
    const double ds = a.s - b.s;
    const double dv = a.v - b.v;
    return dx * dx + dy * dy + ds * ds + dv * dv;
}

int nearest(const Point &p, const std::vector<Point> &centers)
{
    int best = 0;
    double bestDistance = distance(p, centers[0]);
    for (int c = 1; c < int(centers.size()); ++c) {
        const double d = distance(p, centers[c]);
        if (d < bestDistance) {
            bestDistance = d;
            best = c;
        }
    }
    return best;
}

double midFactor(double value)
{
    return 0.5 + 0.5 * qMax(0.0, 1.0 - qAbs(value - ArtColor::midValue) / ArtColor::midValue);
}

std::vector<Point> samplesOf(const QImage &source)
{
    std::vector<Point> samples;
    samples.reserve(size_t(source.width()) * size_t(source.height()));
    for (int y = 0; y < source.height(); ++y) {
        const auto *line = reinterpret_cast<const QRgb *>(source.constScanLine(y));
        for (int x = 0; x < source.width(); ++x) {
            float h = 0;
            float s = 0;
            float v = 0;
            QColor(line[x]).getHsvF(&h, &s, &v);
            if (h < 0 || s < ArtColor::neutralSaturation || v < ArtColor::darkValue
                || (v > ArtColor::whiteValue && s < ArtColor::whiteSaturation))
                continue;
            const double angle = qDegreesToRadians(double(h) * 360.0);
            samples.push_back({std::cos(angle), std::sin(angle), double(s), double(v)});
        }
    }
    return samples;
}

std::vector<Point> seeds(const std::vector<Point> &samples)
{
    const double split = ArtColor::mergeDistance * ArtColor::mergeDistance;
    size_t first = 0;
    for (size_t i = 1; i < samples.size(); ++i) {
        if (samples[i].s > samples[first].s)
            first = i;
    }
    std::vector<Point> centers{samples[first]};
    while (int(centers.size()) < ArtColor::clusters) {
        bool found = false;
        size_t best = 0;
        double bestScore = 0;
        for (size_t i = 0; i < samples.size(); ++i) {
            double d = distance(samples[i], centers[0]);
            for (size_t c = 1; c < centers.size(); ++c)
                d = qMin(d, distance(samples[i], centers[c]));
            if (d < split)
                continue;
            const double score = samples[i].s * d;
            if (!found || score > bestScore) {
                found = true;
                bestScore = score;
                best = i;
            }
        }
        if (!found)
            break;
        centers.push_back(samples[best]);
    }
    return centers;
}

std::vector<Group> groupsOf(const std::vector<Point> &samples, const std::vector<Point> &centers)
{
    std::vector<Group> groups(centers.size());
    for (const Point &p : samples)
        groups[size_t(nearest(p, centers))].add(p);
    return groups;
}

}

ArtColor::ArtColor(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
}

ArtColor::~ArtColor()
{
    m_pool.waitForDone();
}

int ArtColor::levelFor(double saturation)
{
    if (saturation < level2Saturation)
        return 1;
    if (saturation < level3Saturation)
        return 2;
    if (saturation < level4Saturation)
        return 3;
    return 4;
}

ArtPick ArtColor::pick(const QImage &image)
{
    if (image.isNull())
        return {};
    QImage source = image.size() == QSize(side, side)
                        ? image
                        : image.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    source = source.convertToFormat(QImage::Format_RGB32);
    const std::vector<Point> samples = samplesOf(source);
    const qint64 total = qint64(source.width()) * source.height();
    if (samples.empty() || qint64(samples.size()) * 100 < total * minPercent)
        return {};

    std::vector<Point> centers = seeds(samples);
    for (int pass = 0; pass < iterations; ++pass) {
        const std::vector<Group> groups = groupsOf(samples, centers);
        bool moved = false;
        for (size_t c = 0; c < centers.size(); ++c) {
            if (groups[c].weight <= 0)
                continue;
            const Point next = groups[c].center();
            if (distance(next, centers[c]) > 1e-12)
                moved = true;
            centers[c] = next;
        }
        if (!moved)
            break;
    }

    std::vector<Group> groups = groupsOf(samples, centers);
    const double merge = mergeDistance * mergeDistance;
    for (size_t i = 0; i < groups.size(); ++i) {
        if (groups[i].count == 0)
            continue;
        for (size_t j = i + 1; j < groups.size(); ++j) {
            if (groups[j].count == 0 || distance(groups[i].center(), groups[j].center()) >= merge)
                continue;
            groups[i].merge(groups[j]);
            groups[j] = Group();
        }
    }

    int best = -1;
    double bestScore = 0;
    for (int c = 0; c < int(groups.size()); ++c) {
        const Group &g = groups[size_t(c)];
        if (g.count == 0)
            continue;
        const double share = double(g.count) / double(samples.size());
        const double score = share * (g.sumS / g.count) * midFactor(g.sumV / g.count);
        if (best < 0 || score > bestScore) {
            best = c;
            bestScore = score;
        }
    }
    if (best < 0)
        return {};

    const Group &g = groups[size_t(best)];
    double degrees = qRadiansToDegrees(std::atan2(g.y, g.x));
    if (degrees < 0)
        degrees += 360.0;
    const double saturation = qBound(0.0, g.sumS / g.count, 1.0);
    const double value = qBound(0.0, g.sumV / g.count, 1.0);
    ArtPick result;
    result.hue = qRound(degrees) % 360;
    result.level = levelFor(saturation);
    result.color = QColor::fromHsvF(float(result.hue) / 360.0f, float(saturation), float(value));
    return result;
}

int ArtColor::hueOf(const QImage &image)
{
    return pick(image).hue;
}

QString ArtColor::localPath(const QString &path)
{
    if (!path.startsWith(QLatin1StringView("file:"), Qt::CaseInsensitive))
        return path;
    const QUrl url(path);
    return url.isLocalFile() ? url.toLocalFile() : QString();
}

ArtPick ArtColor::pickFile(const QString &path)
{
    const QString file = localPath(path);
    if (file.isEmpty())
        return {};
    QImageReader reader(file);
    reader.setScaledSize(QSize(side, side));
    const QImage image = reader.read();
    if (image.isNull())
        return {};
    return pick(image);
}

int ArtColor::hueOfFile(const QString &path)
{
    return pickFile(path).hue;
}

bool ArtColor::contains(const QString &path) const
{
    return m_cache.contains(path);
}

ArtPick ArtColor::cached(const QString &path) const
{
    return m_cache.value(path);
}

int ArtColor::cachedHue(const QString &path) const
{
    return cached(path).hue;
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
        emit found(path, hit->hue, hit->level, hit->color);
        return;
    }
    if (m_pending.contains(path))
        return;
    m_pending.insert(path);
    const int generation = m_generation;
    m_pool.start([this, path, generation] {
        const ArtPick result = pickFile(path);
        QMetaObject::invokeMethod(
            this,
            [this, path, generation, result] {
                m_pending.remove(path);
                if (generation == m_generation)
                    store(path, result);
                emit found(path, result.hue, result.level, result.color);
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

void ArtColor::store(const QString &path, const ArtPick &result)
{
    if (m_cache.size() >= cacheLimit)
        m_cache.clear();
    m_cache.insert(path, result);
}

}
