#include "BtOffer.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <memory>

using namespace microdap;

class BtOfferTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void absentReadsAuto();
    void createdLater();
    void appliedChange();
    void garbageIgnored();
    void directoryRecreated();
    void pendingRules();

private:
    bool write(const QByteArray &text);

    std::unique_ptr<QTemporaryDir> m_dir;
};

namespace {

constexpr int waitMs = 5000;

}

void BtOfferTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("MICRODAP_RUNTIME_DIR", QFile::encodeName(m_dir->path()));
}

void BtOfferTest::cleanup()
{
    qunsetenv("MICRODAP_RUNTIME_DIR");
    m_dir.reset();
}

bool BtOfferTest::write(const QByteArray &text)
{
    const QString dir = m_dir->filePath(QStringLiteral("microos-audio"));
    if (!QDir().mkpath(dir))
        return false;
    QFile f(dir + QStringLiteral("/bt-codecs.new"));
    if (!f.open(QIODevice::WriteOnly) || f.write(text) != text.size())
        return false;
    f.close();
    return std::rename(QFile::encodeName(f.fileName()).constData(), QFile::encodeName(BtOffer::file()).constData()) == 0;
}

void BtOfferTest::absentReadsAuto()
{
    QCOMPARE(BtOffer::file(), m_dir->filePath(QStringLiteral("microos-audio/bt-codecs")));
    BtOffer offer;
    QCOMPARE(offer.applied(), QStringLiteral("auto"));
}

void BtOfferTest::createdLater()
{
    BtOffer offer;
    QSignalSpy spy(&offer, &BtOffer::appliedChanged);
    QVERIFY(write("wanted aac\napplied aac\nwritten 12\n"));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("aac"), waitMs);
    QCOMPARE(spy.count(), 1);
}

void BtOfferTest::appliedChange()
{
    QVERIFY(write("wanted aac\napplied auto\n"));
    BtOffer offer;
    QCOMPARE(offer.applied(), QStringLiteral("auto"));
    QSignalSpy spy(&offer, &BtOffer::appliedChanged);
    QVERIFY(write("wanted aac\napplied aac\n"));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("aac"), waitMs);
    QVERIFY(write("wanted auto\napplied aptx\n"));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("aptx"), waitMs);
    QVERIFY(QFile::remove(BtOffer::file()));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("auto"), waitMs);
    QCOMPARE(spy.count(), 3);
}

void BtOfferTest::garbageIgnored()
{
    QVERIFY(write("applied\napplied ldac\napplied aac extra\nwanted sbc\n\x01\x02\napplied sbc\nnoise\n"));
    BtOffer offer;
    QCOMPARE(offer.applied(), QStringLiteral("sbc"));
    QVERIFY(write("applied opus\n"));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("auto"), waitMs);
}

void BtOfferTest::directoryRecreated()
{
    QVERIFY(write("applied aac\n"));
    BtOffer offer;
    QCOMPARE(offer.applied(), QStringLiteral("aac"));
    QVERIFY(QDir(m_dir->filePath(QStringLiteral("microos-audio"))).removeRecursively());
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("auto"), waitMs);
    QVERIFY(write("applied sbc\n"));
    QTRY_COMPARE_WITH_TIMEOUT(offer.applied(), QStringLiteral("sbc"), waitMs);
}

void BtOfferTest::pendingRules()
{
    const QString receiver = QStringLiteral("receiver");
    const QString headphones = QStringLiteral("headphones");
    QVERIFY(!BtOffer::pending(receiver, QStringLiteral("auto"), QStringLiteral("auto")));
    QVERIFY(BtOffer::pending(receiver, QStringLiteral("aac"), QStringLiteral("auto")));
    QVERIFY(BtOffer::pending(receiver, QStringLiteral("auto"), QStringLiteral("aac")));
    QVERIFY(!BtOffer::pending(receiver, QStringLiteral("aac"), QStringLiteral("aac")));
    QVERIFY(BtOffer::pending(headphones, QStringLiteral("ldac"), QStringLiteral("aac")));
    QVERIFY(BtOffer::pending(headphones, QStringLiteral("auto"), QStringLiteral("aac")));
    QVERIFY(!BtOffer::pending(headphones, QStringLiteral("aac"), QStringLiteral("aac")));
    QVERIFY(!BtOffer::pending(headphones, QStringLiteral("sbc"), QStringLiteral("aac")));
    QVERIFY(!BtOffer::pending(headphones, QStringLiteral("ldac"), QStringLiteral("auto")));
    QVERIFY(!BtOffer::pending(headphones, QStringLiteral("auto"), QStringLiteral("auto")));
}

QTEST_GUILESS_MAIN(BtOfferTest)
#include "tst_btoffer.moc"
