#include "FakeUsbIo.h"
#include "Fiio.h"
#include "FiioWorker.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>
#include <memory>
#include <thread>

#include <unistd.h>

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

FiioPaths pathsIn(const QString &root)
{
    FiioPaths paths;
    paths.runDir = (root + QStringLiteral("/run")).toStdString();
    paths.runDirExists = QDir().mkpath(root + QStringLiteral("/run"));
    paths.stateDir = (root + QStringLiteral("/state/casdap")).toStdString();
    paths.configDir = (root + QStringLiteral("/config/casdap")).toStdString();
    paths.sysRoot = (root + QStringLiteral("/sys")).toStdString();
    paths.asoundDir = (root + QStringLiteral("/asound")).toStdString();
    return paths;
}

struct Rig
{
    explicit Rig(FiioDevice::Bounds bounds = {})
        : paths(pathsIn(root.path()))
        , device(fake, tinyModel(), bounds, paths)
    {
        sysPath = (root.path() + QStringLiteral("/3-1")).toStdString();
        nodeReady = usbNode(root.path() + QStringLiteral("/3-1"), "3", "2");
        device.setLog([this](const std::string &line) { log.push_back(line); });
        fake.setOutCheck([this] { return raw || !checkStamp || stamped(); });
    }

    bool rawOut(const std::vector<uint8_t> &frame)
    {
        std::vector<uint8_t> out = frame;
        raw = true;
        const bool ok = fake.bulk(0x05, out.data(), int(out.size()), 200) == int(out.size());
        raw = false;
        return ok;
    }

    InflightStamp stamp() const
    {
        return InflightStamp(paths, sysPath);
    }

    bool marker() const
    {
        return QFileInfo::exists(QString::fromStdString(fiioMarkerPath(paths)));
    }

    bool touchMarker() const
    {
        return writeFile(QString::fromStdString(fiioMarkerPath(paths)), "2972:0149\n");
    }

    bool stamped() const
    {
        return QFileInfo::exists(QString::fromStdString(stamp().path()));
    }

    ~Rig()
    {
        if (fakeErrorsExpected)
            return;
        QCOMPARE(fake.refusedReads(), 0);
        QCOMPARE(fake.badFrames(), 0);
        QCOMPARE(fake.outCheckFailures(), 0);
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
    FiioPaths paths;
    FakeUsbIo fake;
    FiioDevice device;
    std::string sysPath;
    std::vector<std::string> log;
    bool nodeReady = false;
    bool fakeErrorsExpected = false;
    bool checkStamp = true;
    bool raw = false;
    uint16_t seq = 0;
};

QStringList outs(const FakeUsbIo &fake, int skip = 0)
{
    QStringList out;
    for (const FakeUsbIo::Call &call : fake.calls()) {
        if (call.op != FakeUsbIo::Op::Out || call.data.size() < 9)
            continue;
        if (skip > 0) {
            --skip;
            continue;
        }
        QString line = QLatin1String(call.data[1] == 0xAA ? "set " : "get ")
                       + QString::asprintf("%02x", call.data[5]);
        const std::vector<uint8_t> payload(call.data.begin() + 7, call.data.begin() + 7 + call.data[6]);
        if (!payload.empty())
            line += QLatin1Char(' ') + QString::fromLatin1(hexOf(payload));
        out.append(line);
    }
    return out;
}

FiioProfileFrames testFrames(double shift = 0.0)
{
    FiioProfileFrames frames;
    for (int i = 0; i < fiioBandCount; ++i) {
        FiioBand band;
        band.index = i;
        band.gain = (i % 5) - 2 + shift;
        band.freq = 31.25 * (1 << i);
        band.q = i == 0 ? 0.71 : 1.0;
        frames.bands[i] = bandPayload(tinyModel(), band);
    }
    return frames;
}

QString bandSet(const std::vector<uint8_t> &payload)
{
    return QStringLiteral("set 15 ") + QString::fromLatin1(hexOf(payload));
}

QStringList bandReads()
{
    QStringList out;
    for (int i = 0; i < fiioBandCount; ++i)
        out.append(QStringLiteral("get 15 %1").arg(i, 2, 10, QLatin1Char('0')));
    return out;
}

QStringList bandSets(const FiioProfileFrames &frames)
{
    QStringList out;
    for (const std::vector<uint8_t> &band : frames.bands)
        out.append(bandSet(band));
    return out;
}

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

QMutex messageMutex;
QStringList *capturedMessages = nullptr;
QtMessageHandler previousHandler = nullptr;

void captureMessage(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    QtMessageHandler previous = nullptr;
    {
        QMutexLocker locker(&messageMutex);
        if (capturedMessages)
            capturedMessages->append(message);
        previous = previousHandler;
    }
    if (previous)
        previous(type, context, message);
}

struct Capture
{
    Capture()
    {
        QMutexLocker locker(&messageMutex);
        capturedMessages = &messages;
        previousHandler = qInstallMessageHandler(captureMessage);
    }

    ~Capture()
    {
        QMutexLocker locker(&messageMutex);
        qInstallMessageHandler(previousHandler);
        capturedMessages = nullptr;
    }

    int count(const QString &line)
    {
        QMutexLocker locker(&messageMutex);
        return int(messages.count(line));
    }

    QStringList messages;
};

FiioWorker::Bounds workerBounds(int driverWaitMs = 2000, int lockMs = 1000, int retryMs = 500)
{
    FiioWorker::Bounds bounds;
    bounds.driverWaitMs = driverWaitMs;
    bounds.lockMs = lockMs;
    bounds.retryMs = retryMs;
    return bounds;
}

struct WorkerRig
{
    struct Done
    {
        quint64 seq = 0;
        bool ok = false;
        FiioReason reason = FiioReason::None;
        FiioMirror mirror;
    };

    explicit WorkerRig(FiioWorker::Bounds bounds = workerBounds())
        : paths(pathsIn(root.path()))
    {
        const QString sys = root.path() + QStringLiteral("/sys");
        devPath = sys + QStringLiteral("/devices/platform/xhci/3-1");
        iface = devPath + QStringLiteral("/3-1:1.0");
        ready = usbDevice(sys, QStringLiteral("3-1"), "2972", "0149") && hidInterface(sys, QStringLiteral("3-1"), "03", true)
                && usbNode(devPath, "3", "2");
        auto owned = std::make_unique<FakeUsbIo>();
        fake = owned.get();
        worker = std::make_unique<FiioWorker>(std::move(owned), bounds, paths);
        QObject::connect(worker.get(), &FiioWorker::batchDone, worker.get(),
                         [this](quint64 seq, bool ok, int reason, const FiioMirror &mirror) {
                             done.push_back({seq, ok, FiioReason(reason), mirror});
                         });
    }

    ~WorkerRig()
    {
        QCOMPARE(fake->refusedReads(), 0);
        QCOMPARE(fake->badFrames(), 0);
    }

    FiioWorker::Target target(FiioWorker::Kind kind, const FiioProfileFrames &frames = testFrames())
    {
        FiioWorker::Target out;
        out.cardId = QStringLiteral("B");
        out.generation = 1;
        out.alsaIndex = 1;
        out.sysPath = devPath;
        out.kind = kind;
        out.frames = frames;
        out.seq = ++seq;
        return out;
    }

    quint64 post(FiioWorker::Kind kind, const FiioProfileFrames &frames = testFrames())
    {
        const FiioWorker::Target out = target(kind, frames);
        worker->post(out);
        return out.seq;
    }

    const Done *result(quint64 wanted) const
    {
        for (const Done &d : done) {
            if (d.seq == wanted)
                return &d;
        }
        return nullptr;
    }

    bool waitDone(quint64 wanted, int ms)
    {
        return QTest::qWaitFor([this, wanted] { return result(wanted) != nullptr; }, ms);
    }

    bool bindDriver() const
    {
        const QString driver = root.path() + QStringLiteral("/sys/bus/usb/drivers/usbhid");
        return QDir().mkpath(driver) && link(driver, iface + QStringLiteral("/driver"));
    }

    bool unbindDriver() const
    {
        return QFile::remove(iface + QStringLiteral("/driver"));
    }

    bool pcmStatus(const QByteArray &text) const
    {
        return writeFile(root.path() + QStringLiteral("/asound/card1/pcm0p/sub0/status"), text);
    }

