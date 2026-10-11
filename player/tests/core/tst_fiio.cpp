#include "FakeUsbIo.h"
#include "Fiio.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <cerrno>
#include <cmath>
#include <cstring>
#include <chrono>
#include <thread>

using namespace casdap;

namespace {

std::vector<uint8_t> bytes(const char *hexText)
{
    const QByteArray raw = QByteArray::fromHex(hexText);
    return std::vector<uint8_t>(raw.begin(), raw.end());
}

std::vector<uint8_t> packet(const char *hexText)
{
    std::vector<uint8_t> out = bytes(hexText);
    out.resize(fiioPacketSize, 0);
    return out;
}

QByteArray hexOf(const std::vector<uint8_t> &data)
{
    return QByteArray(reinterpret_cast<const char *>(data.data()), qsizetype(data.size())).toHex();
}

std::optional<FiioReply> parse(const std::vector<uint8_t> &data)
{
    return parseReply(data.data(), data.size());
}

bool writeFile(const QString &path, const QByteArray &text)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(text) == text.size();
}

bool link(const QString &target, const QString &path)
{
    return QDir().mkpath(QFileInfo(path).absolutePath()) && QFile::link(target, path);
}

bool usbDevice(const QString &sys, const QString &name, const QByteArray &vid, const QByteArray &pid)
{
    const QString dev = sys + QStringLiteral("/devices/platform/xhci/") + name;
    return writeFile(dev + QStringLiteral("/idVendor"), vid + '\n') && writeFile(dev + QStringLiteral("/idProduct"), pid + '\n')
           && link(dev, sys + QStringLiteral("/bus/usb/devices/") + name);
}

bool hidInterface(const QString &sys, const QString &name, const QByteArray &cls, bool epIn, bool epOut = true)
{
    const QString iface = sys + QStringLiteral("/devices/platform/xhci/") + name + QLatin1Char('/') + name
                          + QStringLiteral(":1.0");
    return writeFile(iface + QStringLiteral("/bInterfaceClass"), cls + '\n')
           && (!epOut || QDir().mkpath(iface + QStringLiteral("/ep_05")))
           && (!epIn || QDir().mkpath(iface + QStringLiteral("/ep_86")));
}

bool soundCard(const QString &sys, const QString &name, int card)
{
    const QString audio = sys + QStringLiteral("/devices/platform/xhci/") + name + QLatin1Char('/') + name
                          + QStringLiteral(":1.1");
    return QDir().mkpath(audio)
           && link(audio, sys + QStringLiteral("/class/sound/card%1/device").arg(card));
}

bool usbNode(const QString &dir, const QByteArray &bus, const QByteArray &dev)
{
    return writeFile(dir + QStringLiteral("/busnum"), bus + '\n') && writeFile(dir + QStringLiteral("/devnum"), dev + '\n');
}

const FiioModel &tinyModel()
{
    return *fiioModel(0x2972, 0x0149);
}

struct Rig
{
    Rig()
        : device(fake, tinyModel(), FiioDevice::Bounds{})
    {
        sysPath = (root.path() + QStringLiteral("/3-1")).toStdString();
        nodeReady = usbNode(root.path() + QStringLiteral("/3-1"), "3", "2");
        device.setLog([this](const std::string &line) { log.push_back(line); });
    }

    ~Rig()
    {
        if (fakeErrorsExpected)
            return;
        QCOMPARE(fake.refusedReads(), 0);
        QCOMPARE(fake.badFrames(), 0);
    }

    FiioResult get(uint8_t cmd, const std::vector<uint8_t> &payload = {})
    {
        return device.transact(frameGet(++seq, cmd, payload), std::nullopt);
    }

    FiioResult set(uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        return device.transact(frameSet(++seq, cmd, payload), std::nullopt);
    }

    int logged(const std::string &prefix) const
    {
        return int(std::count_if(log.begin(), log.end(),
                                 [&prefix](const std::string &l) { return l.rfind(prefix, 0) == 0; }));
    }

    QTemporaryDir root;
    FakeUsbIo fake;
    FiioDevice device;
    std::string sysPath;
    std::vector<std::string> log;
    bool nodeReady = false;
    bool fakeErrorsExpected = false;
    uint16_t seq = 0;
};

int inReadsAfterLastOut(const std::vector<FakeUsbIo::Call> &calls)
{
    int reads = 0;
    for (const FakeUsbIo::Call &call : calls) {
        if (call.op == FakeUsbIo::Op::Out)
            reads = 0;
        else if (call.op == FakeUsbIo::Op::In)
            ++reads;
    }
    return reads;
}

FiioBackup sampleBackup()
{
    FiioBackup backup;
    backup.model = "2972:0149";
    backup.firmware = "1.3.0";
    backup.date = "2026-10-11T12:00:00Z";
    backup.name = "FH3";
    backup.preamp = bytes("0050");
    for (int i = 0; i < fiioBandCount; ++i)
        backup.bands[i] = bytes("000000000064ff00");
    for (int i = 0; i < fiioBandCount; ++i)
        backup.bands[i][0] = uint8_t(i);
    backup.bands[9] = bytes("090014319c006400");
    return backup;
}

}

class FiioTest : public QObject
{
    Q_OBJECT

private slots:
    void crc();
    void frames();
    void replyParse();
    void replyMatch();
    void payloads();
    void namePayloadCase();
    void backupRoundTrip();
    void modelTable();
    void usbEnumeration();
    void fakeRejectsShortRead();
    void fakeWedge();
    void delayedReply();
    void lateReplyTimeout();
    void deadline();
    void outErrors();
    void inErrorDuringWait();
    void discardCounted();
    void descriptorMismatch();
    void overflow();
    void claimBusy();
    void permission();
    void namesUnderBypass();
    void bandsZeroUnderBypass();
    void replugPersistence();
    void qRounding();
    void holdRelease();
    void usbfsNoDevice();
};

void FiioTest::crc()
{
    const char *check = "123456789";
    QCOMPARE(crc8Maxim(reinterpret_cast<const uint8_t *>(check), std::strlen(check)), uint8_t(0xA1));
    QCOMPARE(crc8Maxim(nullptr, 0), uint8_t(0));
    const std::vector<uint8_t> band = bytes("07aa0a0006150809fff12ee0004702");
    QCOMPARE(crc8Maxim(band.data(), band.size()), uint8_t(0xCA));
    const std::vector<uint8_t> name = bytes("07aa0a00083009a24341534441500000");
    QCOMPARE(crc8Maxim(name.data(), name.size()), uint8_t(0x8B));
}

