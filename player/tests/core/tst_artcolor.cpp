#include "AccentControl.h"
#include "ArtColor.h"
#include "Queue.h"
#include "RgbControl.h"
#include "Settings.h"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;
constexpr int side = ArtColor::side;

QImage solid(const QColor &color)
{
    QImage image(side, side, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

QColor tone(int hue, double saturation, double value)
{
    return QColor::fromHsvF(float(hue) / 360.0f, float(saturation), float(value));
}

QImage hsvSolid(int hue, double saturation, double value)
{
    return solid(tone(hue, saturation, value));
}

QImage spots(const QColor &base, const QColor &spot, int pixels)
{
    QImage image = solid(base);
    for (int i = 0; i < pixels; ++i)
        image.setPixelColor(i % side, i / side, spot);
    return image;
}

QImage logo(const QColor &base, const QColor &spot, int size)
{
    QImage image = solid(base);
    const int origin = (side - size) / 2;
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x)
            image.setPixelColor(origin + x, origin + y, spot);
    }
    return image;
}

QImage rows(const QList<std::pair<QColor, int>> &bands)
{
    QImage image = solid(QColor(0, 0, 0));
    int y = 0;
    for (const auto &[color, count] : bands) {
        for (int r = 0; r < count && y < side; ++r, ++y) {
            for (int x = 0; x < side; ++x)
                image.setPixelColor(x, y, color);
        }
    }
    return image;
}

QImage greyscale()
{
    QImage image(side, side, QImage::Format_RGB32);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x) {
            const int g = (x + y * side) * 255 / (side * side - 1);
            image.setPixelColor(x, y, QColor(g, g, g));
        }
    }
    return image;
}

struct Cover
{
    QString name;
    QImage image;
    int hue;
    int level;
};

QList<Cover> covers()
{
    const QColor black(0, 0, 0);
    const QColor white(255, 255, 255);
    return {
        {QStringLiteral("neon red"), hsvSolid(0, 1.0, 1.0), 0, 4},
        {QStringLiteral("pastel red"), hsvSolid(0, 0.25, 0.95), 0, 1},
        {QStringLiteral("orange"), hsvSolid(30, 0.6, 0.9), 30, 3},
        {QStringLiteral("yellow"), hsvSolid(60, 0.45, 0.85), 60, 2},
        {QStringLiteral("green"), hsvSolid(120, 0.9, 0.7), 120, 4},
        {QStringLiteral("teal"), hsvSolid(170, 0.5, 0.6), 170, 2},
        {QStringLiteral("pastel cyan"), hsvSolid(190, 0.3, 0.9), 190, 1},
        {QStringLiteral("blue"), hsvSolid(220, 0.8, 0.5), 220, 4},
        {QStringLiteral("indigo"), hsvSolid(250, 0.65, 0.8), 250, 3},
        {QStringLiteral("purple"), hsvSolid(280, 0.4, 0.7), 280, 2},
        {QStringLiteral("neon magenta"), hsvSolid(310, 1.0, 0.9), 310, 4},
        {QStringLiteral("pastel pink"), hsvSolid(340, 0.2, 0.96), 340, 1},
        {QStringLiteral("dark red"), hsvSolid(0, 0.9, 0.3), 0, 4},
        {QStringLiteral("dark navy"), hsvSolid(230, 0.7, 0.25), 230, 3},
        {QStringLiteral("neon green"), hsvSolid(100, 1.0, 1.0), 100, 4},
        {QStringLiteral("pastel lavender"), hsvSolid(260, 0.3, 0.95), 260, 1},
        {QStringLiteral("black with a logo"), logo(black, tone(20, 1.0, 1.0), 10), 20, 4},
        {QStringLiteral("black with a small logo"), logo(black, tone(20, 1.0, 1.0), 8), -1, 0},
        {QStringLiteral("white with a logo"), logo(white, tone(210, 0.85, 0.9), 12), 210, 4},
        {QStringLiteral("white with a small logo"), logo(white, tone(210, 0.85, 0.9), 8), -1, 0},
        {QStringLiteral("greyscale"), greyscale(), -1, 0},
        {QStringLiteral("grey"), solid(QColor(128, 128, 128)), -1, 0},
        {QStringLiteral("teal over red"), rows({{tone(180, 0.7, 0.7), 29}, {tone(0, 0.9, 0.9), 19}}), 180, 3},
        {QStringLiteral("black white purple"),
         rows({{black, 34}, {white, 9}, {tone(270, 0.6, 0.5), 5}}), 270, 3},
        {QStringLiteral("neon blue"), hsvSolid(240, 1.0, 1.0), 240, 4},
        {QStringLiteral("pastel blue"), hsvSolid(240, 0.28, 0.95), 240, 1},
        {QStringLiteral("neon red over dull blue"), rows({{tone(220, 0.3, 0.5), 29}, {tone(0, 1.0, 1.0), 19}}), 0, 4},
    };
}

}

class ArtColorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void solidColors();
    void neutral();
    void levels();
    void logoThreshold();
    void saturationOverShare();
    void tieGoesToFirstSeed();
    void deterministic();
    void utilization();
    void fileUrl();
    void trackSequence();
    void unreadableFile();
    void fileScaled();
    void cacheHit();
    void cacheLimit();
    void accentColors();
    void brightColors();
    void artAccent();
    void migration();
    void effectiveHue();
    void logLines();
    void staleResultCached();
    void phoneActive();

private:
    QString save(const QString &name, const QImage &image);

    QTemporaryDir m_dir;
};

void ArtColorTest::initTestCase()
{
    QVERIFY(m_dir.isValid());
}

QString ArtColorTest::save(const QString &name, const QImage &image)
{
    const QString path = m_dir.path() + QLatin1Char('/') + name;
    if (!image.save(path, "PNG"))
        return {};
    return path;
}

void ArtColorTest::solidColors()
{
    const ArtPick red = ArtColor::pick(solid(QColor(255, 0, 0)));
    QVERIFY(red.valid());
    QCOMPARE(red.hue, 0);
    QCOMPARE(red.level, 4);
    QCOMPARE(red.color.hsvHue(), 0);
    QCOMPARE(ArtColor::hueOf(solid(QColor(255, 0, 20))), 355);
    QCOMPARE(ArtColor::hueOf(solid(QColor(0x1B, 0xA1, 0xE2))), 200);
    QCOMPARE(ArtColor::pick(solid(QColor(0x1B, 0xA1, 0xE2))).level, 4);
    QCOMPARE(ArtColor::hueOf(solid(QColor(40, 0, 0))), 0);
}

void ArtColorTest::neutral()
{
    QCOMPARE(ArtColor::hueOf(QImage()), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(128, 128, 128))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(140, 120, 120))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(0, 0, 0))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(255, 255, 255))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(255, 204, 204))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(20, 0, 0))), -1);
    QCOMPARE(ArtColor::hueOf(greyscale()), -1);
    QVERIFY(!ArtColor::pick(solid(QColor(128, 128, 128))).color.isValid());
}

void ArtColorTest::levels()
{
    QCOMPARE(ArtColor::levelFor(0.18), 1);
    QCOMPARE(ArtColor::levelFor(0.349), 1);
    QCOMPARE(ArtColor::levelFor(0.35), 2);
    QCOMPARE(ArtColor::levelFor(0.549), 2);
    QCOMPARE(ArtColor::levelFor(0.55), 3);
    QCOMPARE(ArtColor::levelFor(0.749), 3);
    QCOMPARE(ArtColor::levelFor(0.75), 4);
    QCOMPARE(ArtColor::levelFor(1.0), 4);
}

