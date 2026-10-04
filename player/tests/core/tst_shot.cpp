#include <QImage>
#include <QtTest>

class ShotTest : public QObject
{
    Q_OBJECT

private slots:
    void blank();
};

void ShotTest::blank()
{
    const QImage image(QStringLiteral(CASDAP_SHOT_PNG));
    QVERIFY(!image.isNull());
    QCOMPARE(image.size(), QSize(640, 960));

    const QColor bg(QStringLiteral("#111111"));
    QCOMPARE(image.pixelColor(10, 10), bg);
    QCOMPARE(image.pixelColor(image.width() / 2, image.height() / 2), bg);
}

QTEST_MAIN(ShotTest)
#include "tst_shot.moc"
