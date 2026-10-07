#include "AccentControl.h"
#include "ArtColor.h"
#include "Queue.h"
#include "Settings.h"

#include <QColor>
#include <QFile>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace casdap;

namespace {

constexpr int waitMs = 5000;

QImage solid(const QColor &color)
{
    QImage image(ArtColor::side, ArtColor::side, QImage::Format_RGB32);
    image.fill(color);
    return image;
}

QImage mixed(const QColor &base, const QColor &spot, int pixels)
{
    QImage image = solid(base);
    for (int i = 0; i < pixels; ++i)
        image.setPixelColor(i % ArtColor::side, i / ArtColor::side, spot);
    return image;
}

}

class ArtColorTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void solidRed();
    void solidCyan();
    void grey();
    void dark();
    void softCovers();
    void fileUrl();
    void trackSequence();
    void spotThreshold();
    void dominantHue();
    void tieGoesLow();
    void unreadableFile();
    void fileScaled();
    void cacheHit();
    void cacheLimit();
    void accentColors();
    void brightColors();
    void migration();
    void effectiveHue();
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

void ArtColorTest::solidRed()
{
    QCOMPARE(ArtColor::hueOf(solid(QColor(255, 0, 0))), 0);
    QCOMPARE(ArtColor::hueOf(solid(QColor(255, 0, 20))), 0);
}

void ArtColorTest::solidCyan()
{
    QCOMPARE(ArtColor::hueOf(solid(QColor(0x1B, 0xA1, 0xE2))), 200);
}

void ArtColorTest::grey()
{
    QCOMPARE(ArtColor::hueOf(solid(QColor(128, 128, 128))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor(140, 120, 120))), -1);
    QCOMPARE(ArtColor::hueOf(QImage()), -1);
}

void ArtColorTest::dark()
{
    QCOMPARE(ArtColor::hueOf(solid(QColor(40, 0, 0))), -1);
}

void ArtColorTest::softCovers()
{
    QCOMPARE(ArtColor::hueOf(solid(QColor::fromHsv(330, 77, 230))), 330);
    QCOMPARE(ArtColor::hueOf(solid(QColor::fromHsv(210, 153, 56))), 210);
    QCOMPARE(ArtColor::hueOf(solid(QColor::fromHsv(330, 60, 230))), -1);
    QCOMPARE(ArtColor::hueOf(solid(QColor::fromHsv(210, 153, 46))), -1);
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
    QCOMPARE(accent.color(), AccentControl::accentFor(0));

    queue.setCurrentIndex(1);
    QCOMPARE(accent.artPath(), blue);
    QTRY_COMPARE_WITH_TIMEOUT(accent.hue(), 240, waitMs);
    QCOMPARE(accent.color(), AccentControl::accentFor(240));

    queue.setCurrentIndex(2);
    QCOMPARE(accent.artPath(), QString());
    QCOMPARE(accent.artHue(), -1);
    QCOMPARE(accent.hue(), 280);

    queue.setCurrentIndex(0);
    QCOMPARE(accent.hue(), 0);
    queue.setCurrentIndex(1);
    QCOMPARE(accent.hue(), 240);
    QCOMPARE(hueSpy.count(), 5);
}

void ArtColorTest::spotThreshold()
{
    const QColor gray(128, 128, 128);
    const QColor red(255, 0, 0);
    QCOMPARE(ArtColor::hueOf(mixed(gray, red, 10)), -1);
    QCOMPARE(ArtColor::hueOf(mixed(gray, red, ArtColor::minPixels - 1)), -1);
    QCOMPARE(ArtColor::hueOf(mixed(gray, red, ArtColor::minPixels)), 0);
    QCOMPARE(ArtColor::hueOf(mixed(gray, red, 103)), 0);
}

void ArtColorTest::dominantHue()
{
    QImage image = solid(QColor(0, 0, 255));
    for (int i = 0; i < 600; ++i)
        image.setPixelColor(i % ArtColor::side, i / ArtColor::side, QColor(0, 255, 0));
    for (int i = 600; i < 700; ++i)
        image.setPixelColor(i % ArtColor::side, i / ArtColor::side, QColor(128, 128, 128));
    QCOMPARE(ArtColor::hueOf(image), 120);

    QImage weighted = solid(QColor(128, 128, 128));
    for (int i = 0; i < 300; ++i)
        weighted.setPixelColor(i % ArtColor::side, i / ArtColor::side, QColor(255, 0, 0));
    for (int i = 300; i < 700; ++i)
        weighted.setPixelColor(i % ArtColor::side, i / ArtColor::side, QColor::fromHsvF(200.0f / 360.0f, 0.4f, 0.4f));
    QCOMPARE(ArtColor::hueOf(weighted), 0);
}

void ArtColorTest::tieGoesLow()
{
    QImage image(ArtColor::side, ArtColor::side, QImage::Format_RGB32);
    for (int y = 0; y < ArtColor::side; ++y) {
        for (int x = 0; x < ArtColor::side; ++x)
            image.setPixelColor(x, y, y < ArtColor::side / 2 ? QColor(0, 0, 255) : QColor(0, 255, 0));
    }
    QCOMPARE(ArtColor::hueOf(image), 120);
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
}

void ArtColorTest::fileScaled()
{
    QImage big(300, 200, QImage::Format_RGB32);
    big.fill(QColor(0x1B, 0xA1, 0xE2));
    const QString path = save(QStringLiteral("big.png"), big);
    QVERIFY(!path.isEmpty());
    QCOMPARE(ArtColor::hueOfFile(path), 200);
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
    QVERIFY(art.contains(path));
    QVERIFY(!art.pending(path));
    QCOMPARE(art.cachedHue(path), 0);

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
    AccentControl accent;
    QCOMPARE(accent.colorFor(350), AccentControl::accentFor(350));
}

void ArtColorTest::brightColors()
{
    QCOMPARE(AccentControl::brightFor(350).name(), QStringLiteral("#ff4d6b"));
    AccentControl accent;
    QCOMPARE(accent.brightColor(), AccentControl::brightFor(350));
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

    accent.setArtPath(gray);
    QTRY_COMPARE_WITH_TIMEOUT(accent.artHue(), -1, waitMs);
    QCOMPARE(accent.hue(), 200);

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
    accent.setSliderHue(120);
    QCOMPARE(accent.hue(), 120);
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
    accent.setPhoneActive(false);
    QCOMPARE(accent.hue(), 0);
}

QTEST_GUILESS_MAIN(ArtColorTest)

#include "tst_artcolor.moc"