void ArtColorTest::logoThreshold()
{
    const QColor red(255, 0, 0);
    for (const QColor &base : {QColor(0, 0, 0), QColor(255, 255, 255), QColor(128, 128, 128)}) {
        QCOMPARE(ArtColor::hueOf(spots(base, red, 0)), -1);
        QCOMPARE(ArtColor::hueOf(spots(base, red, 69)), -1);
        QCOMPARE(ArtColor::hueOf(spots(base, red, 70)), 0);
        QCOMPARE(ArtColor::pick(spots(base, red, 70)).level, 4);
    }
}

void ArtColorTest::saturationOverShare()
{
    const ArtPick pick = ArtColor::pick(rows({{tone(220, 0.3, 0.5), 29}, {tone(0, 1.0, 1.0), 19}}));
    QCOMPARE(pick.hue, 0);
    QCOMPARE(pick.level, 4);

    const ArtPick teal = ArtColor::pick(rows({{tone(180, 0.7, 0.7), 29}, {tone(0, 0.9, 0.9), 19}}));
    QCOMPARE(teal.hue, 180);
    QCOMPARE(teal.level, 3);
}

void ArtColorTest::tieGoesToFirstSeed()
{
    QImage image(side, side, QImage::Format_RGB32);
    for (int y = 0; y < side; ++y) {
        for (int x = 0; x < side; ++x)
            image.setPixelColor(x, y, y < side / 2 ? QColor(0, 0, 255) : QColor(0, 255, 0));
    }
    QCOMPARE(ArtColor::hueOf(image), 240);
}

void ArtColorTest::deterministic()
{
    for (const Cover &cover : covers()) {
        const ArtPick a = ArtColor::pick(cover.image);
        const ArtPick b = ArtColor::pick(cover.image);
        QCOMPARE(a.hue, b.hue);
        QCOMPARE(a.level, b.level);
        QCOMPARE(a.color, b.color);
    }
}

void ArtColorTest::utilization()
{
    const QList<Cover> set = covers();
    QVERIFY(set.size() >= 24);
    QSet<QString> words;
    QSet<int> levelsUsed;
    for (const Cover &cover : set) {
        const ArtPick pick = ArtColor::pick(cover.image);
        if (cover.hue < 0) {
            QVERIFY2(!pick.valid(), qPrintable(cover.name));
            continue;
        }
        QVERIFY2(pick.valid(), qPrintable(cover.name));
        QVERIFY2(pick.hue >= 0 && pick.hue < 360, qPrintable(cover.name));
        QVERIFY2(Settings::normalizeHue(pick.hue) == cover.hue,
                 qPrintable(QStringLiteral("%1: hue %2").arg(cover.name).arg(pick.hue)));
        QVERIFY2(pick.level == cover.level,
                 qPrintable(QStringLiteral("%1: level %2").arg(cover.name).arg(pick.level)));
        QVERIFY2(AccentControl::luminance(AccentControl::artAccentFor(pick.hue, pick.color))
                     >= AccentControl::minLuminance - 1e-9,
                 qPrintable(cover.name));
        words.insert(RgbControl::colorWord(pick.hue, pick.level));
        levelsUsed.insert(pick.level);
    }
    QVERIFY2(words.size() >= 16, qPrintable(QStringLiteral("%1 words").arg(words.size())));
    QCOMPARE(levelsUsed.size(), 4);

    const ArtPick neon = ArtColor::pick(hsvSolid(240, 1.0, 1.0));
    const ArtPick pastel = ArtColor::pick(hsvSolid(240, 0.28, 0.95));
    QCOMPARE(neon.hue, pastel.hue);
    QVERIFY(neon.level != pastel.level);
    QVERIFY(RgbControl::colorWord(neon.hue, neon.level) != RgbControl::colorWord(pastel.hue, pastel.level));
}