void FiioTest::frames()
{
    QCOMPARE(hexOf(frameGet(1, fiioCmdVersion)), QByteArray("07bb0b00010b00b4ee"));
    QCOMPARE(hexOf(frameGet(2, fiioCmdPreset)), QByteArray("07bb0b0002160035ee"));
    QCOMPARE(hexOf(frameGet(3, fiioCmdName, {0xA2})), QByteArray("07bb0b00033001a29cee"));
    QCOMPARE(hexOf(frameGet(4, fiioCmdBand, {9})), QByteArray("07bb0b000415010934ee"));
    QCOMPARE(hexOf(frameSet(5, fiioCmdPreset, presetPayload(0xA2))), QByteArray("07aa0a00051601a221ee"));
    QCOMPARE(hexOf(frameSet(6, fiioCmdBand, bytes("09fff12ee0004702"))), QByteArray("07aa0a0006150809fff12ee0004702caee"));
    QCOMPARE(hexOf(frameSet(7, fiioCmdPreamp, bytes("fff6"))), QByteArray("07aa0a00071702fff6c2ee"));
    QCOMPARE(hexOf(frameSet(8, fiioCmdName, namePayload(0xA2, "CASDAP"))),
             QByteArray("07aa0a00083009a243415344415000008bee"));
    QCOMPARE(hexOf(frameSet(32767, fiioCmdPreset, presetPayload(fiioBypass))), QByteArray("07aa0a7fff1601f02eee"));
    const std::vector<uint8_t> unpadded = frameSet(6, fiioCmdBand, bytes("09fff12ee0004702"));
    QCOMPARE(unpadded.size(), std::size_t(8 + 9));
    QCOMPARE(frameSet(1, fiioCmdName, std::vector<uint8_t>(255, 0x41)).size(), std::size_t(255 + 9));
    QVERIFY(frameSet(1, fiioCmdName, std::vector<uint8_t>(256, 0x41)).empty());
    QVERIFY(frameGet(1, fiioCmdName, std::vector<uint8_t>(300, 0x41)).empty());
    const std::vector<uint8_t> other = frameGet(1, fiioCmdPreset, {}, 0x4B);
    QCOMPARE(other.front(), uint8_t(0x4B));
    QVERIFY(parseReply(other.data(), other.size(), 0x4B));
    QVERIFY(!parseReply(other.data(), other.size()));
    QCOMPARE(fiioModels().front().reportId, fiioReportId);
}

void FiioTest::replyParse()
{
    const struct
    {
        const char *hexText;
        bool set;
        uint16_t seq;
        uint8_t cmd;
        const char *payload;
    } recorded[] = {
        {"07bb0b00012001014cee", false, 0x0001, 0x20, "01"},
        {"07bb0b000202013c5cee", false, 0x0002, 0x02, "3c"},
        {"07bb0b005a20010100ee", false, 0x005A, 0x20, "01"},
        {"07bb0b005b02013c17ee", false, 0x005B, 0x02, "3c"},
        {"07bb0b007202011e6bee", false, 0x0072, 0x02, "1e"},
        {"07aa0a007102011e12ee", true, 0x0071, 0x02, "1e"},
        {"07aa0a00751601a0e5ee", true, 0x0075, fiioCmdPreset, "a0"},
        {"07bb0b00761601a09cee", false, 0x0076, fiioCmdPreset, "a0"},
        {"07bb0b0078150800001400170064008bee", false, 0x0078, fiioCmdBand, "0000140017006400"},
        {"07bb0b008a150800ffdc001400640066ee", false, 0x008A, fiioCmdBand, "00ffdc0014006400"},
        {"07bb0b00931508090014319c00640075ee", false, 0x0093, fiioCmdBand, "090014319c006400"},
    };
    for (const auto &r : recorded) {
        const std::optional<FiioReply> reply = parse(packet(r.hexText));
        QVERIFY2(reply, r.hexText);
        QCOMPARE(reply->set, r.set);
        QCOMPARE(reply->seq, r.seq);
        QCOMPARE(reply->cmd, r.cmd);
        QCOMPARE(hexOf(reply->payload), QByteArray(r.payload));
        QVERIFY2(reply->crcValid, r.hexText);
    }

    for (const char *hexText : {"07bb0b00590b030103000000", "07bb0b000c0b03010300ee00"}) {
        const std::optional<FiioReply> version = parse(packet(hexText));
        QVERIFY2(version, hexText);
        QVERIFY(!version->set);
        QCOMPARE(version->cmd, fiioCmdVersion);
        QCOMPARE(hexOf(version->payload), QByteArray("010300"));
        QVERIFY2(!version->crcValid, hexText);
    }

    const std::optional<FiioReply> fh = parse(packet("07bb0b00931508090014319c00640075ee"));
    const std::optional<FiioBand> band = bandOfPayload(fh->payload);
    QVERIFY(band);
    QCOMPARE(band->index, 9);
    QCOMPARE(band->gain, 2.0);
    QCOMPARE(band->freq, 12700.0);
    QCOMPARE(band->q, 1.0);
    QCOMPARE(band->type, 0);
    const std::optional<FiioBand> low = bandOfPayload(parse(packet("07bb0b008a150800ffdc001400640066ee"))->payload);
    QVERIFY(low);
    QCOMPARE(low->gain, -3.6);
    QCOMPARE(low->freq, 20.0);

    QVERIFY(!parse(packet("01bb0b00761601a09cee")));
    QVERIFY(!parse(packet("07bb0a00761601a09cee")));
    QVERIFY(!parse(packet("07aa0b00761601a09cee")));
    QVERIFY(!parse(bytes("07bb0b0076")));
    QVERIFY(!parse(bytes("07bb0b00761609a0")));
    QVERIFY(!parse(packet("")));
    QVERIFY(!parseReply(nullptr, 0));
    QVERIFY(!parse(packet("07bb0b007616ff")));
    std::vector<uint8_t> badCrc = packet("07bb0b00761601a09cee");
    badCrc[8] ^= 0xFF;
    const std::optional<FiioReply> bad = parse(badCrc);
    QVERIFY(bad);
    QVERIFY(!bad->crcValid);
    std::vector<uint8_t> noEnd = packet("07bb0b00761601a09cee");
    noEnd[9] = 0;
    QVERIFY(!parse(noEnd)->crcValid);
    const std::vector<uint8_t> hostFrame = frameGet(0x76, fiioCmdPreset, {0xA0});
    QVERIFY(!parse(hostFrame)->crcValid);
}

