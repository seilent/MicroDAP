#include "AudioInfo.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    void picksRunningSubstream();
    void streamOfFiio();
    void streamOfAppleDongle();
    void streamPlaybackOnlyAndRanges();
    void hwRateFromHwParams();
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

const char *fiioStream = R"(FIIO SNOWSKY TINY B at usb-xhci-hcd.3.auto-1, high speed : USB Audio

Playback:
  Status: Stop
  Interface 2
    Altset 1
    Format: S16_LE
    Channels: 2
    Endpoint: 0x07 (7 OUT) (ADAPTIVE)
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
    Data packet interval: 125 us
    Bits: 16
    Channel map: FL FR
  Interface 2
    Altset 2
    Format: S24_3LE
    Channels: 2
    Endpoint: 0x07 (7 OUT) (ADAPTIVE)
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
    Data packet interval: 125 us
    Bits: 24
    Channel map: FL FR
  Interface 2
    Altset 3
    Format: S32_LE
    Channels: 2
    Endpoint: 0x07 (7 OUT) (ADAPTIVE)
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
    Data packet interval: 125 us
    Bits: 32
    Channel map: FL FR
  Interface 2
    Altset 4
    Format: DSD_U32_BE
    Channels: 2
    Endpoint: 0x07 (7 OUT) (ADAPTIVE)
    Rates: 44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000
    Data packet interval: 125 us
    Bits: 32
    DSD raw: DOP=0, bitrev=0
    Channel map: FL FR
)";

const char *dongleStream = R"(Apple, Inc. USB-C to 3.5mm Headphone Jack Adapter at usb-xhci-hcd.1.auto-1, high speed : USB Audio

Playback:
  Status: Stop
  Interface 1
    Altset 1
    Format: S24_3LE
    Channels: 2
    Endpoint: 0x01 (1 OUT) (SYNC)
    Rates: 48000
    Data packet interval: 125 us
    Bits: 24
    Channel map: FL FR
  Interface 1
    Altset 2
    Format: S16_LE
    Channels: 2
    Endpoint: 0x01 (1 OUT) (SYNC)
    Rates: 48000
    Data packet interval: 125 us
    Bits: 16
    Channel map: FL FR
)";

bool writeText(const QString &path, const QByteArray &text)
{
    if (!QDir().mkpath(QFileInfo(path).path()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(text) == text.size();
}

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
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());
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
    qunsetenv("CASDAP_ASOUND_DIR");
}

void AudioInfoTest::graphRateWithoutPwDump()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("card0/pcm1p/sub0")));
    const QString params = dir.filePath(QStringLiteral("card0/pcm1p/sub0/hw_params"));
    auto writeParams = [&params](int rate) {
        QFile file(params);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(QStringLiteral("access: MMAP_INTERLEAVED\nformat: S24_LE\nsubformat: STD\nchannels: 2\n"
                                  "rate: %1 (%1/1)\n").arg(rate).toUtf8());
    };
    writeParams(88200);
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());
    QCOMPARE(AudioInfo::pwDumpPath(), QString());

    AudioInfo info;
    QSignalSpy changed(&info, &AudioInfo::graphRateChanged);
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("88200 S24_LE"));
    QCOMPARE(info.graphRate(), 88200);
    info.refreshGraphRate();
    QCOMPARE(info.graphRate(), 88200);
    QCOMPARE(changed.count(), 1);

    writeParams(44100);
    info.refresh();
    QCOMPARE(info.graphRate(), 44100);
    QCOMPARE(changed.count(), 2);

    qunsetenv("CASDAP_PW_DUMP_BIN");
    qunsetenv("CASDAP_ASOUND_DIR");
}