void ArtColorTest::fileUrl()
{
    const QString path = save(QStringLiteral("url cyan.png"), solid(QColor(0x1B, 0xA1, 0xE2)));
    QVERIFY(!path.isEmpty());
    const QString url = QUrl::fromLocalFile(path).toString();
    QVERIFY(url.startsWith(QLatin1StringView("file://")));
    QCOMPARE(ArtColor::localPath(url), path);
    QCOMPARE(ArtColor::localPath(path), path);
    QCOMPARE(ArtColor::hueOfFile(url), 200);
    QCOMPARE(ArtColor::hueOfFile(path), 200);

    AccentControl accent;
    accent.setFromArt(true);
    accent.setArtPath(url);
    QTRY_COMPARE_WITH_TIMEOUT(accent.hue(), 200, waitMs);
    QCOMPARE(accent.level(), 4);
}

void ArtColorTest::trackSequence()
{
    const QString red = save(QStringLiteral("track-red.png"), solid(QColor(255, 0, 0)));
    const QString blue = save(QStringLiteral("track-blue.png"), solid(QColor(0, 0, 255)));
    QVERIFY(!red.isEmpty());
    QVERIFY(!blue.isEmpty());

    QList<QueueEntry> entries;
    for (const QString &art : {red, blue, QString()}) {
        QueueEntry entry;
        entry.id = entries.size() + 1;
        entry.path = QStringLiteral("/nonexistent/t%1.flac").arg(entry.id);
        entry.title = QStringLiteral("t%1").arg(entry.id);
        entry.artPath = art;
        entries.append(entry);
    }

    Queue queue;
    AccentControl accent;
    accent.setSliderHue(280);
    accent.setFromArt(true);
    auto follow = [&queue, &accent] {
        accent.setArtPath(queue.get(queue.currentIndex()).value(QStringLiteral("artPath")).toString());
    };
    connect(&queue, &Queue::indexChanged, &accent, follow);
    connect(&queue, &QAbstractItemModel::modelReset, &accent, follow);
    QSignalSpy hueSpy(&accent, &AccentControl::hueChanged);

    queue.set(entries, 0);
    QCOMPARE(accent.artPath(), red);
    QTRY_COMPARE_WITH_TIMEOUT(accent.hue(), 0, waitMs);
    QCOMPARE(accent.artHue(), 0);
    QCOMPARE(accent.level(), 4);
    QCOMPARE(accent.color(), AccentControl::artAccentFor(0, accent.artTone()));
    QCOMPARE(accent.color(), QColor(250, 0, 0));

    queue.setCurrentIndex(1);
    QCOMPARE(accent.artPath(), blue);
    QTRY_COMPARE_WITH_TIMEOUT(accent.hue(), 240, waitMs);
    QCOMPARE(accent.color(), AccentControl::accentFor(240));

    queue.setCurrentIndex(2);
    QCOMPARE(accent.artPath(), QString());
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 280);
    QCOMPARE(accent.level(), 0);
    QCOMPARE(accent.color(), AccentControl::accentFor(280));

    queue.setCurrentIndex(0);
    QCOMPARE(accent.hue(), 0);
    queue.setCurrentIndex(1);
    QCOMPARE(accent.hue(), 240);
    QCOMPARE(hueSpy.count(), 5);
}

void ArtColorTest::unreadableFile()
{
    QCOMPARE(ArtColor::hueOfFile(m_dir.path() + QStringLiteral("/missing.png")), -1);
    const QString junk = m_dir.path() + QStringLiteral("/junk.png");
    QFile file(junk);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("not an image");
    file.close();
    QCOMPARE(ArtColor::hueOfFile(junk), -1);
    QVERIFY(!ArtColor::pickFile(junk).valid());
}

void ArtColorTest::fileScaled()
{
    QImage big(300, 200, QImage::Format_RGB32);
    big.fill(QColor(0x1B, 0xA1, 0xE2));
    const QString path = save(QStringLiteral("big.png"), big);
    QVERIFY(!path.isEmpty());
    QCOMPARE(ArtColor::hueOfFile(path), 200);
    QCOMPARE(ArtColor::pick(big).hue, 200);
}