void FiioTest::replyMatch()
{
    const FiioReply band0 = *parse(packet("07bb0b0078150800001400170064008bee"));
    const FiioReply band9 = *parse(packet("07bb0b00931508090014319c00640075ee"));
    QVERIFY(matches(frameGet(9, fiioCmdBand, {0}), band0));
    QVERIFY(matches(frameGet(9, fiioCmdBand, {9}), band9));
    QVERIFY(!matches(frameGet(9, fiioCmdBand, {5}), band9));
    QVERIFY(!matches(frameGet(9, fiioCmdBand, {9}), band0));

    const std::vector<uint8_t> nameReply = frameGet(0x25, fiioCmdName, namePayload(0xA2, "FH3"));
    const FiioReply name = *parse(nameReply);
    QVERIFY(matches(frameGet(10, fiioCmdName, {0xA2}), name));
    QVERIFY(!matches(frameGet(10, fiioCmdName, {0xA0}), name));

    const FiioReply preset = *parse(packet("07bb0b00761601a09cee"));
    QVERIFY(matches(frameGet(11, fiioCmdPreset), preset));
    QVERIFY(!matches(frameGet(11, fiioCmdBand, {0}), preset));
    QVERIFY(!matches(frameGet(11, fiioCmdPreamp), preset));
    QVERIFY(!matches(frameSet(11, fiioCmdPreset, presetPayload(0xA0)), preset));

    const FiioReply presetAck = *parse(packet("07aa0a00751601a0e5ee"));
    QVERIFY(matches(frameSet(12, fiioCmdPreset, presetPayload(0xA0)), presetAck));
    QVERIFY(!matches(frameSet(12, fiioCmdPreset, presetPayload(fiioBypass)), presetAck));
    QVERIFY(!matches(frameGet(12, fiioCmdPreset), presetAck));
    const FiioReply volumeAck = *parse(packet("07aa0a007102011e12ee"));
    QVERIFY(matches(frameSet(13, 0x02, {0x1E}), volumeAck));
    QVERIFY(!matches(frameSet(13, 0x02, {0x3C}), volumeAck));
    QVERIFY(!matches(frameSet(13, fiioCmdPreset, {0x1E}), volumeAck));
    QVERIFY(matches(frameGet(13, 0x02), *parse(packet("07bb0b007202011e6bee"))));

    const FiioReply bandAck = *parse(packet("07aa0a0094150809fff12ee000460294ee"));
    QVERIFY(bandAck.crcValid);
    QVERIFY(matches(frameSet(16, fiioCmdBand, bytes("09fff12ee0004702")), bandAck));
    QVERIFY(matches(frameSet(16, fiioCmdBand, bytes("09fff12ee0004b02")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("09fff12ee0004c02")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("09fff02ee0004702")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("09fff12ee1004702")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("08fff12ee0004702")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("09fff12ee0004701")), bandAck));
    QVERIFY(!matches(frameSet(16, fiioCmdBand, bytes("09fff12ee000")), bandAck));
    const FiioReply preampAck = *parse(packet("07aa0a00961702fff6efee"));
    QVERIFY(preampAck.crcValid);
    QVERIFY(matches(frameSet(17, fiioCmdPreamp, bytes("fff6")), preampAck));
    QVERIFY(!matches(frameSet(17, fiioCmdPreamp, bytes("fff7")), preampAck));
    QVERIFY(presetAck.crcValid);
    QVERIFY(volumeAck.crcValid);
    const FiioReply nameAck = *parse(packet("07aa0a01483009a243415344415000000057"));
    QCOMPARE(nameAck.payload.size(), std::size_t(1 + fiioNameLength));
    QVERIFY(matches(frameSet(18, fiioCmdName, namePayload(0xA2, "CASDAP")), nameAck));
    QVERIFY(matches(frameSet(18, fiioCmdName, bytes("a24341534441500000" "00")), nameAck));
    QVERIFY(!matches(frameSet(18, fiioCmdName, namePayload(0xA2, "CASDAQ")), nameAck));
    QVERIFY(!matches(frameSet(18, fiioCmdName, namePayload(0xA1, "CASDAP")), nameAck));

    for (const char *hexText : {"07bb0b00590b030103000000", "07bb0b000c0b03010300ee00"}) {
        const FiioReply version = *parse(packet(hexText));
        QVERIFY(!version.crcValid);
        QVERIFY(matches(frameGet(14, fiioCmdVersion), version));
        QVERIFY(!matches(frameGet(14, 0x20), version));
    }
    std::vector<uint8_t> badCrc = packet("07bb0b00931508090014319c00640075ee");
    badCrc[15] ^= 0x55;
    const FiioReply bad = *parse(badCrc);
    QVERIFY(!bad.crcValid);
    QVERIFY(matches(frameGet(15, fiioCmdBand, {9}), bad));
    QVERIFY(!matches(bytes("07bb"), preset));
}

void FiioTest::payloads()
{
    const FiioModel &tiny = fiioModels().front();
    FiioBand band;
    band.index = 9;
    band.gain = -1.5;
    band.freq = 12000.0;
    band.q = 0.71;
    band.type = 2;
    QCOMPARE(hexOf(bandPayload(tiny, band)), QByteArray("09fff12ee0004702"));

    band.index = 0;
    band.type = 0;
    band.gain = -15.0;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(2, 4), QByteArray("ff88"));
    band.gain = 13.0;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(2, 4), QByteArray("0078"));
    band.gain = 3.04;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(2, 4), QByteArray("001e"));
    band.q = 10.0;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(10, 4), QByteArray("0320"));
    band.q = 0.1;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(10, 4), QByteArray("0019"));
    band.q = 1.41;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(10, 4), QByteArray("008d"));
    band.freq = 25000.0;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(6, 4), QByteArray("4e20"));
    band.freq = 5.0;
    QCOMPARE(hexOf(bandPayload(tiny, band)).mid(6, 4), QByteArray("0014"));

    FiioBand off;
    off.index = 4;
    off.enabled = false;
    off.type = 4;
    off.gain = 6.0;
    off.freq = 250.0;
    off.q = 2.0;
    QCOMPARE(hexOf(bandPayload(tiny, off)), QByteArray("04000000fa00c800"));

    FiioBand lowPass;
    lowPass.index = 5;
    lowPass.type = 4;
    lowPass.gain = 6.0;
    lowPass.freq = 105.0;
    lowPass.q = 0.7;
    QCOMPARE(hexOf(bandPayload(tiny, lowPass)), QByteArray("0500000069004604"));
    for (int type = 3; type <= 6; ++type) {
        lowPass.type = type;
        QCOMPARE(hexOf(bandPayload(tiny, lowPass)).mid(2, 4), QByteArray("0000"));
    }
    lowPass.type = 1;
    QCOMPARE(hexOf(bandPayload(tiny, lowPass)).mid(2, 4), QByteArray("003c"));

    QCOMPARE(hexOf(preampPayload(tiny, 0.0)), QByteArray("0000"));
    QCOMPARE(hexOf(preampPayload(tiny, -6.0)), QByteArray("ffc4"));
    QCOMPARE(hexOf(preampPayload(tiny, -30.0)), QByteArray("ff88"));
    QCOMPARE(hexOf(presetPayload(0xA2)), QByteArray("a2"));

    const std::optional<FiioBand> jazz = bandOfPayload(bytes("03001e00fa004600"));
    QVERIFY(jazz);
    QCOMPARE(jazz->index, 3);
    QCOMPARE(jazz->gain, 3.0);
    QCOMPARE(jazz->freq, 250.0);
    QCOMPARE(jazz->q, 0.7);
    QCOMPARE(jazz->type, 0);
    const std::optional<FiioBand> fh = bandOfPayload(bytes("09fff12ee0004602"));
    QVERIFY(fh);
    QCOMPARE(fh->gain, -1.5);
    QCOMPARE(fh->freq, 12000.0);
    QCOMPARE(fh->type, 2);
    QVERIFY(!bandOfPayload(bytes("03001e00fa0046")));

    FiioBand refused;
    refused.index = 10;
    QVERIFY(bandPayload(tiny, refused).empty());
    refused.index = -1;
    QVERIFY(bandPayload(tiny, refused).empty());
    refused.index = 0;
    refused.gain = std::nan("");
    QVERIFY(bandPayload(tiny, refused).empty());
    refused.gain = 0.0;
    refused.freq = INFINITY;
    QVERIFY(bandPayload(tiny, refused).empty());
    refused.freq = 1000.0;
    refused.q = -INFINITY;
    QVERIFY(bandPayload(tiny, refused).empty());
    refused.q = 0.71;
    QCOMPARE(bandPayload(tiny, refused).size(), std::size_t(fiioBandPayloadSize));
}