    QTemporaryDir root;
    FiioPaths paths;
    QString devPath;
    QString iface;
    bool ready = false;
    quint64 seq = 0;
    std::vector<Done> done;
    FakeUsbIo *fake = nullptr;
    std::unique_ptr<FiioWorker> worker;
};

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
    void pathsFromEnv();
    void takeOrder();
    void takeKnownSlot();
    void numberedBackups();
    void backupFailure();
    void updateDiff();
    void updateNotSelected();
    void abandonBetween();
    void killMidTransaction();
    void bypassDeletesMarker();
    void forceOverPreset0();
    void forceAt240();
    void dumpReads();
    void stampDrain();
    void stampPerDevice();
    void restoreAbandon();
    void restoreForeignAbandon();
    void noStampDrain();
    void stampUnparsable();
    void drainOverflowTerminal();
    void drainCap();
    void deadlines();
    void lockPeer();
    void restoreSlotVerified();
    void driverWaitNeedsInterface();
    void workerLatestWins();
    void workerAbandon();
    void workerAbandonInWaits();
    void workerDriverWait();
    void workerLockRetry();
    void workerClaimRetry();
    void workerResetPcmOpen();
    void workerReset();
    void workerResetFails();
    void quitSendsBypass();
    void workerQuitJoin();
    void workerGuiNeverBlocks();
    void workerResetExHeld();
    void workerMirrorPerGeneration();
    void workerQuitDeadline();
    void workerUpdate();
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
    QVERIFY(rig.rawOut(bad));
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
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 2);
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
    FiioDevice::Bounds bounds;
    bounds.stampWindowMs = 300;
    Rig rig(bounds);
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setOutError(-ETIMEDOUT);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Timeout);
    QVERIFY(rig.stamped());
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
    QVERIFY(rig.stamped());
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
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
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 1);
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
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Claim), 1);
    QCOMPARE(rig.fake.calls().back().op, FakeUsbIo::Op::In);
    QCOMPARE(rig.fake.calls().back().timeoutMs, 40);
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
    QVERIFY(rig.rawOut(unread));

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
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Overflow);
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
    FiioDevice::Bounds bounds;
    bounds.stampWindowMs = 300;
    Rig rig(bounds);
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

void FiioTest::pathsFromEnv()
{
    const char *names[] = {"CASDAP_RUN_DIR", "HOME", "XDG_STATE_HOME", "XDG_CONFIG_HOME", "CASDAP_SYS_ROOT",
                           "CASDAP_ASOUND_DIR"};
    struct Saved
    {
        ~Saved()
        {
            for (std::size_t i = 0; i < values.size(); ++i) {
                if (set[i])
                    qputenv(keys[i], values[i]);
                else
                    qunsetenv(keys[i]);
            }
        }
        std::vector<const char *> keys;
        std::vector<QByteArray> values;
        std::vector<bool> set;
    } saved;
    for (const char *name : names) {
        saved.keys.push_back(name);
        saved.set.push_back(qEnvironmentVariableIsSet(name));
        saved.values.push_back(qgetenv(name));
    }
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray r = root.path().toUtf8();
    qputenv("CASDAP_RUN_DIR", r + "/run");
    qputenv("HOME", r + "/home");
    qputenv("XDG_STATE_HOME", r + "/xstate");
    qputenv("XDG_CONFIG_HOME", r + "/xconfig");
    qputenv("CASDAP_SYS_ROOT", r + "/sys");
    qputenv("CASDAP_ASOUND_DIR", r + "/asound");
    FiioPaths paths = fiioPathsFromEnv();
    QCOMPARE(paths.runDir, (r + "/run").toStdString());
    QVERIFY(!paths.runDirExists);
    QCOMPARE(paths.stateDir, (r + "/xstate/casdap").toStdString());
    QCOMPARE(paths.configDir, (r + "/xconfig/casdap").toStdString());
    QCOMPARE(paths.sysRoot, (r + "/sys").toStdString());
    QCOMPARE(paths.asoundDir, (r + "/asound").toStdString());
    QCOMPARE(fiioMarkerPath(paths), (r + "/xstate/casdap/fiio-taking").toStdString());
    QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/run")));
    QVERIFY(fiioPathsFromEnv().runDirExists);

    qunsetenv("CASDAP_RUN_DIR");
    qunsetenv("XDG_STATE_HOME");
    qputenv("XDG_CONFIG_HOME", "");
    qunsetenv("CASDAP_SYS_ROOT");
    qunsetenv("CASDAP_ASOUND_DIR");
    paths = fiioPathsFromEnv();
    QCOMPARE(paths.runDir, "/run/user/" + std::to_string(::getuid()));
    QCOMPARE(paths.stateDir, (r + "/home/.local/state/casdap").toStdString());
    QCOMPARE(paths.configDir, (r + "/home/.config/casdap").toStdString());
    QCOMPARE(paths.sysRoot, std::string("/sys"));
    QCOMPARE(paths.asoundDir, std::string("/proc/asound"));
}

void FiioTest::takeOrder()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const FiioProfileFrames frames = testFrames();
    FiioMirror mirror;
    const FiioResult result = FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt);
    QCOMPARE(result.reason, FiioReason::None);

    QStringList expected{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                         QStringLiteral("set 16 a2"), QStringLiteral("get 17")};
    expected += bandReads();
    expected += QStringLiteral("set 30 a24341534441500000");
    expected += QStringLiteral("set 17 0000");
    expected += bandSets(frames);
    QCOMPARE(outs(rig.fake), expected);

    QCOMPARE(rig.fake.preset(), uint8_t(162));
    QCOMPARE(rig.fake.name(162), std::string("CASDAP"));
    QCOMPARE(rig.fake.preamp(162), bytes("0000"));
    QCOMPARE(hexOf(rig.fake.band(162, 0)), hexOf(frames.bands[0]).replace("0047", "0046"));
    for (int i = 1; i < fiioBandCount; ++i)
        QCOMPARE(rig.fake.band(162, i), frames.bands[i]);
    QVERIFY(mirror.valid);
    QCOMPARE(mirror.preset, 162);
    QCOMPARE(mirror.name, std::string("CASDAP"));
    QCOMPARE(mirror.firmware, std::string("1.3.0"));
    QCOMPARE(mirror.preamp, bytes("0000"));
    QVERIFY(mirror.bands == frames.bands);
    QVERIFY(!rig.marker());
    QVERIFY(!rig.stamped());

    const std::optional<std::string> newest = newestBackup(rig.paths.configDir, tinyModel());
    QVERIFY(newest);
    QVERIFY(QString::fromStdString(*newest).endsWith(QStringLiteral("/fiio-2972-0149-slot162-1.txt")));
    const std::optional<FiioBackup> backup = readBackup(*newest);
    QVERIFY(backup);
    QCOMPARE(backup->name, std::string("FH3"));
    QCOMPARE(backup->firmware, std::string("1.3.0"));
    QCOMPARE(backup->preamp, bytes("0050"));
    QCOMPARE(backup->bands[9], bytes("0900003e80004600"));
    QCOMPARE(rig.logged("backup "), 1);
    QCOMPARE(rig.logged("take"), 1);
}

void FiioTest::takeKnownSlot()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const FiioProfileFrames frames = testFrames();
    FiioMirror mirror;
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    const int before = rig.fake.count(FakeUsbIo::Op::Out);

    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QStringList expected{QStringLiteral("get 16"), QStringLiteral("get 30 a2"), QStringLiteral("set 17 0000")};
    expected += bandSets(frames);
    QCOMPARE(outs(rig.fake, before), expected);
    QVERIFY(!QFileInfo::exists(QString::fromStdString(rig.paths.configDir)
                               + QStringLiteral("/fiio-2972-0149-slot162-2.txt")));

    QVERIFY(rig.touchMarker());
    FiioMirror fresh;
    const int second = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::take(rig.device, fresh, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QCOMPARE(outs(rig.fake, second).mid(0, 3),
             QStringList({QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2")}));
    QCOMPARE(outs(rig.fake, second).size(), 14);
    QVERIFY(!rig.marker());
    QCOMPARE(rig.logged("backup "), 1);

    FiioProfileFrames bad = frames;
    bad.bands[4][0] = 5;
    const int third = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::take(rig.device, fresh, rig.paths, bad, {}, std::nullopt).reason, FiioReason::Refused);
    QVERIFY(!fresh.valid);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), third);
}

void FiioTest::numberedBackups()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const FiioProfileFrames frames = testFrames();
    FiioMirror mirror;
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    const QString dir = QString::fromStdString(rig.paths.configDir);
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/fiio-2972-0149-slot162-1.txt")));

    QCOMPARE(rig.set(fiioCmdName, namePayload(162, "FH3")).reason, FiioReason::None);
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QVERIFY(QFileInfo::exists(dir + QStringLiteral("/fiio-2972-0149-slot162-2.txt")));
    const std::optional<FiioBackup> second = readBackup((dir + QStringLiteral("/fiio-2972-0149-slot162-2.txt")).toStdString());
    QVERIFY(second);
    QCOMPARE(second->name, std::string("FH3"));
    QCOMPARE(second->preamp, bytes("0000"));
    QCOMPARE(second->bands[3], frames.bands[3]);

    QCOMPARE(rig.set(fiioCmdName, namePayload(162, "FH3")).reason, FiioReason::None);
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QVERIFY(!QFileInfo::exists(dir + QStringLiteral("/fiio-2972-0149-slot162-3.txt")));
    QCOMPARE(QString::fromStdString(*newestBackup(rig.paths.configDir, tinyModel())),
             dir + QStringLiteral("/fiio-2972-0149-slot162-2.txt"));
    QCOMPARE(rig.fake.name(162), std::string("CASDAP"));
}