void ArtColorTest::cacheHit()
{
    ArtColor art;
    QSignalSpy found(&art, &ArtColor::found);
    const QString path = save(QStringLiteral("red.png"), solid(QColor(255, 0, 0)));
    QVERIFY(!path.isEmpty());
    QVERIFY(!art.contains(path));
    art.request(path);
    QVERIFY(art.pending(path));
    art.request(path);
    QTRY_COMPARE_WITH_TIMEOUT(found.count(), 1, waitMs);
    QCOMPARE(found.at(0).at(0).toString(), path);
    QCOMPARE(found.at(0).at(1).toInt(), 0);
    QCOMPARE(found.at(0).at(2).toInt(), 4);
    QVERIFY(found.at(0).at(3).value<QColor>().isValid());
    QVERIFY(art.contains(path));
    QVERIFY(!art.pending(path));
    QCOMPARE(art.cachedHue(path), 0);
    QCOMPARE(art.cached(path).level, 4);

    QVERIFY(QFile::remove(path));
    art.request(path);
    QCOMPARE(found.count(), 2);
    QCOMPARE(found.at(1).at(1).toInt(), 0);
    QVERIFY(!art.pending(path));

    art.clear();
    QVERIFY(!art.contains(path));
    art.request(path);
    QTRY_COMPARE_WITH_TIMEOUT(found.count(), 3, waitMs);
    QCOMPARE(found.at(2).at(1).toInt(), -1);
    QCOMPARE(found.at(2).at(2).toInt(), 0);
    QVERIFY(art.contains(path));
    QCOMPARE(art.cachedHue(path), -1);
}

void ArtColorTest::cacheLimit()
{
    ArtColor art;
    QSignalSpy found(&art, &ArtColor::found);
    for (int i = 0; i < ArtColor::cacheLimit; ++i)
        art.request(m_dir.path() + QStringLiteral("/none-%1.png").arg(i));
    QTRY_COMPARE_WITH_TIMEOUT(found.count(), ArtColor::cacheLimit, waitMs * 4);
    QCOMPARE(art.cacheSize(), ArtColor::cacheLimit);
    art.request(m_dir.path() + QStringLiteral("/none-extra.png"));
    QTRY_COMPARE_WITH_TIMEOUT(found.count(), ArtColor::cacheLimit + 1, waitMs);
    QCOMPARE(art.cacheSize(), 1);
}

void ArtColorTest::accentColors()
{
    QCOMPARE(AccentControl::accentFor(350).name(), QStringLiteral("#fa2a4d"));
    QCOMPARE(AccentControl::saturationFor(350), 0.83);
    for (int hue = 0; hue < 360; hue += 10) {
        const QColor color = AccentControl::accentFor(hue);
        const double s = AccentControl::saturationFor(hue);
        QVERIFY2(AccentControl::luminance(color) >= AccentControl::minLuminance - 1e-9 || qFuzzyCompare(s, 0.40),
                 qPrintable(QStringLiteral("hue %1").arg(hue)));
        QVERIFY(s <= 0.83 && s >= 0.40);
        if (s < 0.83) {
            const double above = (qRound(s * 100) + 1) / 100.0;
            QVERIFY2(AccentControl::luminance(AccentControl::hsv(hue, above, AccentControl::accentValue))
                         < AccentControl::minLuminance,
                     qPrintable(QStringLiteral("hue %1").arg(hue)));
        }
    }
    QCOMPARE(AccentControl::saturationFor(0), 0.83);
    QCOMPARE(AccentControl::saturationFor(120), 0.83);
    QVERIFY(AccentControl::saturationFor(240) < 0.83);
    QCOMPARE(AccentControl::accentFor(198), AccentControl::accentFor(200));
    AccentControl accent;
    QCOMPARE(accent.colorFor(350), AccentControl::accentFor(350));
}

void ArtColorTest::brightColors()
{
    QCOMPARE(AccentControl::brightFor(350).name(), QStringLiteral("#ff4d6b"));
    AccentControl accent;
    QCOMPARE(accent.brightColor(), AccentControl::brightFor(350));
}

