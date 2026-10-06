#include "AudioInfo.h"

#include <QDir>
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
    void parseGraphRate();
    void graphRateFromStub();
    void graphRateWithoutPwDump();
};

namespace {

const char *dumpJson = R"([
  {"id": 0, "type": "PipeWire:Interface:Core", "info": {"props": {"default.clock.rate": 48000}}},
  {"id": 31, "type": "PipeWire:Interface:Metadata", "props": {"metadata.name": "settings"},
   "metadata": [{"subject": 0, "key": "clock.rate", "type": "", "value": 48000}]},
  {"id": 32, "type": "PipeWire:Interface:Metadata", "props": {"metadata.name": "default"},
   "metadata": [{"subject": 0, "key": "default.audio.sink", "type": "Spa:String:JSON",
                 "value": {"name": "alsa_output.platform-sound.HiFi__Speaker__sink"}}]},
  {"id": 40, "type": "PipeWire:Interface:Node", "info": {
     "props": {"node.name": "alsa_output.platform-sound.HiFi__Headphones__sink", "clock.rate": 96000},
     "params": {"Format": [{"rate": 96000}]}}},
  {"id": 41, "type": "PipeWire:Interface:Node", "info": {
     "props": {"node.name": "alsa_output.platform-sound.HiFi__Speaker__sink"},
     "params": {"Format": [{"mediaType": "audio", "rate": 44100, "channels": 2}]}}}
])";

bool writeExecutable(const QString &path, const QString &text)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(text.toUtf8());
    file.close();
    return file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
}

}

void AudioInfoTest::parseGraphRate()
{
    QCOMPARE(AudioInfo::parseGraphRate(QByteArray(dumpJson)), 44100);

    QByteArray clockRate(dumpJson);
    clockRate.replace("HiFi__Speaker__sink\"}}]", "HiFi__Headphones__sink\"}}]");
    QCOMPARE(AudioInfo::parseGraphRate(clockRate), 96000);

    QByteArray noSink(dumpJson);
    noSink.replace("default.audio.sink", "default.audio.source");
    QCOMPARE(AudioInfo::parseGraphRate(noSink), 48000);

    QCOMPARE(AudioInfo::parseGraphRate(QByteArrayLiteral("[]")), 0);
    QCOMPARE(AudioInfo::parseGraphRate(QByteArrayLiteral("not json")), 0);
}

void AudioInfoTest::graphRateFromStub()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString json = dir.filePath(QStringLiteral("dump.json"));
    {
        QFile file(json);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(dumpJson);
    }
    const QString stub = dir.filePath(QStringLiteral("pw-dump"));
    QVERIFY(writeExecutable(stub, QStringLiteral("#!/bin/sh\ncat \"") + json + QStringLiteral("\"\n")));
    qputenv("CASDAP_PW_DUMP_BIN", stub.toUtf8());
    qputenv("CASDAP_HW_PARAMS_DIR", dir.path().toUtf8());
    QCOMPARE(AudioInfo::pwDumpPath(), stub);

    AudioInfo info;
    QSignalSpy changed(&info, &AudioInfo::graphRateChanged);
    QCOMPARE(info.graphRate(), 0);
    info.setActive(true);
    QTest::qWait(100);
    QCOMPARE(changed.count(), 0);
    info.refreshGraphRate();
    QTRY_COMPARE_WITH_TIMEOUT(info.graphRate(), 44100, 5000);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(info.pcmFormat(), QString());

    info.refresh();
    QTest::qWait(300);
    QCOMPARE(changed.count(), 1);
    QCOMPARE(info.graphRate(), 44100);

    QVERIFY(writeExecutable(stub, QStringLiteral("#!/bin/sh\nexit 1\n")));
    QTRY_VERIFY_WITH_TIMEOUT([&info] { info.refreshGraphRate(); return info.graphRate() == 0; }(), 5000);
    QCOMPARE(changed.count(), 2);

    qunsetenv("CASDAP_PW_DUMP_BIN");
    qunsetenv("CASDAP_HW_PARAMS_DIR");
}

void AudioInfoTest::graphRateWithoutPwDump()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("pcm1p/sub0")));
    {
        QFile file(dir.filePath(QStringLiteral("pcm1p/sub0/hw_params")));
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write("access: MMAP_INTERLEAVED\nformat: S24_LE\nsubformat: STD\nchannels: 2\nrate: 88200 (88200/1)\n");
    }
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());
    qputenv("CASDAP_HW_PARAMS_DIR", dir.path().toUtf8());
    qputenv("CASDAP_HW_PARAMS_PCM", "1");
    QCOMPARE(AudioInfo::pwDumpPath(), QString());

    AudioInfo info;
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("88200 S24_LE"));
    info.refreshGraphRate();
    QCOMPARE(info.graphRate(), 88200);

    qunsetenv("CASDAP_PW_DUMP_BIN");
    qunsetenv("CASDAP_HW_PARAMS_DIR");
    qunsetenv("CASDAP_HW_PARAMS_PCM");
}

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
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());
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
    qunsetenv("CASDAP_PW_DUMP_BIN");
}

QTEST_GUILESS_MAIN(AudioInfoTest)

#include "tst_audioinfo.moc"