void FiioTest::backupFailure()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QVERIFY(writeFile(rig.root.path() + QStringLiteral("/blocked"), "file\n"));
    rig.paths.configDir = (rig.root.path() + QStringLiteral("/blocked/casdap")).toStdString();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    FiioMirror mirror;
    const FiioResult result = FiioBatch::take(rig.device, mirror, rig.paths, testFrames(), {}, std::nullopt);
    QCOMPARE(result.reason, FiioReason::Backup);
    QCOMPARE(reasonText(result.reason), std::string_view("backup"));
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(rig.fake.name(162), std::string("FH3"));
    QVERIFY(!rig.marker());
    QVERIFY(!mirror.valid);
    QCOMPARE(outs(rig.fake).last(), QStringLiteral("set 16 f0"));
    QCOMPARE(outs(rig.fake).filter(QStringLiteral("set 30")).size(), 0);
    QCOMPARE(rig.logged("backup failed"), 1);
    QCOMPARE(rig.logged("bypass acked"), 1);
}

void FiioTest::updateDiff()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    FiioProfileFrames frames = testFrames();
    FiioMirror mirror;
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);

    int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::update(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QCOMPARE(outs(rig.fake, before), QStringList{QStringLiteral("get 16")});

    const FiioProfileFrames moved = testFrames(1.5);
    frames.bands[2] = moved.bands[2];
    frames.bands[7] = moved.bands[7];
    before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::update(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QCOMPARE(outs(rig.fake, before),
             QStringList({QStringLiteral("get 16"), bandSet(frames.bands[2]), bandSet(frames.bands[7])}));
    QCOMPARE(rig.fake.band(162, 2), frames.bands[2]);
    QCOMPARE(rig.fake.band(162, 7), frames.bands[7]);
    QVERIFY(mirror.valid);
    QVERIFY(mirror.bands == frames.bands);
    QCOMPARE(rig.logged("update 2"), 1);
}

void FiioTest::updateNotSelected()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const FiioProfileFrames frames = testFrames();
    FiioMirror mirror;
    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);

    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(0)).reason, FiioReason::None);
    int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::update(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QStringList expected{QStringLiteral("get 16"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                         QStringLiteral("set 16 a2"), QStringLiteral("set 17 0000")};
    expected += bandSets(frames);
    QCOMPARE(outs(rig.fake, before), expected);
    QCOMPARE(rig.fake.preset(), uint8_t(162));
    QVERIFY(mirror.valid);

    mirror.valid = false;
    before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::update(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    expected = QStringList{QStringLiteral("get 16"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                           QStringLiteral("set 17 0000")};
    expected += bandSets(frames);
    QCOMPARE(outs(rig.fake, before), expected);
    QVERIFY(mirror.valid);
}

void FiioTest::abandonBetween()
{
    const FiioProfileFrames frames = testFrames();
    for (int k = 0; k <= 27; ++k) {
        Rig rig;
        QVERIFY(rig.nodeReady);
        QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
        FiioMirror mirror;
        const auto abandoned = [&rig, k] { return rig.fake.count(FakeUsbIo::Op::Out) >= k; };
        const FiioResult result = FiioBatch::take(rig.device, mirror, rig.paths, frames, abandoned, std::nullopt);
        const QByteArray at = QByteArray::number(k);
        QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), k);
        QVERIFY2(result.reason == (k < 27 ? FiioReason::Abandoned : FiioReason::None), at);
        QVERIFY2(mirror.valid == (k == 27), at);
        QVERIFY2(rig.fake.preset() != 162 || rig.fake.name(162) == "CASDAP" || rig.marker(), at);
        QVERIFY2(rig.fake.name(162) != "CASDAP"
                     || readBackup(newestBackup(rig.paths.configDir, tinyModel()).value_or(""))->name == "FH3",
                 at);
        if (k == 3)
            QVERIFY(rig.marker() && rig.fake.preset() == fiioBypass);
        if (k == 16)
            QVERIFY(!rig.marker() && rig.fake.name(162) == "CASDAP");
        QVERIFY2(!rig.stamped(), at);

        QCOMPARE(FiioBatch::force(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        QCOMPARE(rig.fake.preset(), fiioBypass);
        QVERIFY2(!rig.marker(), at);
        QVERIFY(!rig.fake.wedged());
        QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        QCOMPARE(rig.fake.name(162), std::string("CASDAP"));
        QVERIFY(mirror.valid);
    }
}

void FiioTest::killMidTransaction()
{
    const FiioProfileFrames frames = testFrames();
    FiioDevice::Bounds dying;
    dying.replyWindowMs = 100;
    dying.readMs = 50;
    FiioDevice::Bounds next;
    next.stampWindowMs = 400;
    next.readMs = 100;
    for (int k : {0, 2, 3, 4, 15, 16, 17, 26}) {
        Rig rig(dying);
        QVERIFY(rig.nodeReady);
        QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
        FiioMirror mirror;
        const auto killAt = [&rig, k] {
            if (rig.fake.count(FakeUsbIo::Op::Out) == k)
                rig.fake.setReplyDelayMs(250);
            return false;
        };
        const QByteArray at = QByteArray::number(k);
        QVERIFY2(FiioBatch::take(rig.device, mirror, rig.paths, frames, killAt, std::nullopt).reason
                     == FiioReason::Timeout,
                 at);
        QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), k + 1);
        QVERIFY2(rig.stamped(), at);
        rig.device.releaseClose();
        rig.fake.setReplyDelayMs(0);
        QVERIFY2(rig.fake.preset() != 162 || rig.fake.name(162) == "CASDAP" || rig.marker(), at);

        QVERIFY2(rig.fake.name(162) != "CASDAP"
                     || readBackup(newestBackup(rig.paths.configDir, tinyModel()).value_or(""))->name == "FH3",
                 at);
        FiioDevice after(rig.fake, tinyModel(), next, rig.paths);
        QCOMPARE(after.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
        QVERIFY2(!rig.stamped(), at);
        QCOMPARE(rig.fake.pending(), 0);
        FiioMirror reset;
        QCOMPARE(FiioBatch::force(after, reset, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        QVERIFY2(!rig.fake.wedged(), at);
        QCOMPARE(rig.fake.preset(), fiioBypass);
        QVERIFY2(!rig.marker(), at);
    }
}

void FiioTest::bypassDeletesMarker()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QVERIFY(rig.touchMarker());
    FiioMirror mirror;
    mirror.valid = true;
    const int before = rig.fake.count(FakeUsbIo::Op::Out);
    const FiioResult result = FiioBatch::bypass(rig.device, mirror, rig.paths, testFrames(), [] { return true; },
                                                std::nullopt);
    QCOMPARE(result.reason, FiioReason::None);
    QCOMPARE(outs(rig.fake, before), QStringList{QStringLiteral("set 16 f0")});
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(mirror.preset, int(fiioBypass));
    QVERIFY(!rig.marker());
    QCOMPARE(rig.logged("bypass acked"), 1);
}

void FiioTest::forceOverPreset0()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(0)).reason, FiioReason::None);
    const std::vector<uint8_t> jazzBand = rig.fake.band(0, 3);
    const std::vector<uint8_t> jazzPreamp = rig.fake.preamp(0);
    QVERIFY(rig.touchMarker());
    FiioMirror mirror;
    const int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::force(rig.device, mirror, rig.paths, testFrames(), {}, std::nullopt).reason, FiioReason::None);
    QCOMPARE(outs(rig.fake, before), QStringList({QStringLiteral("get 16"), QStringLiteral("set 16 f0")}));
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(rig.fake.band(0, 3), jazzBand);
    QCOMPARE(rig.fake.preamp(0), jazzPreamp);
    QCOMPARE(mirror.preset, int(fiioBypass));
    QVERIFY(!rig.marker());
    QCOMPARE(rig.logged("force from preset 0"), 1);
}

void FiioTest::forceAt240()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(rig.touchMarker());
    FiioMirror mirror;
    QCOMPARE(FiioBatch::force(rig.device, mirror, rig.paths, testFrames(), {}, std::nullopt).reason, FiioReason::None);
    QCOMPARE(outs(rig.fake), QStringList{QStringLiteral("get 16")});
    QCOMPARE(mirror.preset, int(fiioBypass));
    QVERIFY(!rig.marker());

    QCOMPARE(rig.set(fiioCmdPreset, presetPayload(162)).reason, FiioReason::None);
    QVERIFY(rig.touchMarker());
    const int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::force(rig.device, mirror, rig.paths, testFrames(), [&rig, before] {
                 return rig.fake.count(FakeUsbIo::Op::Out) > before;
             }, std::nullopt).reason,
             FiioReason::Abandoned);
    QVERIFY(rig.marker());
    QCOMPARE(rig.fake.preset(), uint8_t(162));
}