void ArtColorTest::artAccent()
{
    QCOMPARE(AccentControl::artAccentFor(0, tone(0, 1.0, 1.0)), QColor(250, 0, 0));
    QCOMPARE(AccentControl::artAccentFor(240, tone(240, 1.0, 1.0)), AccentControl::accentFor(240));
    QCOMPARE(AccentControl::artAccentFor(0, tone(0, 0.25, 0.95)), AccentControl::hsv(0, 0.25, 0.95));
    QCOMPARE(AccentControl::artBrightFor(0, tone(0, 0.25, 0.95)), AccentControl::hsv(0, 0.84 * 0.25, 1.0));
    QVERIFY(AccentControl::artAccentFor(0, tone(0, 0.25, 0.95)) != AccentControl::artAccentFor(0, tone(0, 1.0, 1.0)));

    const QColor navy = AccentControl::artAccentFor(230, tone(230, 0.7, 0.25));
    QVERIFY(AccentControl::luminance(navy) >= AccentControl::minLuminance);
    QVERIFY(qAbs(navy.hsvHue() - 230) <= 2);
    const QColor dark = AccentControl::artAccentFor(0, tone(0, 0.9, 0.3));
    QVERIFY(AccentControl::luminance(dark) >= AccentControl::minLuminance);
    QCOMPARE(dark.hsvHue(), 0);
    QVERIFY(dark.valueF() <= float(AccentControl::accentValue) + 0.01f);

    for (int hue = 0; hue < 360; hue += 5) {
        for (double s : {0.2, 0.5, 0.8, 1.0}) {
            for (double v : {0.15, 0.5, 1.0}) {
                const QColor color = AccentControl::artAccentFor(hue, tone(hue, s, v));
                QVERIFY2(AccentControl::luminance(color) >= AccentControl::minLuminance - 1e-9,
                         qPrintable(QStringLiteral("hue %1 s %2 v %3").arg(hue).arg(s).arg(v)));
            }
        }
    }
}

void ArtColorTest::migration()
{
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#FA2A55")), 350);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#F09609")), 40);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#1BA1E2")), 200);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#339933")), 120);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#A200FF")), 280);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#808080")), 350);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("notacolor")), 350);
    QCOMPARE(Settings::hueFromColor(QStringLiteral("#zzzzzz")), 350);
    QCOMPARE(Settings::normalizeHue(355), 0);
    QCOMPARE(Settings::normalizeHue(344), 340);
    QCOMPARE(Settings::normalizeHue(-20), 340);

    const QString path = m_dir.path() + QStringLiteral("/migrate.ini");
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[General]\naccent=#1BA1E2\nscale=1.1\n");
    }
    {
        Settings settings(path);
        QCOMPARE(settings.accentHue(), 200);
        QVERIFY(!settings.accentFromArt());
        settings.sync();
    }
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        QVERIFY2(text.contains(QStringLiteral("accentHue=200")), qPrintable(text));
        QVERIFY2(!text.contains(QStringLiteral("accent=")), qPrintable(text));
    }
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[General]\naccentHue=abc\n");
    }
    {
        Settings settings(path);
        QCOMPARE(settings.accentHue(), 350);
    }
    {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("[General]\naccentHue=203\naccent=#A200FF\n");
    }
    {
        Settings settings(path);
        QCOMPARE(settings.accentHue(), 200);
    }
}