void FiioTest::namePayloadCase()
{
    QCOMPARE(hexOf(namePayload(0xA2, "CASDAP")), QByteArray("a2434153444150" "0000"));
    QCOMPARE(hexOf(namePayload(0xA2, "")), QByteArray("a20000000000000000"));
    QCOMPARE(hexOf(namePayload(0xA0, "ABCDEFGHIJ")), QByteArray("a04142434445464748"));
    QCOMPARE(namePayload(0xA2, "CASDAP").size(), std::size_t(1 + fiioNameLength));
    QCOMPARE(nameOfPayload(bytes("a246483300000000")), std::string("FH3"));
    QCOMPARE(nameOfPayload(bytes("a14646350000000000")), std::string("FF5"));

    const std::optional<FiioReply> fd15 = parse(packet("07bb0b00413009a04644313500000000eebf"));
    QVERIFY(fd15);
    QCOMPARE(fd15->cmd, fiioCmdName);
    QCOMPARE(fd15->payload.size(), std::size_t(1 + fiioNameLength));
    QCOMPARE(nameOfPayload(fd15->payload), std::string("FD15"));
    QVERIFY(matches(frameGet(1, fiioCmdName, {0xA0}), *fd15));
    QVERIFY(!matches(frameGet(1, fiioCmdName, {0xA2}), *fd15));
    QCOMPARE(nameOfPayload(bytes("a2")), std::string());
}