void FiioTest::dumpReads()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const FiioProfileFrames frames = testFrames();
    FiioMirror taken;
    QCOMPARE(FiioBatch::take(rig.device, taken, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    FiioMirror mirror;
    const int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::dump(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
    QStringList expected{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                         QStringLiteral("get 17")};
    expected += bandReads();
    QCOMPARE(outs(rig.fake, before), expected);
    QCOMPARE(mirror.preset, 162);
    QCOMPARE(mirror.name, std::string("CASDAP"));
    QCOMPARE(mirror.preamp, bytes("0000"));
    QCOMPARE(mirror.firmware, std::string("1.3.0"));
    QCOMPARE(mirror.bands[0], rig.fake.band(162, 0));
    QCOMPARE(mirror.bands[9], frames.bands[9]);
    QVERIFY(!mirror.valid);
}

void FiioTest::stampDrain()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setReplyDelayMs(1500);
    QVERIFY(rig.rawOut(frameGet(70, fiioCmdBand, {1})));
    QVERIFY(rig.stamp().write(fiioNowMs(), rig.sysPath));
    rig.device.releaseClose();
    rig.fake.setReplyDelayMs(0);
    QCOMPARE(rig.fake.pending(), 1);

    const int64_t start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const int64_t elapsed = fiioNowMs() - start;
    QVERIFY2(elapsed >= 2990 && elapsed < 3600, std::to_string(elapsed).c_str());
    QCOMPARE(rig.logged("drained 07bb0b00001508"), 1);
    QVERIFY(!rig.stamped());
    QCOMPARE(rig.fake.pending(), 0);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::None);
    QVERIFY(!rig.fake.wedged());

}

void FiioTest::stampPerDevice()
{
    FiioDevice::Bounds bounds;
    bounds.replyWindowMs = 50;
    bounds.readMs = 50;
    bounds.stampWindowMs = 300;
    Rig rig(bounds);
    QVERIFY(rig.nodeReady);
    const QString otherDir = rig.root.path() + QStringLiteral("/devices/4-1");
    QVERIFY(usbNode(otherDir, "4", "5"));
    QVERIFY(link(otherDir, rig.root.path() + QStringLiteral("/bus/4-1")));
    const std::string otherLink = (rig.root.path() + QStringLiteral("/bus/4-1")).toStdString();
    FakeUsbIo otherFake;
    FiioDevice other(otherFake, tinyModel(), bounds, rig.paths);
    const InflightStamp otherStamp(rig.paths, otherLink);
    otherFake.setOutCheck([&otherStamp] { return QFileInfo::exists(QString::fromStdString(otherStamp.path())); });

    QVERIFY(rig.stamp().path() != otherStamp.path());
    QVERIFY(QString::fromStdString(rig.stamp().path()).endsWith(QStringLiteral("/run/casdap-fiio.inflight.3-1")));
    QVERIFY(QString::fromStdString(otherStamp.path()).endsWith(QStringLiteral("/run/casdap-fiio.inflight.4-1")));

    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(other.openClaimed(otherLink, std::nullopt), FiioReason::None);
    QCOMPARE(otherFake.calls().front().path, std::string("/dev/bus/usb/004/005"));
    rig.fake.setReplyDelayMs(200);
    otherFake.setReplyDelayMs(200);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Timeout);
    QCOMPARE(other.transact(frameGet(1, fiioCmdPreset), std::nullopt).reason, FiioReason::Timeout);
    const std::optional<InflightStamp::Entry> mine = rig.stamp().read();
    const std::optional<InflightStamp::Entry> theirs = otherStamp.read();
    QVERIFY(mine && mine->parsed && theirs && theirs->parsed);
    QCOMPARE(mine->device, QFileInfo(QString::fromStdString(rig.sysPath)).canonicalFilePath().toStdString());
    QCOMPARE(theirs->device, QFileInfo(otherDir).canonicalFilePath().toStdString());

    rig.device.releaseClose();
    rig.fake.setReplyDelayMs(0);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(!rig.stamped());
    QVERIFY(otherStamp.read());
    QCOMPARE(otherStamp.read()->ms, theirs->ms);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::None);
    QVERIFY(otherStamp.read());
    QCOMPARE(otherFake.outCheckFailures(), 0);
}

void FiioTest::noStampDrain()
{
    FiioDevice::Bounds bounds;
    bounds.replyWindowMs = 50;
    bounds.readMs = 50;
    Rig rig(bounds);
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(rig.rawOut(frameGet(71, fiioCmdBand, {2})));
    rig.device.releaseClose();
    QCOMPARE(rig.fake.pending(), 1);
    const int64_t start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(fiioNowMs() - start < 300);
    QCOMPARE(rig.logged("drained "), 1);
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 2);
    QCOMPARE(rig.fake.calls().back().timeoutMs, 40);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::None);
    QVERIFY(!rig.fake.wedged());

    rig.fake.setReplyDelayMs(200);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Timeout);
    QVERIFY(rig.stamped());
    const std::optional<InflightStamp::Entry> entry = rig.stamp().read();
    QVERIFY(entry && entry->parsed);
    QCOMPARE(entry->device, rig.sysPath);
    QVERIFY(fiioNowMs() - entry->ms < 1000);
    rig.stamp().remove();

    rig.device.releaseClose();
    FiioPaths noRun = rig.paths;
    noRun.runDirExists = false;
    FiioDevice bare(rig.fake, tinyModel(), bounds, noRun);
    rig.checkStamp = false;
    QThread::msleep(250);
    QCOMPARE(bare.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(bare.transact(frameGet(1, fiioCmdPreset), std::nullopt).reason, FiioReason::Timeout);
    QVERIFY(!rig.stamped());
}

void FiioTest::stampUnparsable()
{
    FiioDevice::Bounds bounds;
    bounds.stampWindowMs = 400;
    bounds.readMs = 100;
    Rig rig(bounds);
    QVERIFY(rig.nodeReady);
    const QString path = QString::fromStdString(rig.stamp().path());
    for (const QByteArray &junk : {QByteArray("junk\n"), QByteArray(""), QByteArray("123\n"), QByteArray(" /sys/x\n"),
                                   QByteArray("12a4 /sys/x\n"), QByteArray("123 /sys/x trailing\n"),
                                   QByteArray("123 /sys/x\n\n"), QByteArray("123 /sys/x\nmore\n"),
                                   QByteArray("123  /sys/x\n")}) {
        QVERIFY(writeFile(path, junk));
        const std::optional<InflightStamp::Entry> entry = rig.stamp().read();
        QVERIFY2(entry && !entry->parsed, junk.constData());
        QVERIFY(std::abs(fiioNowMs() - entry->ms) < 50);
    }
    const int64_t start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    const int64_t elapsed = fiioNowMs() - start;
    QVERIFY2(elapsed >= 390 && elapsed < 800, std::to_string(elapsed).c_str());
    QVERIFY(!rig.stamped());

    QVERIFY(rig.stamp().write(fiioNowMs() - 10000, "/sys/devices/elsewhere"));
    const int64_t foreign = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(fiioNowMs() - foreign >= 390);
    QVERIFY(!rig.stamped());

    QVERIFY(rig.stamp().write(fiioNowMs() + 60000, rig.sysPath));
    const int64_t future = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(fiioNowMs() - future < 800);
    QVERIFY(!rig.stamp().read());
}

void FiioTest::drainOverflowTerminal()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QVERIFY(rig.rawOut(frameGet(72, fiioCmdBand, {3})));
    QVERIFY(rig.rawOut(frameGet(73, fiioCmdBand, {4})));
    QVERIFY(rig.fake.wedged());
    QVERIFY(rig.stamp().write(fiioNowMs(), rig.sysPath));
    rig.device.releaseClose();

    const int64_t start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Overflow);
    QVERIFY(fiioNowMs() - start < 200);
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 1);
    QCOMPARE(rig.logged("drain overflow"), 1);
    QVERIFY(!rig.fake.isOpen());
    QVERIFY(rig.stamped());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Reset), 0);

    const int reads = rig.fake.count(FakeUsbIo::Op::In);
    QCOMPARE(rig.device.openClaimedNoDrain(rig.sysPath), FiioReason::None);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::In), reads);
    const int outsBefore = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(rig.get(fiioCmdPreset).reason, FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), outsBefore);
    QCOMPARE(rig.device.resetAndClose(), FiioReason::ResetDone);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Reset), 1);
    const std::vector<FakeUsbIo::Call> calls = rig.fake.calls();
    QCOMPARE(calls[calls.size() - 3].op, FakeUsbIo::Op::Release);
    QCOMPARE(calls[calls.size() - 2].op, FakeUsbIo::Op::Reset);
    QCOMPARE(calls[calls.size() - 1].op, FakeUsbIo::Op::Close);
    QVERIFY(!rig.fake.wedged());
    QVERIFY(!rig.stamped());
    QCOMPARE(rig.logged("reset"), 1);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    QCOMPARE(rig.get(fiioCmdPreset).payload, std::vector<uint8_t>{fiioBypass});

    const InflightStamp otherStamp(rig.paths, (rig.root.path() + QStringLiteral("/9-9")).toStdString());
    QVERIFY(otherStamp.write(fiioNowMs(), "/sys/devices/9-9"));
    QCOMPARE(rig.device.openClaimedNoDrain(rig.sysPath), FiioReason::None);
    QCOMPARE(rig.device.resetAndClose(), FiioReason::ResetDone);
    QVERIFY(otherStamp.read());
    QCOMPARE(rig.device.openClaimedNoDrain(rig.sysPath), FiioReason::None);
    rig.fake.setResetError(-ENODEV);
    QCOMPARE(rig.device.resetAndClose(), FiioReason::Device);
    QVERIFY(!rig.fake.isOpen());
    QCOMPARE(rig.device.resetAndClose(), FiioReason::Device);
}

