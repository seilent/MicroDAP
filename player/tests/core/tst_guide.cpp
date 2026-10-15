#include "GuideChord.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace microdap;

namespace {

bool writeFile(const QString &path, const QByteArray &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text);
    return true;
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QByteArray ago(qint64 ms)
{
    return QByteArray::number(QDateTime::currentMSecsSinceEpoch() - ms) + '\n';
}

}

class GuideTest : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void directory();
    void fresh();
    void stale_data();
    void stale();
    void missing();
    void readOnly();

private:
    std::unique_ptr<QTemporaryDir> m_dir;
    QString m_down;
    QString m_chord;
};

void GuideTest::init()
{
    m_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_dir->isValid());
    qputenv("MICRODAP_GUIDE_DIR", m_dir->path().toUtf8());
    m_down = m_dir->path() + QStringLiteral("/down");
    m_chord = m_dir->path() + QStringLiteral("/chord");
}

void GuideTest::directory()
{
    QCOMPARE(GuideChord().directory(), m_dir->path());
    qunsetenv("MICRODAP_GUIDE_DIR");
    const QByteArray runtime = qgetenv("XDG_RUNTIME_DIR");
    qputenv("XDG_RUNTIME_DIR", "/tmp/microdap-runtime");
    QCOMPARE(GuideChord::defaultDirectory(), QStringLiteral("/tmp/microdap-runtime/microdap-guide"));
    qunsetenv("XDG_RUNTIME_DIR");
    QVERIFY(GuideChord::defaultDirectory().startsWith(QStringLiteral("/run/user/")));
    QVERIFY(GuideChord::defaultDirectory().endsWith(QStringLiteral("/microdap-guide")));
    if (!runtime.isEmpty())
        qputenv("XDG_RUNTIME_DIR", runtime);
}

void GuideTest::fresh()
{
    const QByteArray value = ago(100);
    QVERIFY(writeFile(m_down, value));
    GuideChord guide;
    QVERIFY(guide.take());
    QCOMPARE(readFile(m_chord), value);
}

void GuideTest::stale_data()
{
    QTest::addColumn<QByteArray>("down");
    QTest::newRow("zero") << QByteArray("0\n");
    QTest::newRow("old") << ago(600);
    QTest::newRow("future") << ago(-5000);
    QTest::newRow("empty") << QByteArray();
    QTest::newRow("garbage") << QByteArray("guide\n");
}

void GuideTest::stale()
{
    QFETCH(QByteArray, down);
    QVERIFY(writeFile(m_down, down));
    GuideChord guide;
    QVERIFY(!guide.take());
    QVERIFY(!QFile::exists(m_chord));
}

void GuideTest::missing()
{
    GuideChord guide;
    QVERIFY(!guide.take());
    QVERIFY(!QFile::exists(m_chord));
}

void GuideTest::readOnly()
{
    QVERIFY(writeFile(m_down, ago(100)));
    QVERIFY(QDir().mkdir(m_chord));
    GuideChord guide;
    QVERIFY(!guide.take());
    QVERIFY(QFileInfo(m_chord).isDir());
}

QTEST_GUILESS_MAIN(GuideTest)

#include "tst_guide.moc"