void FiioTest::backupRoundTrip()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const std::string dir = (root.path() + QStringLiteral("/config/casdap")).toStdString();
    const FiioModel &tiny = fiioModels().front();
    QCOMPARE(backupName(tiny, 3), std::string("fiio-2972-0149-slot162-3.txt"));
    QVERIFY(!newestBackup(dir, tiny));

    FiioBackup backup = sampleBackup();
    const std::optional<std::string> first = writeBackup(dir, tiny, backup);
    QVERIFY(first);
    QCOMPARE(*first, dir + "/fiio-2972-0149-slot162-1.txt");
    QVERIFY(!QFileInfo::exists(QString::fromStdString(*first + ".tmp")));
    QFile file(QString::fromStdString(*first));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray text = file.readAll();
    QVERIFY(text.startsWith("model=2972:0149\nfirmware=1.3.0\ndate=2026-10-11T12:00:00Z\nname=FH3\npreamp=0050\n"));
    QVERIFY(text.contains("band9=090014319c006400\n"));
    const QFileDevice::Permissions mode = QFileInfo(file).permissions()
                                          & (QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner
                                             | QFileDevice::ReadGroup | QFileDevice::WriteGroup
                                             | QFileDevice::ExeGroup | QFileDevice::ReadOther
                                             | QFileDevice::WriteOther | QFileDevice::ExeOther);
    QCOMPARE(mode, QFileDevice::ReadOwner | QFileDevice::WriteOwner);

    const std::optional<FiioBackup> read = readBackup(*first);
    QVERIFY(read);
    QCOMPARE(read->model, backup.model);
    QCOMPARE(read->firmware, backup.firmware);
    QCOMPARE(read->date, backup.date);
    QCOMPARE(read->name, backup.name);
    QCOMPARE(read->preamp, backup.preamp);
    QVERIFY(read->bands == backup.bands);

    backup.date = "2026-10-12T08:00:00Z";
    QCOMPARE(writeBackup(dir, tiny, backup), first);
    QVERIFY(!QFileInfo::exists(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-2.txt")));

    backup.name = "FD15";
    const std::optional<std::string> second = writeBackup(dir, tiny, backup);
    QCOMPARE(second, std::optional<std::string>(dir + "/fiio-2972-0149-slot162-2.txt"));
    QVERIFY(writeFile(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-x.txt"), "junk\n"));
    QVERIFY(writeFile(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot161-9.txt"), "junk\n"));
    QCOMPARE(newestBackup(dir, tiny), second);
    backup.bands[0][1] = 0x10;
    QCOMPARE(writeBackup(dir, tiny, backup), std::optional<std::string>(dir + "/fiio-2972-0149-slot162-3.txt"));

    const QString broken = root.path() + QStringLiteral("/broken.txt");
    QVERIFY(writeFile(broken, text.left(text.indexOf("band9="))));
    QVERIFY(!readBackup(broken.toStdString()));
    QByteArray badHex = text;
    badHex.replace("preamp=0050", "preamp=00zz");
    QVERIFY(writeFile(broken, badHex));
    QVERIFY(!readBackup(broken.toStdString()));
    QVERIFY(!readBackup(dir + "/absent.txt"));

    FiioBackup wrong = sampleBackup();
    wrong.bands[2] = bytes("0102");
    QVERIFY(!writeBackup(dir, tiny, wrong));
    for (const char *name : {"FH3\n", "F=\x7f", "\x01" "AB", "\xc3\xa9"}) {
        wrong = sampleBackup();
        wrong.name = name;
        QVERIFY2(!writeBackup(dir, tiny, wrong), name);
    }
    wrong = sampleBackup();
    wrong.name = "A B~!";
    QVERIFY(writeBackup(dir, tiny, wrong));
    QVERIFY(writeFile(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-0099.txt"), text));
    QVERIFY(writeFile(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-0.txt"), text));
    QCOMPARE(newestBackup(dir, tiny), std::optional<std::string>(dir + "/fiio-2972-0149-slot162-4.txt"));
    QVERIFY(writeFile(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-5.txt.tmp"), "stale\n"));
    wrong.name = "STALE";
    QCOMPARE(writeBackup(dir, tiny, wrong), std::optional<std::string>(dir + "/fiio-2972-0149-slot162-5.txt"));
    QVERIFY(!QFileInfo::exists(root.path() + QStringLiteral("/config/casdap/fiio-2972-0149-slot162-5.txt.tmp")));
    QCOMPARE(readBackup(dir + "/fiio-2972-0149-slot162-5.txt")->name, std::string("STALE"));
    QVERIFY(!writeBackup(dir + "/fiio-2972-0149-slot162-5.txt", tiny, sampleBackup()));
    wrong = sampleBackup();
    wrong.model = "05ac:110a";
    QVERIFY(!writeBackup(dir, tiny, wrong));
}

void FiioTest::modelTable()
{
    QCOMPARE(fiioModels().size(), std::size_t(1));
    const FiioModel *tiny = fiioModel(0x2972, 0x0149);
    QVERIFY(tiny);
    QCOMPARE(tiny->interface, 0);
    QCOMPARE(tiny->epOut, uint8_t(0x05));
    QCOMPARE(tiny->epIn, uint8_t(0x86));
    QCOMPARE(tiny->reportId, uint8_t(7));
    QCOMPARE(tiny->slot, uint8_t(162));
    QCOMPARE(tiny->minGain, -12.0);
    QCOMPARE(tiny->maxGain, 12.0);
    QCOMPARE(tiny->minFreq, 20.0);
    QCOMPARE(tiny->maxFreq, 20000.0);
    QCOMPARE(tiny->minQ, 0.25);
    QCOMPARE(tiny->maxQ, 8.0);
    QCOMPARE(tiny->firmware, std::string_view("1.3.0"));
    QVERIFY(!fiioModel(0x05AC, 0x110A));

    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString sys = root.path() + QStringLiteral("/sys");
    QVERIFY(usbDevice(sys, QStringLiteral("3-1"), "2972", "0149"));
    QVERIFY(hidInterface(sys, QStringLiteral("3-1"), "03", true));
    QVERIFY(soundCard(sys, QStringLiteral("3-1"), 1));
    QVERIFY(usbDevice(sys, QStringLiteral("1-1"), "05ac", "110a"));
    QVERIFY(hidInterface(sys, QStringLiteral("1-1"), "03", true));
    QVERIFY(soundCard(sys, QStringLiteral("1-1"), 2));
    QVERIFY(usbDevice(sys, QStringLiteral("2-1"), "2972", "0149"));
    QVERIFY(hidInterface(sys, QStringLiteral("2-1"), "01", true));
    QVERIFY(soundCard(sys, QStringLiteral("2-1"), 3));
    QVERIFY(usbDevice(sys, QStringLiteral("4-1"), "2972", "0149"));
    QVERIFY(hidInterface(sys, QStringLiteral("4-1"), "03", false));
    QVERIFY(soundCard(sys, QStringLiteral("4-1"), 4));
    QVERIFY(usbDevice(sys, QStringLiteral("6-1"), "2972", "0149"));
    QVERIFY(hidInterface(sys, QStringLiteral("6-1"), "03", true, false));
    QVERIFY(soundCard(sys, QStringLiteral("6-1"), 5));

    const std::string s = sys.toStdString();
    const std::optional<FiioMatch> fiio = supportedModel(s, 1);
    QVERIFY(fiio);
    QCOMPARE(fiio->model, tiny);
    QVERIFY(QString::fromStdString(fiio->devPath).endsWith(QStringLiteral("/devices/platform/xhci/3-1")));
    QVERIFY(!supportedModel(s, 2));
    QVERIFY(!supportedModel(s, 3));
    QVERIFY(!supportedModel(s, 4));
    QVERIFY(!supportedModel(s, 5));
    QVERIFY(!supportedModel(s, 0));
    QVERIFY(!supportedModel(s + "/absent", 1));
}

void FiioTest::usbEnumeration()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString sys = root.path() + QStringLiteral("/sys");
    const std::string s = sys.toStdString();
    QVERIFY(usbDevicesByModel(s).empty());
    QVERIFY(usbDevicesByModel(s + "/absent").empty());

    QVERIFY(usbDevice(sys, QStringLiteral("3-1"), "2972", "0149"));
    QVERIFY(usbDevice(sys, QStringLiteral("1-1"), "05ac", "110a"));
    QVERIFY(QDir().mkpath(sys + QStringLiteral("/bus/usb/devices/usb3")));
    QVERIFY(link(sys + QStringLiteral("/devices/platform/xhci/3-1/3-1:1.0"),
                 sys + QStringLiteral("/bus/usb/devices/3-1:1.0")));

    std::vector<FiioMatch> found = usbDevicesByModel(s);
    QCOMPARE(found.size(), std::size_t(1));
    QCOMPARE(found.front().model, fiioModel(0x2972, 0x0149));
    QVERIFY(QString::fromStdString(found.front().devPath).endsWith(QStringLiteral("/devices/platform/xhci/3-1")));
    QVERIFY(!interfaceSupported(found.front().devPath, *found.front().model));
    QVERIFY(!supportedModel(s, 1));

    QVERIFY(hidInterface(sys, QStringLiteral("3-1"), "03", true));
    QVERIFY(interfaceSupported(found.front().devPath, *found.front().model));

    QVERIFY(usbDevice(sys, QStringLiteral("5-1"), "2972", "0149"));
    found = usbDevicesByModel(s);
    QCOMPARE(found.size(), std::size_t(2));
    QVERIFY(found[0].devPath < found[1].devPath);
}

void FiioTest::fakeRejectsShortRead()
{
    Rig rig;
    rig.fakeErrorsExpected = true;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    uint8_t buf[128] = {};
    QCOMPARE(rig.fake.bulk(0x86, buf, 32, 50), -EINVAL);
    QCOMPARE(rig.fake.bulk(0x86, buf, 63, 50), -EINVAL);
    QCOMPARE(rig.fake.bulk(0x86, buf, 128, 50), -EINVAL);
    QCOMPARE(rig.fake.bulk(0x86, buf, 64, 0), -EINVAL);
    QCOMPARE(rig.fake.refusedReads(), 4);
    QCOMPARE(rig.fake.bulk(0x86, buf, 64, 20), -ETIMEDOUT);

    std::vector<uint8_t> bad = frameGet(1, fiioCmdPreset);
    bad[7] ^= 0xFF;
    QCOMPARE(rig.fake.bulk(0x05, bad.data(), int(bad.size()), 200), int(bad.size()));
    QCOMPARE(rig.fake.badFrames(), 1);
    QCOMPARE(rig.fake.pending(), 0);

    const FiioResult preset = rig.get(fiioCmdPreset);
    QCOMPARE(preset.reason, FiioReason::None);
    QCOMPARE(preset.payload, std::vector<uint8_t>{fiioBypass});
    QCOMPARE(rig.fake.refusedReads(), 4);
    for (const FakeUsbIo::Call &call : rig.fake.calls()) {
        if (call.op == FakeUsbIo::Op::Out)
            QCOMPARE(call.timeoutMs, 200);
    }
    const std::vector<FakeUsbIo::Call> calls = rig.fake.calls();
    QCOMPARE(calls.back().op, FakeUsbIo::Op::In);
    QCOMPARE(calls.back().len, fiioPacketSize);
    QCOMPARE(calls.back().result, fiioPacketSize);
    QVERIFY(calls.back().timeoutMs <= 500);
    QCOMPARE(calls[calls.size() - 2].op, FakeUsbIo::Op::Out);
    QCOMPARE(calls[calls.size() - 2].len, 9);
}

void FiioTest::fakeWedge()
{
    FakeUsbIo fake;
    QCOMPARE(fake.open("/dev/bus/usb/003/002"), 0);
    QCOMPARE(fake.claim(0), 0);
    std::vector<uint8_t> out = frameGet(1, fiioCmdBand, {1});
    QCOMPARE(fake.bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    uint8_t in[64] = {};
    QCOMPARE(fake.bulk(0x86, in, 64, 1), fiioPacketSize);
    QCOMPARE(parseReply(in, sizeof in)->cmd, fiioCmdBand);

    QCOMPARE(fake.bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QCOMPARE(fake.release(0), 0);
    fake.close();
    QCOMPARE(fake.pending(), 1);
    QCOMPARE(fake.bulk(0x86, in, 64, 10), -EBADF);
    QCOMPARE(fake.open("/dev/bus/usb/003/002"), 0);
    std::memset(in, 0, sizeof in);
    QCOMPARE(fake.bulk(0x86, in, 64, 10), fiioPacketSize);
    const std::optional<FiioReply> orphan = parseReply(in, sizeof in);
    QVERIFY(orphan);
    QCOMPARE(orphan->cmd, fiioCmdBand);
    QCOMPARE(orphan->payload[0], uint8_t(1));
    QVERIFY(orphan->crcValid);
    QCOMPARE(fake.bulk(0x86, in, 64, 10), -ETIMEDOUT);

    QCOMPARE(fake.bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QCOMPARE(fake.bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QVERIFY(fake.wedged());
    QCOMPARE(fake.bulk(0x86, in, 64, 10), -EOVERFLOW);
    QCOMPARE(fake.bulk(0x86, in, 64, 10), -EOVERFLOW);
    fake.close();
    QCOMPARE(fake.open("/dev/bus/usb/003/002"), 0);
    QCOMPARE(fake.bulk(0x86, in, 64, 10), -EOVERFLOW);
    QCOMPARE(fake.reset(), 0);
    QVERIFY(!fake.wedged());
    QCOMPARE(fake.pending(), 0);
    QCOMPARE(fake.bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QCOMPARE(fake.bulk(0x86, in, 64, 10), fiioPacketSize);
    fake.setResetError(-ENODEV);
    QCOMPARE(fake.reset(), -ENODEV);
}

void FiioTest::delayedReply()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setReplyDelayMs(1000);
    const int64_t start = fiioNowMs();
    const FiioResult preset = rig.get(fiioCmdPreset);
    const int64_t elapsed = fiioNowMs() - start;
    QCOMPARE(preset.reason, FiioReason::None);
    QCOMPARE(preset.payload, std::vector<uint8_t>{fiioBypass});
    QVERIFY2(elapsed >= 990 && elapsed < 1400, std::to_string(elapsed).c_str());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::In), 2);
    for (const FakeUsbIo::Call &call : rig.fake.calls()) {
        if (call.op != FakeUsbIo::Op::In)
            continue;
        QCOMPARE(call.len, fiioPacketSize);
        QVERIFY(call.timeoutMs > 0 && call.timeoutMs <= 500);
    }
    QCOMPARE(rig.fake.pending(), 0);
}

void FiioTest::lateReplyTimeout()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setReplyDelayMs(3500);
    const int64_t start = fiioNowMs();
    const FiioResult preset = rig.get(fiioCmdPreset);
    const int64_t elapsed = fiioNowMs() - start;
    QCOMPARE(preset.reason, FiioReason::Timeout);
    QVERIFY(preset.payload.empty());
    QVERIFY2(elapsed >= 2990 && elapsed < 3400, std::to_string(elapsed).c_str());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QVERIFY(rig.fake.count(FakeUsbIo::Op::In) >= 6);
    QCOMPARE(rig.fake.pending(), 1);
    QVERIFY(!rig.fake.wedged());
    QCOMPARE(reasonText(preset.reason), std::string_view("timeout"));

    const FiioResult next = rig.get(fiioCmdPreset);
    QCOMPARE(next.reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QVERIFY(!rig.fake.wedged());
    QCOMPARE(reasonText(next.reason), std::string_view("refused"));
}

void FiioTest::deadline()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, fiioNowMs() - 1), FiioReason::Deadline);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Open), 0);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, fiioNowMs() + 1000), FiioReason::None);

    rig.fake.setReplyDelayMs(1000);
    QCOMPARE(rig.device.transact(frameGet(1, fiioCmdPreset), fiioNowMs() - 1).reason, FiioReason::Deadline);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 0);

    const int64_t start = fiioNowMs();
    const FiioResult late = rig.device.transact(frameGet(2, fiioCmdPreset), start + 300);
    const int64_t elapsed = fiioNowMs() - start;
    QCOMPARE(late.reason, FiioReason::Deadline);
    QVERIFY2(elapsed >= 290 && elapsed < 600, std::to_string(elapsed).c_str());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    for (const FakeUsbIo::Call &call : rig.fake.calls()) {
        if (call.op == FakeUsbIo::Op::In || call.op == FakeUsbIo::Op::Out)
            QVERIFY(call.timeoutMs > 0 && call.timeoutMs <= 300);
    }
    QCOMPARE(rig.fake.pending(), 1);
    QCOMPARE(reasonText(FiioReason::Deadline), std::string_view("deadline"));
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QVERIFY(!rig.fake.wedged());
}

void FiioTest::outErrors()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setOutError(-ETIMEDOUT);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Timeout);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);

    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setOutError(-ETIMEDOUT);
    QCOMPARE(rig.device.transact(frameGet(20, fiioCmdPreset), fiioNowMs() + 100).reason, FiioReason::Deadline);
    QVERIFY(rig.fake.calls().back().timeoutMs < 200);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 2);

    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setOutError(-EPIPE);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Device);
    QCOMPARE(rig.logged("out failed"), 1);
    const FiioResult preset = rig.get(fiioCmdPreset);
    QCOMPARE(preset.reason, FiioReason::None);
    QCOMPARE(preset.payload, std::vector<uint8_t>{fiioBypass});
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 4);
    QCOMPARE(rig.fake.pending(), 0);
}