void FiioTest::drainCap()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.strayBeforeReply(std::vector<std::vector<uint8_t>>(20, bytes("07bb0b00931508090014319c00640075ee")));
    QVERIFY(rig.rawOut(frameGet(74, fiioCmdBand, {9})));
    rig.device.releaseClose();
    QCOMPARE(rig.fake.pending(), 21);

    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 4);
    QCOMPARE(rig.logged("drain cap 4"), 1);
    QCOMPARE(rig.fake.pending(), 17);

    QVERIFY(rig.stamp().write(fiioNowMs(), rig.sysPath));
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::Device);
    QCOMPARE(inReadsAfterLastOut(rig.fake.calls()), 4 + 16);
    QCOMPARE(rig.logged("drain cap 16"), 1);
    QCOMPARE(rig.fake.pending(), 1);
    QVERIFY(rig.stamped());
    QVERIFY(!rig.fake.isOpen());
}

void FiioTest::deadlines()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    FiioLock peer(rig.paths);
    QVERIFY(peer.acquire(0, std::nullopt, {}));
    FiioLock lock(rig.paths);
    int64_t start = fiioNowMs();
    QVERIFY(!lock.acquire(1000, start + 100, {}));
    int64_t elapsed = fiioNowMs() - start;
    QVERIFY2(elapsed >= 90 && elapsed < 400, std::to_string(elapsed).c_str());
    QVERIFY(!lock.held());
    peer.release();

    QVERIFY(rig.stamp().write(fiioNowMs(), rig.sysPath));
    start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, start + 200), FiioReason::Deadline);
    elapsed = fiioNowMs() - start;
    QVERIFY2(elapsed >= 190 && elapsed < 500, std::to_string(elapsed).c_str());
    QVERIFY(rig.stamped());
    QVERIFY(!rig.fake.isOpen());
    for (const FakeUsbIo::Call &call : rig.fake.calls()) {
        if (call.op == FakeUsbIo::Op::In)
            QVERIFY(call.timeoutMs <= 200);
    }
    rig.stamp().remove();

    start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, start + 20), FiioReason::Deadline);
    QVERIFY(rig.fake.calls().back().timeoutMs <= 20);
    QVERIFY(!rig.stamped());
    QVERIFY(rig.stamp().write(fiioNowMs() - 3100, rig.sysPath));
    start = fiioNowMs();
    QCOMPARE(rig.device.openClaimed(rig.sysPath, start + 30), FiioReason::Deadline);
    QVERIFY(fiioNowMs() - start < 200);
    QVERIFY(rig.stamped());
    rig.stamp().remove();

    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    rig.fake.setReplyDelayMs(100);
    FiioMirror mirror;
    start = fiioNowMs();
    const FiioResult result = FiioBatch::take(rig.device, mirror, rig.paths, testFrames(), {}, start + 250);
    elapsed = fiioNowMs() - start;
    QCOMPARE(result.reason, FiioReason::Deadline);
    QVERIFY2(elapsed >= 240 && elapsed < 500, std::to_string(elapsed).c_str());
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 3);
    QVERIFY(rig.stamped());
    QVERIFY(!mirror.valid);
    QCOMPARE(FiioBatch::bypass(rig.device, mirror, rig.paths, testFrames(), {}, std::nullopt).reason,
             FiioReason::Refused);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 3);
}

void FiioTest::lockPeer()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const FiioPaths paths = pathsIn(root.path());
    FiioLock a(paths);
    FiioLock b(paths);
    QVERIFY(a.acquire(0, std::nullopt, {}));
    QVERIFY(a.held());
    QVERIFY(a.acquire(0, std::nullopt, {}));
    QVERIFY(QFileInfo::exists(root.path() + QStringLiteral("/run/casdap-fiio.lock")));
    int64_t start = fiioNowMs();
    QVERIFY(!b.acquire(150, std::nullopt, {}));
    QVERIFY(fiioNowMs() - start >= 140);
    start = fiioNowMs();
    QVERIFY(!b.acquire(1000, std::nullopt, [] { return true; }));
    QVERIFY(fiioNowMs() - start < 100);
    std::thread releaser([&a] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        a.release();
    });
    start = fiioNowMs();
    const bool got = b.acquire(1000, std::nullopt, {});
    const int64_t waited = fiioNowMs() - start;
    releaser.join();
    QVERIFY(got);
    QVERIFY2(waited >= 90 && waited < 400, std::to_string(waited).c_str());
    QVERIFY(!a.acquire(0, std::nullopt, {}));
    b.release();
    QVERIFY(a.acquire(0, std::nullopt, {}));

    FiioPaths noRun = paths;
    noRun.runDir = (root.path() + QStringLiteral("/absent")).toStdString();
    noRun.runDirExists = false;
    FiioLock c(noRun);
    FiioLock d(noRun);
    QVERIFY(c.acquire(0, std::nullopt, {}));
    QVERIFY(d.acquire(0, std::nullopt, {}));
    QVERIFY(!QFileInfo::exists(root.path() + QStringLiteral("/absent")));
}

void FiioTest::restoreSlotVerified()
{
    Rig rig;
    QVERIFY(rig.nodeReady);
    QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
    FiioMirror mirror;
    QCOMPARE(FiioBatch::restoreSlot(rig.device, mirror, rig.paths, {}, {}, std::nullopt).reason, FiioReason::Backup);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), 0);

    QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, testFrames(), {}, std::nullopt).reason, FiioReason::None);
    FiioBackup backup = sampleBackup();
    const std::optional<std::string> file = writeBackup(rig.paths.configDir, tinyModel(), backup);
    QVERIFY(file);

    const QString broken = rig.root.path() + QStringLiteral("/broken.txt");
    QFile good(QString::fromStdString(*file));
    QVERIFY(good.open(QIODevice::ReadOnly));
    const QByteArray text = good.readAll();
    QVERIFY(text.contains("band3=03"));
    QVERIFY(writeFile(broken, QByteArray(text).replace("band3=03", "band3=05")));
    int before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::restoreSlot(rig.device, mirror, rig.paths, {}, {}, std::nullopt, broken.toStdString()).reason,
             FiioReason::Backup);
    QCOMPARE(rig.fake.count(FakeUsbIo::Op::Out), before);
    FiioBackup misnumbered = backup;
    misnumbered.bands[3][0] = 5;
    misnumbered.name = "OTHER";
    QVERIFY(!writeBackup(rig.paths.configDir, tinyModel(), misnumbered));

    rig.fake.setIgnoreBandSets(true);
    QCOMPARE(FiioBatch::restoreSlot(rig.device, mirror, rig.paths, {}, {}, std::nullopt).reason, FiioReason::Ack);
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(rig.fake.name(162), std::string("CASDAP"));
    QCOMPARE(rig.logged("restore band 0 reads "), 1);
    QCOMPARE(outs(rig.fake).filter(QStringLiteral("set 30")).size(), 1);
    rig.fake.setIgnoreBandSets(false);

    QVERIFY(rig.touchMarker());
    before = rig.fake.count(FakeUsbIo::Op::Out);
    QCOMPARE(FiioBatch::restoreSlot(rig.device, mirror, rig.paths, {}, {}, std::nullopt).reason, FiioReason::None);
    QStringList expected{QStringLiteral("get 30 a2"), QStringLiteral("set 16 a2"), QStringLiteral("set 17 0050")};
    for (const std::vector<uint8_t> &band : backup.bands)
        expected.append(bandSet(band));
    expected += bandReads();
    expected += QStringLiteral("set 30 a2464833") + QStringLiteral("0000000000");
    expected += QStringLiteral("set 16 f0");
    QCOMPARE(outs(rig.fake, before), expected);
    QCOMPARE(rig.fake.preset(), fiioBypass);
    QCOMPARE(rig.fake.name(162), std::string("FH3"));
    QCOMPARE(rig.fake.preamp(162), bytes("0050"));
    for (int i = 0; i < fiioBandCount; ++i)
        QCOMPARE(rig.fake.band(162, i), backup.bands[i]);
    QVERIFY(!rig.marker());
    QCOMPARE(rig.logged("restored, verified " + *file), 1);
}

