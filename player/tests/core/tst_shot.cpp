#include <QImage>
#include <QTransform>
#include <QtTest>

#include <algorithm>
#include <cstdlib>

namespace {

constexpr int channelDelta = 32;
constexpr double shareThreshold = 0.02;

int landscapeTurn()
{
    return qEnvironmentVariable("MICRODAP_LANDSCAPE_TURN") == QLatin1StringView("3") ? 3 : 1;
}

}

class ShotTest : public QObject
{
    Q_OBJECT

private slots:
    void blank();
    void orientation();
};

void ShotTest::blank()
{
    const QImage image(QStringLiteral(MICRODAP_SHOT_PNG));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(640, 960));

    const QColor bg(QStringLiteral("#111111"));
    QCOMPARE(image.pixelColor(10, 10), bg);
    QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), bg);
}

void ShotTest::orientation()
{
    const QImage upright = QImage(QStringLiteral(MICRODAP_SHOT_UPRIGHT)).convertToFormat(QImage::Format_RGB32);
    const QImage turned = QImage(QStringLiteral(MICRODAP_SHOT_TURNED)).convertToFormat(QImage::Format_RGB32);
    QVERIFY(!upright.isNull());
    QVERIFY(!turned.isNull());
    QCOMPARE(upright.size(), QSize(960, 640));
    QCOMPARE(turned.size(), QSize(640, 960));

    const QImage back = turned.transformed(QTransform().rotate(-90 * landscapeTurn()));
    QCOMPARE(back.size(), upright.size());

    qint64 differing = 0;
    for (int y = 0; y < upright.height(); ++y) {
        const QRgb *a = reinterpret_cast<const QRgb *>(upright.constScanLine(y));
        const QRgb *b = reinterpret_cast<const QRgb *>(back.constScanLine(y));
        for (int x = 0; x < upright.width(); ++x) {
            const int d = std::max({std::abs(qRed(a[x]) - qRed(b[x])), std::abs(qGreen(a[x]) - qGreen(b[x])),
                                    std::abs(qBlue(a[x]) - qBlue(b[x]))});
            if (d > channelDelta)
                ++differing;
        }
    }
    const double share = double(differing) / (upright.width() * upright.height());
    qInfo() << "microdap shot: orientation share over" << channelDelta << share;
    QVERIFY(share <= shareThreshold);
}

QTEST_MAIN(ShotTest)
#include "tst_shot.moc"