void FiioTest::inErrorDuringWait()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setReplyDelayMs(1000);
    std::thread unplug([&rig] {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        rig.fake.simulateReplug();
    });
    const int64_t start = fiioNowMs();
    const FiioResult result = rig.get(fiioCmdPreset);
    const int64_t elapsed = fiioNowMs() - start;
    unplug.join();
    QCOMPARE(result.reason, FiioReason::Device);
    QVERIFY2(elapsed >= 150 && elapsed < 600, std::to_string(elapsed).c_str());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QCOMPARE(rig.fake.calls().back().op, FakeUsbIo::Op::In);
    QCOMPARE(rig.fake.calls().back().result, -ENODEV);
    QCOMPARE(rig.logged("in failed"), 1);
}

void FiioTest::discardCounted()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QCOMPARE(rig.device.discarded(), 0);

    rig.fake.strayBeforeReply({bytes("07bb0b00931508090014319c00640075ee"), bytes("07bb0b00761601a09cee"),
                               bytes("0100ff")});
    const FiioResult band = rig.get(fiioCmdBand, {5});
    QCOMPARE(band.reason, FiioReason::None);
    QCOMPARE(band.payload, rig.fake.band(162, 5));
    QCOMPARE(rig.device.discarded(), 3);
    QCOMPARE(rig.logged("discarded "), 3);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 2);
    QCOMPARE(rig.fake.pending(), 0);
    QVERIFY(std::find(rig.log.begin(), rig.log.end(), "discarded 07bb0b00931508090014319c00640075ee")
            != rig.log.end());

    rig.fake.strayBeforeReply({bytes("07aa0a00751601a0e5ee")});
    const FiioResult ack = rig.set(fiioCmdPreset, presetPayload(fiioBypass));
    QCOMPARE(ack.reason, FiioReason::Ack);
    QCOMPARE(rig.logged("ack mismatch "), 1);
    QCOMPARE(rig.device.discarded(), 3);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 3);
    QCOMPARE(reasonText(ack.reason), std::string_view("ack"));
}