void FiioTest::restoreAbandon()
{
    const FiioProfileFrames frames = testFrames();
    for (int k = 0; k <= 24; ++k) {
        Rig rig;
        QVERIFY(rig.nodeReady);
        QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
        FiioMirror mirror;
        QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        const std::optional<std::string> good = newestBackup(rig.paths.configDir, tinyModel());
        QVERIFY(good);
        const int start = rig.fake.count(FakeUsbIo::Op::Out);
        const auto abandoned = [&rig, start, k] { return rig.fake.count(FakeUsbIo::Op::Out) - start >= k; };
        const FiioResult result = FiioBatch::restoreSlot(rig.device, mirror, rig.paths, frames, abandoned,
                                                         std::nullopt);
        const QByteArray at = QByteArray::number(k);
        QVERIFY2(result.reason == (k < 24 ? FiioReason::Abandoned : FiioReason::None), at);
        QVERIFY2(rig.fake.name(162) == (k < 24 ? "CASDAP" : "FH3"), at);

        QCOMPARE(FiioBatch::take(rig.device, mirror, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        QCOMPARE(newestBackup(rig.paths.configDir, tinyModel()), good);
        QVERIFY2(!QFileInfo::exists(QString::fromStdString(rig.paths.configDir)
                                    + QStringLiteral("/fiio-2972-0149-slot162-2.txt")),
                 at);
        QCOMPARE(readBackup(*good)->name, std::string("FH3"));
    }
}

void FiioTest::restoreForeignAbandon()
{
    const FiioProfileFrames frames = testFrames();
    const std::vector<uint8_t> userBand = bytes("04ffc407d0008d01");
    for (int k = 0; k <= 37; ++k) {
        Rig rig;
        QVERIFY(rig.nodeReady);
        QCOMPARE(rig.device.openClaimed(rig.sysPath, std::nullopt), FiioReason::None);
        FiioMirror taken;
        QCOMPARE(FiioBatch::take(rig.device, taken, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        const std::optional<std::string> good = newestBackup(rig.paths.configDir, tinyModel());
        QVERIFY(good);
        const std::optional<FiioBackup> goodBackup = readBackup(*good);
        QCOMPARE(rig.set(fiioCmdName, namePayload(162, "USER")).reason, FiioReason::None);
        QCOMPARE(rig.set(fiioCmdBand, userBand).reason, FiioReason::None);
        QCOMPARE(rig.set(fiioCmdPreset, presetPayload(fiioBypass)).reason, FiioReason::None);
        std::array<std::vector<uint8_t>, fiioBandCount> userBands;
        for (int i = 0; i < fiioBandCount; ++i)
            userBands[i] = rig.fake.band(162, i);
        const std::vector<uint8_t> userPreamp = rig.fake.preamp(162);

        FiioMirror mirror;
        const int start = rig.fake.count(FakeUsbIo::Op::Out);
        const auto abandoned = [&rig, start, k] { return rig.fake.count(FakeUsbIo::Op::Out) - start >= k; };
        const FiioResult result = FiioBatch::restoreSlot(rig.device, mirror, rig.paths, frames, abandoned,
                                                         std::nullopt);
        const QByteArray at = QByteArray::number(k);
        QVERIFY2(result.reason == (k < 37 ? FiioReason::Abandoned : FiioReason::None), at);
        const std::string name = rig.fake.name(162);
        QVERIFY2(name == "CASDAP" || name == "USER" || (k == 37 && name == "FH3"), at);
        if (name == "USER") {
            QVERIFY2(k <= 15, at);
            for (int i = 0; i < fiioBandCount; ++i)
                QVERIFY2(rig.fake.band(162, i) == userBands[i], at);
        }
        if (k > 15 && k < 37)
            QVERIFY2(name == "CASDAP", at);
        QVERIFY2(rig.fake.preset() != 162 || name == "CASDAP" || rig.marker() || k == 37, at);
        if (k == 37) {
            QCOMPARE(rig.fake.preset(), fiioBypass);
            QCOMPARE(name, std::string("FH3"));
        }

        QCOMPARE(FiioBatch::take(rig.device, taken, rig.paths, frames, {}, std::nullopt).reason, FiioReason::None);
        bool userSaved = false;
        for (int n = 1; n <= 4; ++n) {
            const std::string file = rig.paths.configDir + "/" + backupName(tinyModel(), n);
            const std::optional<FiioBackup> saved = readBackup(file);
            if (!saved)
                continue;
            QVERIFY2(saved->name == "FH3" || saved->name == "USER", (at + " " + QByteArray::number(n)).constData());
            if (saved->name == "FH3") {
                QVERIFY2(saved->bands == goodBackup->bands && saved->preamp == goodBackup->preamp, at);
            } else {
                QVERIFY2(saved->bands == userBands && saved->preamp == userPreamp, at);
                userSaved = true;
            }
        }
        QVERIFY2(userSaved, at);
    }
}

void FiioTest::driverWaitNeedsInterface()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString dev = root.path() + QStringLiteral("/devices/3-1");
    QVERIFY(QDir().mkpath(dev));
    const std::string path = dev.toStdString();
    int64_t start = fiioNowMs();
    QCOMPARE(waitDriverFree(path, 100, std::nullopt, {}), FiioReason::HidBound);
    QVERIFY(fiioNowMs() - start >= 90);
    QCOMPARE(waitDriverFree(path, 0, std::nullopt, {}), FiioReason::HidBound);

    const QString iface = dev + QStringLiteral("/3-1:1.0");
    QVERIFY(QDir().mkpath(iface));
    QVERIFY(QDir().mkpath(root.path() + QStringLiteral("/drivers/usbhid")));
    QVERIFY(link(root.path() + QStringLiteral("/drivers/usbhid"), iface + QStringLiteral("/driver")));
    QCOMPARE(waitDriverFree(path, 60, std::nullopt, {}), FiioReason::HidBound);
    start = fiioNowMs();
    QCOMPARE(waitDriverFree(path, 1000, start + 80, {}), FiioReason::Deadline);
    QVERIFY(fiioNowMs() - start < 300);
    QCOMPARE(waitDriverFree(path, 1000, std::nullopt, [] { return true; }), FiioReason::Abandoned);

    std::thread unbind([&iface] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        QFile::remove(iface + QStringLiteral("/driver"));
    });
    start = fiioNowMs();
    const FiioReason freed = waitDriverFree(path, 2000, std::nullopt, {});
    const int64_t waited = fiioNowMs() - start;
    unbind.join();
    QCOMPARE(freed, FiioReason::None);
    QVERIFY2(waited >= 90 && waited < 500, std::to_string(waited).c_str());
    QCOMPARE(waitDriverFree(path, 0, std::nullopt, {}), FiioReason::None);
    QVERIFY(link(root.path() + QStringLiteral("/absent-driver"), iface + QStringLiteral("/driver")));
    QCOMPARE(waitDriverFree(path, 0, std::nullopt, {}), FiioReason::HidBound);
}

void FiioTest::workerLatestWins()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    const std::shared_ptr<FakeUsbIo::Hold> hold = rig.fake->hold(FakeUsbIo::Op::Open);
    const quint64 a = rig.post(FiioWorker::Kind::Take, testFrames());
    QVERIFY(hold->waitEntered(1000));
    const quint64 b = rig.post(FiioWorker::Kind::Take, testFrames(1.0));
    const FiioProfileFrames last = testFrames(2.0);
    const quint64 c = rig.post(FiioWorker::Kind::Take, last);
    hold->release();
    QVERIFY(rig.waitDone(c, 5000));

    QCOMPARE(rig.done.size(), std::size_t(3));
    QCOMPARE(rig.done[0].seq, a);
    QCOMPARE(rig.done[0].reason, FiioReason::Abandoned);
    QVERIFY(!rig.done[0].ok);
    QCOMPARE(rig.done[1].seq, b);
    QCOMPARE(rig.done[1].reason, FiioReason::Abandoned);
    QCOMPARE(rig.done[2].seq, c);
    QCOMPARE(rig.done[2].reason, FiioReason::None);
    QVERIFY(rig.done[2].ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 2);

    const QStringList sent = outs(*rig.fake);
    QCOMPARE(sent.size(), 27);
    QCOMPARE(sent.first(), QStringLiteral("get 0b"));
    QCOMPARE(sent.mid(17), bandSets(last));
    for (int i = 1; i < fiioBandCount; ++i)
        QCOMPARE(rig.fake->band(162, i), last.bands[i]);
    const FiioMirror &mirror = rig.done[2].mirror;
    QVERIFY(mirror.valid);
    QCOMPARE(mirror.cardId, std::string("B"));
    QCOMPARE(mirror.generation, uint64_t(1));
    QCOMPARE(mirror.preset, 162);
    QVERIFY(mirror.bands == last.bands);
    QVERIFY(!rig.fake->wedged());
}

void FiioTest::workerAbandon()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    for (int i = 0; i < 3; ++i)
        rig.fake->hold(FakeUsbIo::Op::Out)->release();
    const std::shared_ptr<FakeUsbIo::Hold> fourth = rig.fake->hold(FakeUsbIo::Op::Out);
    const quint64 a = rig.post(FiioWorker::Kind::Take);
    QVERIFY(fourth->waitEntered(3000));
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Out), 3);
    const quint64 b = rig.post(FiioWorker::Kind::Bypass);
    fourth->release();
    QVERIFY(rig.waitDone(b, 3000));

    QCOMPARE(rig.done.size(), std::size_t(2));
    QCOMPARE(rig.result(a)->reason, FiioReason::Abandoned);
    QVERIFY(!rig.result(a)->mirror.valid);
    QVERIFY(rig.result(b)->ok);
    QCOMPARE(rig.result(b)->mirror.preset, int(fiioBypass));
    const QStringList sent = outs(*rig.fake);
    QCOMPARE(sent, (QStringList{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                                QStringLiteral("set 16 a2"), QStringLiteral("set 16 f0")}));
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QVERIFY(!rig.fake->wedged());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 2);
}

