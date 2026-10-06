#include "AudioInfo.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace casdap;

class AudioInfoTest : public QObject
{
    Q_OBJECT

private slots:
    void parseLive();
    void parseClosed();
    void readsFixtureAndTracksChanges();
};

void AudioInfoTest::parseLive()
{
    const QString text = QStringLiteral("access: MMAP_INTERLEAVED\nformat: S24_LE\nsubformat: STD\n"
                                        "channels: 2\nrate: 44100 (44100/1)\nperiod_size: 480\nbuffer_size: 3840\n");
    QCOMPARE(AudioInfo::parse(text), QStringLiteral("44100 S24_LE"));
}

void AudioInfoTest::parseClosed()
{
    QCOMPARE(AudioInfo::parse(QStringLiteral("closed\n")), QString());
    QCOMPARE(AudioInfo::parse(QString()), QString());
}

void AudioInfoTest::readsFixtureAndTracksChanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("pcm1p/sub0")));
    const QString path = dir.filePath(QStringLiteral("pcm1p/sub0/hw_params"));
    auto writeFile = [&](const QString &text) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text.toUtf8());
    };
    writeFile(QStringLiteral("closed\n"));
    qputenv("CASDAP_HW_PARAMS_DIR", dir.path().toUtf8());
    qputenv("CASDAP_HW_PARAMS_PCM", "1");
    QCOMPARE(AudioInfo::hwParamsPath(), path);

    AudioInfo info;
    QSignalSpy changed(&info, &AudioInfo::pcmFormatChanged);
    info.setActive(true);
    QCOMPARE(info.pcmFormat(), QString());
    QCOMPARE(changed.count(), 0);

    writeFile(QStringLiteral("access: MMAP_INTERLEAVED\nformat: S24_LE\nsubformat: STD\nchannels: 2\n"
                             "rate: 96000 (96000/1)\nperiod_size: 960\nbuffer_size: 7680\n"));
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("96000 S24_LE"));
    QCOMPARE(changed.count(), 1);

    info.setActive(false);
    writeFile(QStringLiteral("closed\n"));
    QTest::qWait(50);
    QCOMPARE(info.pcmFormat(), QStringLiteral("96000 S24_LE"));
    info.refresh();
    QCOMPARE(info.pcmFormat(), QString());
    qunsetenv("CASDAP_HW_PARAMS_DIR");
    qunsetenv("CASDAP_HW_PARAMS_PCM");
}

QTEST_GUILESS_MAIN(AudioInfoTest)

#include "tst_audioinfo.moc"