void FiioTest::descriptorMismatch()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    rig.fake.setDescriptor(0x05AC, 0x110A);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
    const std::vector<FakeUsbIo::Call> calls = rig.fake.calls();
    QCOMPARE(calls.size(), std::size_t(3));
    QCOMPARE(calls[0].op, FakeUsbIo::Op::Open);
    QCOMPARE(calls[0].path, std::string("/dev/bus/usb/003/002"));
    QCOMPARE(calls[1].op, FakeUsbIo::Op::Descriptor);
    QCOMPARE(calls[2].op, FakeUsbIo::Op::Close);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Claim), 0);
    QVERIFY(!rig.fake.isOpen());
    QCOMPARE(rig.logged("descriptor 05ac:110a, not 2972:0149"), 1);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Device);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 0);

    rig.fake.setDescriptor(0x2972, 0x0149);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(rig.fake.claimed());
    QCOMPARE(rig.fake.calls().back().len, 0);
    rig.device.releaseClose();
    QVERIFY(!rig.fake.claimed());
    QVERIFY(!rig.fake.isOpen());
    const std::vector<FakeUsbIo::Call> after = rig.fake.calls();
    QCOMPARE(after[after.size() - 2].op, FakeUsbIo::Op::Release);
    QCOMPARE(after[after.size() - 1].op, FakeUsbIo::Op::Close);
    rig.device.releaseClose();
    QCOMPARE(rig.fake.calls().size(), after.size());

    QVERIFY(usbNode(rig.root.path() + QStringLiteral("/9-9"), "12", "107"));
    QCOMPARE(rig.device.openClaimed((rig.root.path() + QStringLiteral("/9-9")).toStdString(), std::nullopt),
             FiioReason::None);
    QCOMPARE(rig.fake.calls()[after.size()].path, std::string("/dev/bus/usb/012/107"));
    QCOMPARE(rig.device.openClaimed((rig.root.path() + QStringLiteral("/absent")).toStdString(), std::nullopt),
             FiioReason::Device);

    rig.fake.setDescriptorError(-EIO);
    const int claims = rig.fake.count(FakeUsbIo::Op::Claim);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
    QCOMPARE(rig.logged("descriptor read failed 5"), 1);
    QCOMPARE(rig.logged("descriptor 0000:0000"), 0);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Claim), claims);
    QVERIFY(!rig.fake.isOpen());
}

void FiioTest::overflow()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    std::vector<uint8_t> unread = frameGet(40, fiioCmdBand, {1});
    QCOMPARE(rig.fake.bulk(0x05, unread.data(), int(unread.size()), 200), int(unread.size()));

    const int64_t start = fiioNowMs();
    const FiioResult result = rig.get(fiioCmdPreset);
    QCOMPARE(result.reason, FiioReason::Overflow);
    QVERIFY(fiioNowMs() - start < 400);
    QVERIFY(rig.fake.wedged());
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 1);
    QCOMPARE(rig.fake.calls().back().result, -EOVERFLOW);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Reset), 0);
    QCOMPARE(rig.logged("in overflow"), 1);
    QCOMPARE(reasonText(result.reason), std::string_view("overflow"));

    rig.device.releaseClose();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Overflow);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Reset), 0);
}

void FiioTest::claimBusy()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    rig.fake.setClaimError(-EBUSY);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Claim);
    QVERIFY(!rig.fake.isOpen());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Claim), 1);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Close), 1);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Release), 0);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Device);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 0);
    QCOMPARE(reasonText(FiioReason::Claim), std::string_view("claim"));
    rig.fake.setClaimError(-ENODEV);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
}

void FiioTest::permission()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    rig.fake.setOpenError(-EACCES);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Permission);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Descriptor), 0);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Close), 0);
    rig.fake.setOpenError(-ENOENT);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
    rig.fake.setOpenError(0);
    rig.fake.setClaimError(-EACCES);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Permission);
    QVERIFY(!rig.fake.isOpen());
    QCOMPARE(reasonText(FiioReason::Permission), std::string_view("permission"));
    QCOMPARE(reasonText(FiioReason::HidBound), std::string_view("hid bound"));
    QCOMPARE(reasonText(FiioReason::Device), std::string_view("device"));
}

void FiioTest::namesUnderBypass()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.get(fiioCmdPreset).payload, std::vector<uint8_t>{fiioBypass});
    const struct
    {
        uint8_t preset;
        const char *name;
    } customSlots[] = {{160, "FD15"}, {161, "FF5"}, {162, "FH3"}};
    for (const auto &slot : customSlots) {
        const FiioResult name = rig.get(fiioCmdName, {slot.preset});
        QCOMPARE(name.reason, FiioReason::None);
        QCOMPARE(name.payload.size(), std::size_t(1 + fiioNameLength));
        QCOMPARE(name.payload[0], slot.preset);
        QCOMPARE(nameOfPayload(name.payload), std::string(slot.name));
    }
    const FiioResult renamed = rig.set(fiioCmdName, namePayload(162, "CASDAP"));
    QCOMPARE(renamed.reason, FiioReason::None);
    QCOMPARE(renamed.payload, namePayload(162, "CASDAP"));
    QCOMPARE(nameOfPayload(rig.get(fiioCmdName, {162}).payload), std::string("CASDAP"));
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(rig.get(fiioCmdPreamp).payload, bytes("0014"));
    const FiioResult version = rig.get(fiioCmdVersion);
    QCOMPARE(version.reason, FiioReason::None);
    QCOMPARE(version.payload, bytes("010300"));
    QCOMPARE(rig.logged("reply crc mismatch"), 0);
}