void FiioTest::workerAbandonInWaits()
{
    WorkerRig rig(workerBounds(2000, 1000, 500));
    QVERIFY(rig.ready);

    QVERIFY(rig.bindDriver());
    const quint64 a = rig.post(FiioWorker::Kind::Force);
    QTest::qWait(100);
    int64_t start = fiioNowMs();
    const quint64 b = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(a, 1000));
    QVERIFY2(fiioNowMs() - start < 300, std::to_string(fiioNowMs() - start).c_str());
    QCOMPARE(rig.result(a)->reason, FiioReason::Abandoned);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    QVERIFY(rig.unbindDriver());
    QVERIFY(rig.waitDone(b, 3000));
    QVERIFY(rig.result(b)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);

    FiioLock peer(rig.paths);
    QVERIFY(peer.acquire(0, std::nullopt, {}));
    const quint64 c = rig.post(FiioWorker::Kind::Force);
    QTest::qWait(100);
    start = fiioNowMs();
    const quint64 d = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(c, 1000));
    QVERIFY2(fiioNowMs() - start < 300, std::to_string(fiioNowMs() - start).c_str());
    QCOMPARE(rig.result(c)->reason, FiioReason::Abandoned);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);
    peer.release();
    QVERIFY(rig.waitDone(d, 3000));
    QVERIFY(rig.result(d)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 2);

    const int claims = rig.fake->count(FakeUsbIo::Op::Claim);
    rig.fake->setClaimError(-EBUSY);
    const quint64 e = rig.post(FiioWorker::Kind::Force);
    QVERIFY(QTest::qWaitFor([&rig, claims] { return rig.fake->count(FakeUsbIo::Op::Claim) == claims + 1; }, 2000));
    rig.fake->setClaimError(0);
    QTest::qWait(50);
    start = fiioNowMs();
    const quint64 f = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(e, 1000));
    QVERIFY2(fiioNowMs() - start < 300, std::to_string(fiioNowMs() - start).c_str());
    QCOMPARE(rig.result(e)->reason, FiioReason::Abandoned);
    QVERIFY(rig.waitDone(f, 3000));
    QVERIFY(rig.result(f)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Claim), claims + 2);
}

void FiioTest::workerDriverWait()
{
    WorkerRig rig(workerBounds(300));
    QVERIFY(rig.ready);
    QVERIFY(rig.bindDriver());
    int64_t start = fiioNowMs();
    const quint64 a = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(a, 2000));
    int64_t waited = fiioNowMs() - start;
    QCOMPARE(rig.result(a)->reason, FiioReason::HidBound);
    QVERIFY(!rig.result(a)->ok);
    QVERIFY2(waited >= 280 && waited < 800, std::to_string(waited).c_str());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);

    QTimer::singleShot(100, [&rig] { rig.unbindDriver(); });
    start = fiioNowMs();
    const quint64 b = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(b, 2000));
    waited = fiioNowMs() - start;
    QVERIFY(rig.result(b)->ok);
    QVERIFY2(waited >= 90 && waited < 300, std::to_string(waited).c_str());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);
}

void FiioTest::workerLockRetry()
{
    WorkerRig rig(workerBounds(2000, 200, 150));
    QVERIFY(rig.ready);
    FiioLock peer(rig.paths);
    QVERIFY(peer.acquire(0, std::nullopt, {}));
    QTimer::singleShot(300, [&peer] { peer.release(); });
    int64_t start = fiioNowMs();
    const quint64 a = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(a, 3000));
    int64_t waited = fiioNowMs() - start;
    QVERIFY(rig.result(a)->ok);
    QVERIFY2(waited >= 340 && waited < 800, std::to_string(waited).c_str());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);

    QVERIFY(peer.acquire(0, std::nullopt, {}));
    start = fiioNowMs();
    const quint64 b = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(b, 3000));
    waited = fiioNowMs() - start;
    QCOMPARE(rig.result(b)->reason, FiioReason::Lock);
    QVERIFY(!rig.result(b)->ok);
    QVERIFY2(waited >= 540 && waited < 1000, std::to_string(waited).c_str());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 1);
}

void FiioTest::workerClaimRetry()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    rig.fake->setClaimError(-EBUSY);
    QTimer::singleShot(300, [&rig] { rig.fake->setClaimError(0); });
    int64_t start = fiioNowMs();
    const quint64 a = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(a, 3000));
    const int64_t waited = fiioNowMs() - start;
    QVERIFY(rig.result(a)->ok);
    QVERIFY2(waited >= 490 && waited < 1000, std::to_string(waited).c_str());
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Claim), 2);
    QCOMPARE(rig.fake->preset(), fiioBypass);

    rig.fake->setClaimError(-EBUSY);
    start = fiioNowMs();
    const quint64 b = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(b, 3000));
    QCOMPARE(rig.result(b)->reason, FiioReason::Claim);
    QVERIFY(fiioNowMs() - start >= 490);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Claim), 4);
}

void FiioTest::workerResetPcmOpen()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.pcmStatus("state: RUNNING\nowner_pid   : 4242\n"));
    const quint64 a = rig.post(FiioWorker::Kind::Reset);
    QVERIFY(rig.waitDone(a, 3000));
    QCOMPARE(rig.result(a)->reason, FiioReason::Overflow);
    QVERIFY(!rig.result(a)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::In), 0);
    QVERIFY(!rig.fake->isOpen());

    QFile::remove(rig.root.path() + QStringLiteral("/asound/card1/pcm0p/sub0/status"));
    const quint64 b = rig.post(FiioWorker::Kind::Reset);
    QVERIFY(rig.waitDone(b, 3000));
    QCOMPARE(rig.result(b)->reason, FiioReason::Overflow);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
}

void FiioTest::workerReset()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    QCOMPARE(rig.fake->open("/dev/bus/usb/003/002"), 0);
    QCOMPARE(rig.fake->claim(0), 0);
    std::vector<uint8_t> out = frameGet(1, fiioCmdBand, {1});
    QCOMPARE(rig.fake->bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QCOMPARE(rig.fake->bulk(0x05, out.data(), int(out.size()), 200), int(out.size()));
    QCOMPARE(rig.fake->release(0), 0);
    rig.fake->close();
    QVERIFY(rig.fake->wedged());
    const std::string canonical = QFileInfo(rig.devPath).canonicalFilePath().toStdString();
    const InflightStamp stamp(rig.paths, canonical);
    QVERIFY(stamp.write(fiioNowMs(), canonical));
    QVERIFY(rig.pcmStatus("closed\n"));
    const std::size_t before = rig.fake->calls().size();

    Capture capture;
    const quint64 a = rig.post(FiioWorker::Kind::Reset);
    QVERIFY(rig.waitDone(a, 3000));
    QCOMPARE(rig.result(a)->reason, FiioReason::ResetDone);
    QVERIFY(rig.result(a)->ok);
    QVERIFY(!rig.result(a)->mirror.valid);
    QCOMPARE(capture.count(QStringLiteral("casdap dac eq: driver unbound after reset")), 1);
    QCOMPARE(capture.count(QStringLiteral("casdap dac eq: reset")), 1);

    const std::vector<FakeUsbIo::Call> calls = rig.fake->calls();
    QList<int> ops;
    for (std::size_t i = before; i < calls.size(); ++i)
        ops.append(int(calls[i].op));
    const QList<int> expected{int(FakeUsbIo::Op::Open),    int(FakeUsbIo::Op::Descriptor), int(FakeUsbIo::Op::Claim),
                              int(FakeUsbIo::Op::Release), int(FakeUsbIo::Op::Reset),      int(FakeUsbIo::Op::Close)};
    QCOMPARE(ops, expected);
    QVERIFY(!rig.fake->wedged());
    QVERIFY(!QFileInfo::exists(QString::fromStdString(stamp.path())));
    QVERIFY(!rig.fake->isOpen());

    const quint64 b = rig.post(FiioWorker::Kind::Force);
    QVERIFY(rig.waitDone(b, 3000));
    QVERIFY(rig.result(b)->ok);
    QCOMPARE(rig.fake->preset(), fiioBypass);
}