void AudioInfoTest::picksRunningSubstream()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto writeSub = [&dir](const QString &pcm, const QString &params, const QString &status) {
        QVERIFY(QDir(dir.path()).mkpath(pcm + QStringLiteral("/sub0")));
        QFile hw(dir.filePath(pcm + QStringLiteral("/sub0/hw_params")));
        QVERIFY(hw.open(QIODevice::WriteOnly | QIODevice::Truncate));
        hw.write(params.toUtf8());
        QFile st(dir.filePath(pcm + QStringLiteral("/sub0/status")));
        QVERIFY(st.open(QIODevice::WriteOnly | QIODevice::Truncate));
        st.write(status.toUtf8());
    };
    const QString open48 = QStringLiteral("access: MMAP_INTERLEAVED\nformat: S16_LE\nchannels: 2\nrate: 48000 (48000/1)\n");
    const QString open96 = QStringLiteral("access: MMAP_INTERLEAVED\nformat: S24_3LE\nchannels: 2\nrate: 96000 (96000/1)\n");
    const QString running = QStringLiteral("state: RUNNING\nowner_pid   : 812\n");
    const QString prepared = QStringLiteral("state: PREPARED\nowner_pid   : 812\n");
    writeSub(QStringLiteral("card0/pcm0p"), open48, prepared);
    writeSub(QStringLiteral("card0/pcm1p"), QStringLiteral("closed\n"), QStringLiteral("closed\n"));
    writeSub(QStringLiteral("card1/pcm0p"), open96, running);
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("card1/pcm0c/sub0")));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());

    QCOMPARE(AudioInfo::asoundDir(), dir.path());
    QCOMPARE(AudioInfo::parseState(running), QStringLiteral("RUNNING"));
    QCOMPARE(AudioInfo::parseState(QStringLiteral("closed\n")), QString());
    QCOMPARE(AudioInfo::hwParamsPath(), dir.filePath(QStringLiteral("card1/pcm0p/sub0/hw_params")));
    AudioInfo info;
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("96000 S24_3LE"));
    QCOMPARE(info.graphRate(), 96000);

    writeSub(QStringLiteral("card1/pcm0p"), QStringLiteral("closed\n"), QStringLiteral("closed\n"));
    QCOMPARE(AudioInfo::hwParamsPath(), dir.filePath(QStringLiteral("card0/pcm0p/sub0/hw_params")));
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("48000 S16_LE"));

    writeSub(QStringLiteral("card0/pcm0p"), QStringLiteral("closed\n"), QStringLiteral("closed\n"));
    QCOMPARE(AudioInfo::hwParamsPath(), QString());
    info.refresh();
    QCOMPARE(info.pcmFormat(), QString());
    QCOMPARE(info.graphRate(), 0);

    qunsetenv("CASDAP_ASOUND_DIR");
    qunsetenv("CASDAP_PW_DUMP_BIN");
}

void AudioInfoTest::streamOfFiio()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeText(dir.filePath(QStringLiteral("card1/stream0")), fiioStream));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());

    const QList<int> eight{44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
    const UsbStream stream = AudioInfo::streamFor(1);
    QCOMPARE(stream.formats, QStringList({QStringLiteral("S16_LE"), QStringLiteral("S24_3LE"),
                                          QStringLiteral("S32_LE"), QStringLiteral("DSD_U32_BE")}));
    QCOMPARE(stream.rates, eight);
    QCOMPARE(AudioInfo::ratesFor(1), eight);

    QVERIFY(AudioInfo::streamFor(2).formats.isEmpty());
    QVERIFY(AudioInfo::streamFor(2).rates.isEmpty());
    QVERIFY(AudioInfo::ratesFor(2).isEmpty());

    qunsetenv("CASDAP_ASOUND_DIR");
}

void AudioInfoTest::streamOfAppleDongle()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeText(dir.filePath(QStringLiteral("card1/stream0")), dongleStream));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());

    const UsbStream stream = AudioInfo::streamFor(1);
    QCOMPARE(stream.formats, QStringList({QStringLiteral("S24_3LE"), QStringLiteral("S16_LE")}));
    QCOMPARE(stream.rates, QList<int>({48000}));
    QCOMPARE(AudioInfo::ratesFor(1), QList<int>({48000}));

    qunsetenv("CASDAP_ASOUND_DIR");
}