void FiioTest::bandsZeroUnderBypass()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    for (int i = 0; i < fiioBandCount; ++i) {
        const FiioResult band = rig.get(fiioCmdBand, {uint8_t(i)});
        QCOMPARE(band.reason, FiioReason::None);
        std::vector<uint8_t> zero(fiioBandPayloadSize, 0);
        zero[0] = uint8_t(i);
        QCOMPARE(band.payload, zero);
    }
    for (const char *recorded : {"07bb0b004515080100000000000000f2ee", "07bb0b004d1508090000000000000026ee"}) {
        const std::optional<FiioReply> reply = parse(packet(recorded));
        QVERIFY2(reply && reply->crcValid, recorded);
        QCOMPARE(reply->cmd, fiioCmdBand);
        QCOMPARE(rig.get(fiioCmdBand, {reply->payload[0]}).payload, reply->payload);
    }
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QCOMPARE(rig.get(fiioCmdBand, {9}).payload, bytes("0900003e80004600"));
    const std::vector<uint8_t> payload = bytes("09fff12ee0004702");
    const std::vector<uint8_t> stored = bytes("09fff12ee0004602");
    QCOMPARE(rig.set(fiioCmdBand, payload).reason, FiioReason::None);
    QCOMPARE(rig.get(fiioCmdBand, {9}).payload, stored);
    QCOMPARE(rig.fake.band(161, 9), bytes("0900003e80004600"));
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(fiioBypass)).reason, FiioReason::None);
    QCOMPARE(rig.get(fiioCmdBand, {9}).payload, bytes("0900000000000000"));
    QCOMPARE(rig.set(fiioCmdBand, bytes("0900140fa0006400")).reason, FiioReason::None);
    QCOMPARE(rig.get(fiioCmdBand, {9}).payload, bytes("0900000000000000"));
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QCOMPARE(rig.get(fiioCmdBand, {9}).payload, stored);
}

void FiioTest::replugPersistence()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QCOMPARE(rig.set(fiioCmdName, namePayload(162, "CASDAP")).reason, FiioReason::None);
    const std::vector<uint8_t> band = bytes("04ffc407d0008d01");
    QCOMPARE(rig.set(fiioCmdBand, band).reason, FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreamp, bytes("ffe2")).reason, FiioReason::None);
    QCOMPARE(rig.fake.counter(), uint16_t(4));

    rig.fake.simulateReplug();
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Device);
    rig.device.releaseClose();
    QCOMPARE(rig.fake.counter(), uint16_t(0));
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.get(fiioCmdPreset).payload, std::vector<uint8_t>{162});
    const FakeUsbIo::Call first = rig.fake.calls().back();
    QCOMPARE(first.op, FakeUsbIo::Op::In);
    QCOMPARE(parseReply(first.data.data(), first.data.size())->seq, uint16_t(0));
    QCOMPARE(nameOfPayload(rig.get(fiioCmdName, {162}).payload), std::string("CASDAP"));
    QCOMPARE(rig.get(fiioCmdBand, {4}).payload, band);
    QCOMPARE(rig.get(fiioCmdPreamp).payload, bytes("ffe2"));
    QCOMPARE(rig.fake.name(161), std::string("FF5"));
    QCOMPARE(rig.fake.preamp(162), bytes("ffe2"));
}

void FiioTest::qRounding()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);

    QCOMPARE(rig.set(fiioCmdBand, bytes("09fff12ee0004702")).reason, FiioReason::None);
    const FakeUsbIo::Call ack = rig.fake.calls().back();
    QCOMPARE(ack.op, FakeUsbIo::Op::In);
    QCOMPARE(hexOf(parseReply(ack.data.data(), ack.data.size())->payload), QByteArray("09fff12ee0004602"));
    QCOMPARE(hexOf(rig.get(fiioCmdBand, {9}).payload), QByteArray("09fff12ee0004602"));

    const struct
    {
        const char *sent;
        const char *stored;
    } recorded[] = {
        {"0019", "0019"}, {"004b", "004b"}, {"008d", "008d"}, {"0047", "0046"},
        {"012c", "012d"}, {"0320", "0323"}, {"03e8", "03eb"},
    };
    for (const auto &point : recorded) {
        const QByteArray sent = QByteArray("0300003e80") + point.sent + "00";
        const QByteArray stored = QByteArray("0300003e80") + point.stored + "00";
        const FiioResult result = rig.set(fiioCmdBand, bytes(sent.constData()));
        QVERIFY2(result.reason == FiioReason::None, point.sent);
        QCOMPARE(hexOf(result.payload), stored);
        QCOMPARE(hexOf(rig.get(fiioCmdBand, {3}).payload), stored);
        QCOMPARE(hexOf(rig.fake.band(162, 3)), stored);
    }
}

void FiioTest::holdRelease()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const std::shared_ptr<FakeUsbIo::Hold> held = rig.fake.hold(FakeUsbIo::Op::Out);
    FiioResult result{FiioReason::Refused, {}};
    std::thread worker([&rig, &result] { result = rig.device.transact(frameGet(7, fiioCmdPreset), std::nullopt); });
    struct Guard
    {
        ~Guard()
        {
            held->release();
            if (worker.joinable())
                worker.join();
        }
        std::shared_ptr<FakeUsbIo::Hold> held;
        std::thread &worker;
    } guard{held, worker};
    QVERIFY(held->waitEntered(2000));
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 0);
    QCOMPARE(rig.fake.preset(), fiioBypass);
    held->release();
    worker.join();
    QCOMPARE(result.reason, FiioReason::None);
    QCOMPARE(result.payload, std::vector<uint8_t>{fiioBypass});
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::None);
}

void FiioTest::usbfsNoDevice()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    UsbfsIo io;
    uint16_t vid = 0;
    uint16_t pid = 0;
    uint8_t buf[64] = {};
    QCOMPARE(io.readDescriptor(vid, pid), -EBADF);
    QCOMPARE(io.claim(0), -EBADF);
    QCOMPARE(io.release(0), -EBADF);
    QCOMPARE(io.bulk(0x86, buf, 64, 10), -EBADF);
    QCOMPARE(io.reset(), -EBADF);
    QCOMPARE(io.open((root.path() + QStringLiteral("/absent")).toStdString()), -ENOENT);
    const QString notUsb = root.path() + QStringLiteral("/desc");
    QVERIFY(writeFile(notUsb, QByteArray(18, '\x01')));
    QCOMPARE(io.open(notUsb.toStdString()), 0);
    QCOMPARE(io.readDescriptor(vid, pid), -EIO);
    QVERIFY(io.claim(0) < 0);
    io.close();
    QCOMPARE(io.claim(0), -EBADF);
}

QTEST_GUILESS_MAIN(FiioTest)
#include "tst_fiio.moc"