void ArtColorTest::effectiveHue()
{
    const QString red = save(QStringLiteral("effective-red.png"), solid(QColor(255, 0, 0)));
    const QString gray = save(QStringLiteral("effective-gray.png"), solid(QColor(128, 128, 128)));
    QVERIFY(!red.isEmpty());
    QVERIFY(!gray.isEmpty());

    AccentControl accent;
    QSignalSpy hueSpy(&accent, &AccentControl::hueChanged);
    QCOMPARE(accent.hue(), 350);
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.level(), 0);
    accent.setSliderHue(200);
    QCOMPARE(accent.hue(), 200);
    QCOMPARE(accent.color(), AccentControl::accentFor(200));
    QCOMPARE(hueSpy.count(), 1);

    accent.setArtPath(red);
    QTest::qWait(200);
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 200);

    accent.setFromArt(true);
    QTRY_COMPARE_WITH_TIMEOUT(accent.artHue(), 0, waitMs);
    QCOMPARE(accent.hue(), 0);
    QCOMPARE(accent.artLevel(), 4);
    QCOMPARE(accent.level(), 4);

    accent.setArtPath(gray);
    QTRY_COMPARE_WITH_TIMEOUT(accent.artHue(), -1, waitMs);
    QCOMPARE(accent.hue(), 200);
    QCOMPARE(accent.level(), 0);

    accent.setArtPath(red);
    QCOMPARE(accent.artHue(), 0);
    QCOMPARE(accent.hue(), 0);

    accent.setArtPath(QString());
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 200);

    accent.setArtPath(red);
    QCOMPARE(accent.hue(), 0);
    accent.setFromArt(false);
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 200);
    QCOMPARE(accent.level(), 0);
    accent.setSliderHue(120);
    QCOMPARE(accent.hue(), 120);
}

void ArtColorTest::logLines()
{
    const QString pastel = save(QStringLiteral("log-pastel.png"), hsvSolid(0, 0.25, 0.95));
    QVERIFY(!pastel.isEmpty());

    AccentControl accent;
    QTest::ignoreMessage(QtInfoMsg, "casdap accent: hue 280 s 4 (slider)");
    accent.setSliderHue(280);
    QTest::ignoreMessage(QtInfoMsg, qPrintable(QStringLiteral("casdap accent: art hue 0 s 1 for %1").arg(pastel)));
    QTest::ignoreMessage(QtInfoMsg, "casdap accent: hue 0 s 1 (art)");
    accent.setFromArt(true);
    accent.setArtPath(pastel);
    QTRY_COMPARE_WITH_TIMEOUT(accent.level(), 1, waitMs);
    QCOMPARE(accent.hue(), 0);
}

void ArtColorTest::staleResultCached()
{
    const QString cyan = save(QStringLiteral("stale-cyan.png"), solid(QColor(0x1B, 0xA1, 0xE2)));
    const QString green = save(QStringLiteral("stale-green.png"), solid(QColor(0, 255, 0)));
    QVERIFY(!cyan.isEmpty());
    QVERIFY(!green.isEmpty());

    AccentControl accent;
    accent.setFromArt(true);
    accent.setArtPath(cyan);
    accent.setArtPath(green);
    accent.artColor()->waitForDone();
    QTRY_COMPARE_WITH_TIMEOUT(accent.artHue(), 120, waitMs);
    QTRY_VERIFY_WITH_TIMEOUT(accent.artColor()->contains(cyan), waitMs);
    QCOMPARE(accent.artColor()->cachedHue(cyan), 200);
    QTest::qWait(100);
    QCOMPARE(accent.artHue(), 120);
    QCOMPARE(accent.hue(), 120);

    accent.clearCache();
    QVERIFY(!accent.artColor()->contains(cyan));
}

void ArtColorTest::phoneActive()
{
    const QString red = save(QStringLiteral("phone-red.png"), solid(QColor(255, 0, 0)));
    QVERIFY(!red.isEmpty());

    AccentControl accent;
    accent.setSliderHue(280);
    accent.setFromArt(true);
    accent.setArtPath(red);
    QTRY_COMPARE_WITH_TIMEOUT(accent.hue(), 0, waitMs);
    accent.setPhoneActive(true);
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 280);
    QCOMPARE(accent.level(), 0);
    accent.setPhoneActive(false);
    QCOMPARE(accent.hue(), 0);
    QCOMPARE(accent.level(), 4);
}

QTEST_GUILESS_MAIN(ArtColorTest)

#include "tst_artcolor.moc"