void AudioInfoTest::streamPlaybackOnlyAndRanges()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeText(dir.filePath(QStringLiteral("card2/stream0")),
                      "Test DAC at usb-1, high speed : USB Audio\n\n"
                      "Playback:\n  Status: Stop\n"
                      "  Interface 1\n    Altset 1\n    Format: S16_LE S24_LE\n    Channels: 2\n"
                      "    Rates: 32000, 44100, 768000\n"
                      "  Interface 1\n    Altset 2\n    Format: S24_LE\n    Channels: 2\n"
                      "    Rates: 8000 - 96000 (continuous)\n\n"
                      "Capture:\n  Status: Stop\n"
                      "  Interface 2\n    Altset 1\n    Format: S32_LE\n    Channels: 1\n"
                      "    Rates: 176400\n"));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());

    const UsbStream stream = AudioInfo::streamFor(2);
    QCOMPARE(stream.formats, QStringList({QStringLiteral("S16_LE"), QStringLiteral("S24_LE")}));
    QCOMPARE(stream.rates, QList<int>({32000, 44100, 48000, 88200, 96000, 768000}));
    QCOMPARE(AudioInfo::ratesFor(2), QList<int>({44100, 48000, 88200, 96000}));

    qunsetenv("CASDAP_ASOUND_DIR");
}

void AudioInfoTest::hwRateFromHwParams()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString params = dir.filePath(QStringLiteral("card1/pcm0p/sub0/hw_params"));
    QVERIFY(writeText(params, "access: MMAP_INTERLEAVED\nformat: S32_LE\nsubformat: STD\nchannels: 2\n"
                              "rate: 44100 (44100/1)\nperiod_size: 441\nbuffer_size: 3528\n"));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());

    AudioInfo info;
    QSignalSpy changed(&info, &AudioInfo::hwRateChanged);
    QCOMPARE(info.hwRate(), 0);
    QCOMPARE(info.property("hwRate").toInt(), 0);
    info.refresh();
    QCOMPARE(info.hwRate(), 44100);
    QCOMPARE(info.property("hwRate").toInt(), 44100);
    QCOMPARE(changed.count(), 1);
    info.refresh();
    QCOMPARE(changed.count(), 1);

    QVERIFY(writeText(params, "closed\n"));
    info.refresh();
    QCOMPARE(info.hwRate(), 0);
    QCOMPARE(changed.count(), 2);

    qunsetenv("CASDAP_ASOUND_DIR");
    qunsetenv("CASDAP_PW_DUMP_BIN");
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
    QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("card0/pcm1p/sub0")));
    const QString path = dir.filePath(QStringLiteral("card0/pcm1p/sub0/hw_params"));
    auto writeFile = [&](const QString &text) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text.toUtf8());
    };
    writeFile(QStringLiteral("closed\n"));
    qputenv("CASDAP_ASOUND_DIR", dir.path().toUtf8());
    qputenv("CASDAP_PW_DUMP_BIN", dir.filePath(QStringLiteral("missing/pw-dump")).toUtf8());
    QCOMPARE(AudioInfo::hwParamsPath(), QString());

    AudioInfo info;
    QSignalSpy changed(&info, &AudioInfo::pcmFormatChanged);
    info.setActive(true);
    QCOMPARE(info.pcmFormat(), QString());
    QCOMPARE(changed.count(), 0);

    writeFile(QStringLiteral("access: MMAP_INTERLEAVED\nformat: S24_LE\nsubformat: STD\nchannels: 2\n"
                             "rate: 96000 (96000/1)\nperiod_size: 960\nbuffer_size: 7680\n"));
    QCOMPARE(AudioInfo::hwParamsPath(), path);
    info.refresh();
    QCOMPARE(info.pcmFormat(), QStringLiteral("96000 S24_LE"));
    QCOMPARE(changed.count(), 1);

    info.setActive(false);
    writeFile(QStringLiteral("closed\n"));
    QTest::qWait(50);
    QCOMPARE(info.pcmFormat(), QStringLiteral("96000 S24_LE"));
    info.refresh();
    QCOMPARE(info.pcmFormat(), QString());
    qunsetenv("CASDAP_ASOUND_DIR");
    qunsetenv("CASDAP_PW_DUMP_BIN");
}

QTEST_GUILESS_MAIN(AudioInfoTest)

#include "tst_audioinfo.moc"