void FiioTest::workerResetFails()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.pcmStatus("closed\n"));
    rig.fake->setResetError(-EIO);
    Capture capture;
    const quint64 a = rig.post(FiioWorker::Kind::Reset);
    QVERIFY(rig.waitDone(a, 3000));
    QCOMPARE(rig.result(a)->reason, FiioReason::Device);
    QVERIFY(!rig.result(a)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 1);
    QCOMPARE(capture.count(QStringLiteral("casdap dac eq: driver unbound after reset")), 0);
    QCOMPARE(capture.count(QStringLiteral("casdap dac eq: driver bound after reset")), 0);
    QVERIFY(!rig.fake->isOpen());
}

void FiioTest::quitSendsBypass()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    rig.fake->setReplyDelayMs(30);
    const quint64 a = rig.post(FiioWorker::Kind::Take);
    QVERIFY(QTest::qWaitFor([&rig] { return rig.fake->count(FakeUsbIo::Op::Out) >= 2; }, 3000));
    const FiioWorker::Target final = rig.target(FiioWorker::Kind::Bypass);
    const quint64 b = final.seq;
    const int64_t start = fiioNowMs();
    rig.worker->postFinal(final, 500);
    QVERIFY(rig.worker->quitAndJoin(1000));
    QVERIFY2(fiioNowMs() - start < 500, std::to_string(fiioNowMs() - start).c_str());
    const QStringList sent = outs(*rig.fake);
    QCOMPARE(sent.last(), QStringLiteral("set 16 f0"));
    QCOMPARE(rig.fake->preset(), fiioBypass);
    QVERIFY(!rig.fake->wedged());

    QVERIFY(rig.waitDone(b, 1000));
    QCOMPARE(rig.result(a)->reason, FiioReason::Abandoned);
    QVERIFY(rig.result(b)->ok);
    const int sentCount = rig.fake->count(FakeUsbIo::Op::Out);
    rig.post(FiioWorker::Kind::Take);
    QTest::qWait(100);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Out), sentCount);
    QCOMPARE(rig.done.size(), std::size_t(2));
}

void FiioTest::workerQuitJoin()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    const std::shared_ptr<FakeUsbIo::Hold> hold = rig.fake->hold(FakeUsbIo::Op::Open);
    const quint64 a = rig.post(FiioWorker::Kind::Take);
    QVERIFY(hold->waitEntered(1000));
    const int64_t start = fiioNowMs();
    QVERIFY(!rig.worker->quitAndJoin(500));
    const int64_t waited = fiioNowMs() - start;
    QVERIFY2(waited >= 490 && waited < 700, std::to_string(waited).c_str());
    hold->release();
    QVERIFY(rig.worker->quitAndJoin(3000));
    QVERIFY(rig.waitDone(a, 1000));
    QCOMPARE(rig.result(a)->reason, FiioReason::Abandoned);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Out), 0);
    QVERIFY(!rig.fake->isOpen());
}

void FiioTest::workerGuiNeverBlocks()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    const std::shared_ptr<FakeUsbIo::Hold> hold = rig.fake->hold(FakeUsbIo::Op::Out);
    const quint64 a = rig.post(FiioWorker::Kind::Take);
    QVERIFY(hold->waitEntered(2000));
    int64_t start = fiioNowMs();
    const quint64 b0 = rig.post(FiioWorker::Kind::Bypass);
    QVERIFY(fiioNowMs() - start < 20);
    bool fired = false;
    QTimer::singleShot(20, [&fired] { fired = true; });
    QVERIFY(QTest::qWaitFor([&fired] { return fired; }, 500));
    QVERIFY(rig.done.empty());
    const FiioWorker::Target final = rig.target(FiioWorker::Kind::Bypass);
    const quint64 b = final.seq;
    start = fiioNowMs();
    rig.worker->postFinal(final, 3000);
    QVERIFY(fiioNowMs() - start < 20);
    hold->release();
    QVERIFY(rig.worker->quitAndJoin(3000));
    QVERIFY(rig.waitDone(b, 1000));
    QCOMPARE(rig.result(a)->reason, FiioReason::Abandoned);
    QCOMPARE(rig.result(b0)->reason, FiioReason::Abandoned);
    QVERIFY(rig.result(b)->ok);
    QCOMPARE(outs(*rig.fake), (QStringList{QStringLiteral("get 0b"), QStringLiteral("set 16 f0")}));
}

void FiioTest::workerResetExHeld()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    QVERIFY(rig.bindDriver());
    QVERIFY(rig.pcmStatus("closed\n"));
    FiioWorker::Target target = rig.target(FiioWorker::Kind::Reset);
    target.exHeld = true;
    const int64_t start = fiioNowMs();
    rig.worker->post(target);
    QVERIFY(rig.waitDone(target.seq, 3000));
    QVERIFY2(fiioNowMs() - start < 200, std::to_string(fiioNowMs() - start).c_str());
    QCOMPARE(rig.result(target.seq)->reason, FiioReason::Refused);
    QVERIFY(!rig.result(target.seq)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Open), 0);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Reset), 0);
}

void FiioTest::workerMirrorPerGeneration()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    const FiioProfileFrames frames = testFrames();
    const quint64 a = rig.post(FiioWorker::Kind::Take, frames);
    QVERIFY(rig.waitDone(a, 3000));
    QVERIFY(rig.result(a)->ok);
    QCOMPARE(rig.result(a)->mirror.generation, uint64_t(1));
    const int first = int(outs(*rig.fake).size());

    FiioWorker::Target next = rig.target(FiioWorker::Kind::Take, frames);
    next.generation = 2;
    rig.worker->post(next);
    QVERIFY(rig.waitDone(next.seq, 3000));
    QVERIFY(rig.result(next.seq)->ok);
    const FiioMirror &mirror = rig.result(next.seq)->mirror;
    QCOMPARE(mirror.generation, uint64_t(2));
    QCOMPARE(mirror.cardId, std::string("B"));
    QVERIFY(mirror.valid);
    QVERIFY(mirror.bands == frames.bands);
    QStringList expected{QStringLiteral("get 0b"), QStringLiteral("get 16"), QStringLiteral("get 30 a2"),
                         QStringLiteral("set 17 0000")};
    expected += bandSets(frames);
    QCOMPARE(outs(*rig.fake, first), expected);
}

void FiioTest::workerQuitDeadline()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    rig.fake->setReplyDelayMs(1000);
    const FiioWorker::Target final = rig.target(FiioWorker::Kind::Bypass);
    const int64_t start = fiioNowMs();
    rig.worker->postFinal(final, 150);
    QVERIFY(rig.worker->quitAndJoin(1000));
    const int64_t waited = fiioNowMs() - start;
    QVERIFY2(waited >= 140 && waited < 400, std::to_string(waited).c_str());
    QVERIFY(rig.waitDone(final.seq, 1000));
    QCOMPARE(rig.result(final.seq)->reason, FiioReason::Deadline);
    QVERIFY(!rig.result(final.seq)->ok);
    QCOMPARE(rig.fake->count(FakeUsbIo::Op::Out), 1);
    QVERIFY(!rig.fake->isOpen());
}

void FiioTest::workerUpdate()
{
    WorkerRig rig;
    QVERIFY(rig.ready);
    const FiioProfileFrames frames = testFrames();
    const quint64 a = rig.post(FiioWorker::Kind::Take, frames);
    QVERIFY(rig.waitDone(a, 3000));
    QVERIFY(rig.result(a)->ok);
    const int first = int(outs(*rig.fake).size());

    FiioProfileFrames changed = frames;
    FiioBand band;
    band.index = 3;
    band.gain = 4.5;
    band.freq = 250.0;
    band.q = 1.0;
    changed.bands[3] = bandPayload(tinyModel(), band);
    const quint64 b = rig.post(FiioWorker::Kind::Update, changed);
    QVERIFY(rig.waitDone(b, 3000));
    QVERIFY(rig.result(b)->ok);
    QCOMPARE(outs(*rig.fake, first), (QStringList{QStringLiteral("get 16"), bandSet(changed.bands[3])}));
    QCOMPARE(rig.fake->band(162, 3), changed.bands[3]);
    QVERIFY(rig.result(b)->mirror.bands == changed.bands);

    FiioWorker::Target next = rig.target(FiioWorker::Kind::Update, changed);
    next.generation = 2;
    const int second = int(outs(*rig.fake).size());
    rig.worker->post(next);
    QVERIFY(rig.waitDone(next.seq, 3000));
    QVERIFY(rig.result(next.seq)->ok);
    QStringList expected{QStringLiteral("get 16"), QStringLiteral("get 0b"), QStringLiteral("get 16"),
                         QStringLiteral("get 30 a2"), QStringLiteral("set 17 0000")};
    expected += bandSets(changed);
    QCOMPARE(outs(*rig.fake, second), expected);
}

QTEST_GUILESS_MAIN(FiioTest)
#include "tst_fiio.moc"
