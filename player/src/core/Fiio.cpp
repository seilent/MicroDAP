#include "Fiio.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <linux/usbdevice_fs.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

namespace casdap {

namespace {

namespace fs = std::filesystem;

static_assert(USBDEVFS_CLAIMINTERFACE == 0x8004550F);
static_assert(USBDEVFS_RELEASEINTERFACE == 0x80045510);
static_assert(USBDEVFS_BULK == 0xC0185502);
static_assert(USBDEVFS_RESET == 0x5514);

constexpr uint8_t setHead1 = 0xAA;
constexpr uint8_t setHead2 = 0x0A;
constexpr uint8_t getHead1 = 0xBB;
constexpr uint8_t getHead2 = 0x0B;
constexpr uint8_t frameEnd = 0xEE;
constexpr std::size_t headerSize = 7;
constexpr int gainFreeFirst = 3;
constexpr int typeLast = 6;
constexpr std::size_t maxPayload = 255;
constexpr int qSlack = 5;
constexpr std::size_t descriptorSize = 18;

std::vector<uint8_t> frame(uint8_t reportId, uint8_t head1, uint8_t head2, uint16_t seq, uint8_t cmd,
                           const std::vector<uint8_t> &payload)
{
    if (payload.size() > maxPayload)
        return {};
    std::vector<uint8_t> out{reportId, head1, head2, uint8_t(seq >> 8), uint8_t(seq & 0xFF), cmd,
                             uint8_t(payload.size())};
    out.insert(out.end(), payload.begin(), payload.end());
    out.push_back(crc8Maxim(out.data(), out.size()));
    out.push_back(frameEnd);
    return out;
}

void put16(std::vector<uint8_t> &out, int value)
{
    const auto v = static_cast<uint16_t>(static_cast<int16_t>(value));
    out.push_back(uint8_t(v >> 8));
    out.push_back(uint8_t(v & 0xFF));
}

int signed16(uint8_t hi, uint8_t lo)
{
    return static_cast<int16_t>(static_cast<uint16_t>((hi << 8) | lo));
}

int unsigned16(uint8_t hi, uint8_t lo)
{
    return (hi << 8) | lo;
}

std::string readText(const fs::path &path)
{
    std::ifstream in(path);
    std::string text;
    std::getline(in, text);
    return text;
}

std::optional<uint16_t> readHex(const fs::path &path)
{
    const std::string text = readText(path);
    if (text.empty())
        return std::nullopt;
    char *end = nullptr;
    const unsigned long value = std::strtoul(text.c_str(), &end, 16);
    if (end == text.c_str() || value > 0xFFFF)
        return std::nullopt;
    return static_cast<uint16_t>(value);
}

std::vector<fs::path> entries(const fs::path &dir)
{
    std::vector<fs::path> out;
    std::error_code ec;
    fs::directory_iterator it(dir, ec);
    const fs::directory_iterator end;
    while (!ec && it != end) {
        out.push_back(it->path());
        it.increment(ec);
    }
    return out;
}

bool printable(const std::string &name)
{
    return std::all_of(name.begin(), name.end(), [](char c) { return uint8_t(c) >= 0x20 && uint8_t(c) <= 0x7E; });
}

bool sameBand(const std::vector<uint8_t> &sent, const std::vector<uint8_t> &echo)
{
    if (sent.size() != std::size_t(fiioBandPayloadSize) || echo.size() != sent.size())
        return false;
    return std::equal(sent.begin(), sent.begin() + 5, echo.begin()) && sent[7] == echo[7]
           && std::abs(signed16(sent[5], sent[6]) - signed16(echo[5], echo[6])) <= qSlack;
}

bool sameName(const std::vector<uint8_t> &sent, const std::vector<uint8_t> &echo)
{
    const std::size_t size = 1 + fiioNameLength;
    return sent.size() >= size && echo.size() >= size && std::equal(sent.begin(), sent.begin() + size, echo.begin());
}

bool syncDir(const std::string &dir)
{
    const int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0)
        return false;
    const bool ok = ::fsync(fd) == 0;
    return ::close(fd) == 0 && ok;
}

bool ensureDir(const fs::path &dir)
{
    std::error_code ec;
    if (dir.empty() || fs::is_directory(dir, ec))
        return true;
    const fs::path parent = dir.parent_path();
    if (parent != dir && !ensureDir(parent))
        return false;
    fs::create_directory(dir, ec);
    if (!fs::is_directory(dir, ec))
        return false;
    return syncDir(parent.empty() ? std::string(".") : parent.string());
}

std::optional<FiioMatch> matchDevice(const fs::path &dev)
{
    const std::optional<uint16_t> vid = readHex(dev / "idVendor");
    const std::optional<uint16_t> pid = readHex(dev / "idProduct");
    if (!vid || !pid)
        return std::nullopt;
    const FiioModel *model = fiioModel(*vid, *pid);
    if (!model)
        return std::nullopt;
    return FiioMatch{model, dev.string()};
}

std::string hex(const std::vector<uint8_t> &bytes)
{
    std::string out;
    char buf[3];
    for (uint8_t b : bytes) {
        std::snprintf(buf, sizeof buf, "%02x", b);
        out += buf;
    }
    return out;
}

std::optional<std::vector<uint8_t>> unhex(const std::string &text, std::size_t bytes)
{
    if (text.size() != bytes * 2)
        return std::nullopt;
    std::vector<uint8_t> out;
    for (std::size_t i = 0; i < text.size(); i += 2) {
        unsigned value = 0;
        if (std::sscanf(text.c_str() + i, "%2x", &value) != 1 || !std::isxdigit(uint8_t(text[i]))
            || !std::isxdigit(uint8_t(text[i + 1])))
            return std::nullopt;
        out.push_back(uint8_t(value));
    }
    return out;
}

std::string modelKey(const FiioModel &model)
{
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04x:%04x", model.vid, model.pid);
    return buf;
}

std::string backupPrefix(const FiioModel &model)
{
    char buf[48];
    std::snprintf(buf, sizeof buf, "fiio-%04x-%04x-slot%d-", model.vid, model.pid, model.slot);
    return buf;
}

std::string backupText(const FiioBackup &backup)
{
    std::string out = "model=" + backup.model + "\nfirmware=" + backup.firmware + "\ndate=" + backup.date
                      + "\nname=" + backup.name + "\npreamp=" + hex(backup.preamp) + "\n";
    for (int i = 0; i < fiioBandCount; ++i)
        out += "band" + std::to_string(i) + "=" + hex(backup.bands[i]) + "\n";
    return out;
}

bool sameSlot(const FiioBackup &a, const FiioBackup &b)
{
    return a.model == b.model && a.firmware == b.firmware && a.name == b.name && a.preamp == b.preamp
           && a.bands == b.bands;
}

int highestBackup(const std::string &dir, const FiioModel &model)
{
    const std::string prefix = backupPrefix(model);
    int highest = 0;
    for (const fs::path &path : entries(dir)) {
        const std::string name = path.filename().string();
        if (name.size() <= prefix.size() + 4 || name.compare(0, prefix.size(), prefix) != 0
            || name.compare(name.size() - 4, 4, ".txt") != 0)
            continue;
        const std::string digits = name.substr(prefix.size(), name.size() - prefix.size() - 4);
        if (!std::all_of(digits.begin(), digits.end(), [](char c) { return std::isdigit(uint8_t(c)); })
            || digits.size() > 6 || digits.front() == '0')
            continue;
        highest = std::max(highest, std::stoi(digits));
    }
    return highest;
}

bool writeFully(int fd, const std::string &text)
{
    std::size_t done = 0;
    while (done < text.size()) {
        const ssize_t n = ::write(fd, text.data() + done, text.size() - done);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return false;
        done += std::size_t(n);
    }
    return true;
}

std::optional<int> readDecimal(const fs::path &path)
{
    const std::string text = readText(path);
    if (text.empty() || !std::all_of(text.begin(), text.end(), [](char c) { return std::isdigit(uint8_t(c)); })
        || text.size() > 3)
        return std::nullopt;
    return std::stoi(text);
}

std::optional<std::string> devNode(const std::string &sysPath)
{
    const std::optional<int> bus = readDecimal(fs::path(sysPath) / "busnum");
    const std::optional<int> dev = readDecimal(fs::path(sysPath) / "devnum");
    if (!bus || !dev)
        return std::nullopt;
    char buf[32];
    std::snprintf(buf, sizeof buf, "/dev/bus/usb/%03d/%03d", *bus, *dev);
    return std::string(buf);
}

FiioReason openReason(int error)
{
    return error == -EACCES || error == -EPERM ? FiioReason::Permission : FiioReason::Device;
}

int errnoResult(int result)
{
    return result < 0 ? -errno : result;
}

}

uint8_t crc8Maxim(const uint8_t *data, std::size_t size)
{
    uint8_t crc = 0;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 1) ? uint8_t((crc >> 1) ^ 0x8C) : uint8_t(crc >> 1);
    }
    return crc;
}

std::vector<uint8_t> frameSet(uint16_t seq, uint8_t cmd, const std::vector<uint8_t> &payload, uint8_t reportId)
{
    return frame(reportId, setHead1, setHead2, seq, cmd, payload);
}

std::vector<uint8_t> frameGet(uint16_t seq, uint8_t cmd, const std::vector<uint8_t> &payload, uint8_t reportId)
{
    return frame(reportId, getHead1, getHead2, seq, cmd, payload);
}

std::optional<FiioReply> parseReply(const uint8_t *data, std::size_t size, uint8_t reportId)
{
    if (!data || size < headerSize || data[0] != reportId)
        return std::nullopt;
    const bool set = data[1] == setHead1 && data[2] == setHead2;
    const bool get = data[1] == getHead1 && data[2] == getHead2;
    if (!set && !get)
        return std::nullopt;
    const std::size_t len = data[6];
    if (headerSize + len > size)
        return std::nullopt;
    FiioReply reply;
    reply.set = set;
    reply.seq = uint16_t((data[3] << 8) | data[4]);
    reply.cmd = data[5];
    reply.payload.assign(data + headerSize, data + headerSize + len);
    reply.crcValid = headerSize + len + 2 <= size
                     && data[headerSize + len] == crc8Maxim(data + 1, headerSize - 1 + len)
                     && data[headerSize + len + 1] == frameEnd;
    return reply;
}

bool matches(const std::vector<uint8_t> &request, const FiioReply &reply)
{
    if (request.empty())
        return false;
    const std::optional<FiioReply> sent = parseReply(request.data(), request.size(), request[0]);
    if (!sent || sent->set != reply.set || sent->cmd != reply.cmd)
        return false;
    if (sent->set && sent->cmd == fiioCmdBand)
        return sameBand(sent->payload, reply.payload);
    if (sent->set && sent->cmd == fiioCmdName)
        return sameName(sent->payload, reply.payload);
    if (sent->set)
        return reply.payload == sent->payload;
    if (sent->cmd == fiioCmdBand || sent->cmd == fiioCmdName)
        return !sent->payload.empty() && !reply.payload.empty() && reply.payload[0] == sent->payload[0];
    return true;
}

std::vector<uint8_t> bandPayload(const FiioModel &model, const FiioBand &band)
{
    if (band.index < 0 || band.index >= fiioBandCount || !std::isfinite(band.gain) || !std::isfinite(band.freq)
        || !std::isfinite(band.q))
        return {};
    const int type = band.enabled ? std::clamp(band.type, 0, typeLast) : 0;
    const double gain = !band.enabled || type >= gainFreeFirst ? 0.0
                                                               : std::clamp(band.gain, model.minGain, model.maxGain);
    const double freq = std::clamp(band.freq, model.minFreq, model.maxFreq);
    const double q = std::clamp(band.q, model.minQ, model.maxQ);
    std::vector<uint8_t> out{uint8_t(band.index)};
    put16(out, int(std::lround(gain * 10.0)));
    put16(out, int(std::lround(freq)));
    put16(out, int(std::lround(q * 100.0)));
    out.push_back(uint8_t(type));
    return out;
}

std::optional<FiioBand> bandOfPayload(const std::vector<uint8_t> &payload)
{
    if (payload.size() != std::size_t(fiioBandPayloadSize))
        return std::nullopt;
    FiioBand band;
    band.index = payload[0];
    band.gain = signed16(payload[1], payload[2]) / 10.0;
    band.freq = unsigned16(payload[3], payload[4]);
    band.q = signed16(payload[5], payload[6]) / 100.0;
    band.type = payload[7];
    return band;
}

std::vector<uint8_t> preampPayload(const FiioModel &model, double gainDb)
{
    std::vector<uint8_t> out;
    put16(out, int(std::lround(std::clamp(gainDb, model.minGain, model.maxGain) * 10.0)));
    return out;
}

std::vector<uint8_t> namePayload(uint8_t preset, std::string_view name)
{
    std::vector<uint8_t> out{preset};
    for (int i = 0; i < fiioNameLength; ++i)
        out.push_back(i < int(name.size()) ? uint8_t(name[i]) : 0);
    return out;
}

std::string nameOfPayload(const std::vector<uint8_t> &payload)
{
    std::string name;
    for (std::size_t i = 1; i < payload.size() && payload[i] != 0; ++i)
        name += char(payload[i]);
    return name;
}

std::vector<uint8_t> presetPayload(uint8_t preset)
{
    return {preset};
}

const std::vector<FiioModel> &fiioModels()
{
    static const std::vector<FiioModel> models{
        {0x2972, 0x0149, "FiiO SNOWSKY TINY B", 0, 0x05, 0x86, fiioReportId, 162, -12.0, 12.0, 20.0, 20000.0, 0.25,
         8.0, "1.3.0"},
    };
    return models;
}

const FiioModel *fiioModel(uint16_t vid, uint16_t pid)
{
    for (const FiioModel &model : fiioModels()) {
        if (model.vid == vid && model.pid == pid)
            return &model;
    }
    return nullptr;
}

std::optional<FiioMatch> supportedModel(const std::string &sysRoot, int alsaIndex)
{
    std::error_code ec;
    const fs::path link = fs::path(sysRoot) / "class/sound" / ("card" + std::to_string(alsaIndex)) / "device";
    const fs::path device = fs::canonical(link, ec);
    if (ec)
        return std::nullopt;
    const std::optional<FiioMatch> match = matchDevice(device.parent_path());
    if (!match || !interfaceSupported(match->devPath, *match->model))
        return std::nullopt;
    return match;
}

std::vector<FiioMatch> usbDevicesByModel(const std::string &sysRoot)
{
    std::vector<FiioMatch> out;
    std::error_code ec;
    for (const fs::path &path : entries(fs::path(sysRoot) / "bus/usb/devices")) {
        const fs::path dev = fs::canonical(path, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        if (std::optional<FiioMatch> match = matchDevice(dev))
            out.push_back(*match);
    }
    std::sort(out.begin(), out.end(), [](const FiioMatch &a, const FiioMatch &b) { return a.devPath < b.devPath; });
    return out;
}

bool interfaceSupported(const std::string &devPath, const FiioModel &model)
{
    const fs::path dev(devPath);
    const fs::path iface = dev / (dev.filename().string() + ":1." + std::to_string(model.interface));
    char outEp[8];
    char inEp[8];
    std::snprintf(outEp, sizeof outEp, "ep_%02x", model.epOut);
    std::snprintf(inEp, sizeof inEp, "ep_%02x", model.epIn);
    std::error_code ec;
    return readText(iface / "bInterfaceClass") == "03" && fs::is_directory(iface / outEp, ec)
           && fs::is_directory(iface / inEp, ec);
}

std::string backupName(const FiioModel &model, int number)
{
    return backupPrefix(model) + std::to_string(number) + ".txt";
}

std::optional<std::string> newestBackup(const std::string &dir, const FiioModel &model)
{
    const int highest = highestBackup(dir, model);
    if (highest == 0)
        return std::nullopt;
    return (fs::path(dir) / backupName(model, highest)).string();
}

std::optional<FiioBackup> readBackup(const std::string &path)
{
    std::ifstream in(path);
    if (!in)
        return std::nullopt;
    FiioBackup backup;
    bool model = false;
    bool firmware = false;
    bool date = false;
    bool name = false;
    bool preamp = false;
    std::array<bool, fiioBandCount> bands{};
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (key == "model") {
            backup.model = value;
            model = true;
        } else if (key == "firmware") {
            backup.firmware = value;
            firmware = true;
        } else if (key == "date") {
            backup.date = value;
            date = true;
        } else if (key == "name") {
            backup.name = value;
            name = true;
        } else if (key == "preamp") {
            std::optional<std::vector<uint8_t>> bytes = unhex(value, 2);
            if (!bytes)
                return std::nullopt;
            backup.preamp = *bytes;
            preamp = true;
        } else if (key.size() == 5 && key.compare(0, 4, "band") == 0 && std::isdigit(uint8_t(key[4]))) {
            std::optional<std::vector<uint8_t>> bytes = unhex(value, fiioBandPayloadSize);
            if (!bytes || (*bytes)[0] != key[4] - '0')
                return std::nullopt;
            backup.bands[key[4] - '0'] = *bytes;
            bands[key[4] - '0'] = true;
        }
    }
    if (!model || !firmware || !date || !name || !preamp
        || !std::all_of(bands.begin(), bands.end(), [](bool b) { return b; }))
        return std::nullopt;
    return backup;
}

std::optional<std::string> writeBackup(const std::string &dir, const FiioModel &model, const FiioBackup &backup)
{
    if (backup.model != modelKey(model) || backup.preamp.size() != 2 || !printable(backup.name)
        || std::any_of(backup.bands.begin(), backup.bands.end(),
                       [](const std::vector<uint8_t> &b) { return b.size() != std::size_t(fiioBandPayloadSize); }))
        return std::nullopt;
    for (int i = 0; i < fiioBandCount; ++i) {
        if (backup.bands[i][0] != i)
            return std::nullopt;
    }
    if (!ensureDir(dir))
        return std::nullopt;
    const int highest = highestBackup(dir, model);
    if (highest > 0) {
        const std::string newest = (fs::path(dir) / backupName(model, highest)).string();
        const std::optional<FiioBackup> previous = readBackup(newest);
        if (previous && sameSlot(*previous, backup))
            return newest;
    }
    const std::string target = (fs::path(dir) / backupName(model, highest + 1)).string();
    const std::string temp = target + ".tmp";
    ::unlink(temp.c_str());
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0)
        return std::nullopt;
    const bool ok = ::fchmod(fd, 0600) == 0 && writeFully(fd, backupText(backup)) && ::fsync(fd) == 0;
    if (::close(fd) != 0 || !ok || ::rename(temp.c_str(), target.c_str()) != 0) {
        ::unlink(temp.c_str());
        return std::nullopt;
    }
    if (!syncDir(dir))
        return std::nullopt;
    const std::optional<FiioBackup> written = readBackup(target);
    if (!written || !sameSlot(*written, backup) || written->date != backup.date)
        return std::nullopt;
    return target;
}

std::string_view reasonText(FiioReason reason)
{
    switch (reason) {
    case FiioReason::None:
        return "none";
    case FiioReason::HidBound:
        return "hid bound";
    case FiioReason::Lock:
        return "lock";
    case FiioReason::Claim:
        return "claim";
    case FiioReason::Timeout:
        return "timeout";
    case FiioReason::Overflow:
        return "overflow";
    case FiioReason::Ack:
        return "ack";
    case FiioReason::Permission:
        return "permission";
    case FiioReason::Backup:
        return "backup";
    case FiioReason::Device:
        return "device";
    case FiioReason::Abandoned:
        return "abandoned";
    case FiioReason::Deadline:
        return "deadline";
    case FiioReason::ResetDone:
        return "reset done";
    case FiioReason::Refused:
        return "refused";
    }
    return "none";
}

int64_t fiioNowMs()
{
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

UsbfsIo::~UsbfsIo()
{
    close();
}

int UsbfsIo::open(const std::string &path)
{
    close();
    const int fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0)
        return -errno;
    m_fd = fd;
    return 0;
}

int UsbfsIo::readDescriptor(uint16_t &vid, uint16_t &pid)
{
    if (m_fd < 0)
        return -EBADF;
    uint8_t desc[descriptorSize];
    const ssize_t n = ::pread(m_fd, desc, sizeof desc, 0);
    if (n < 0)
        return -errno;
    if (std::size_t(n) != descriptorSize || desc[0] != descriptorSize || desc[1] != 1)
        return -EIO;
    vid = uint16_t(desc[8] | (desc[9] << 8));
    pid = uint16_t(desc[10] | (desc[11] << 8));
    return 0;
}

int UsbfsIo::claim(int iface)
{
    if (m_fd < 0)
        return -EBADF;
    unsigned int value = unsigned(iface);
    return errnoResult(::ioctl(m_fd, USBDEVFS_CLAIMINTERFACE, &value));
}

int UsbfsIo::release(int iface)
{
    if (m_fd < 0)
        return -EBADF;
    unsigned int value = unsigned(iface);
    return errnoResult(::ioctl(m_fd, USBDEVFS_RELEASEINTERFACE, &value));
}

int UsbfsIo::bulk(uint8_t ep, uint8_t *data, int len, int timeoutMs)
{
    if (m_fd < 0)
        return -EBADF;
    if (!data || len <= 0)
        return -EINVAL;
    usbdevfs_bulktransfer transfer{};
    transfer.ep = ep;
    transfer.len = unsigned(len);
    transfer.timeout = unsigned(std::max(timeoutMs, 1));
    transfer.data = data;
    return errnoResult(::ioctl(m_fd, USBDEVFS_BULK, &transfer));
}

int UsbfsIo::reset()
{
    if (m_fd < 0)
        return -EBADF;
    return errnoResult(::ioctl(m_fd, USBDEVFS_RESET, 0));
}

void UsbfsIo::close()
{
    if (m_fd >= 0)
        ::close(m_fd);
    m_fd = -1;
}

namespace {

std::string envOr(const char *name, const std::string &fallback)
{
    const char *value = std::getenv(name);
    return value && *value ? std::string(value) : fallback;
}

std::string isoNow()
{
    const time_t now = ::time(nullptr);
    tm parts{};
    ::gmtime_r(&now, &parts);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &parts);
    return buf;
}

std::string firmwareText(const std::vector<uint8_t> &payload)
{
    std::string out;
    for (std::size_t i = 0; i < payload.size() && i < 3; ++i)
        out += (i ? "." : "") + std::to_string(payload[i]);
    return out;
}

bool writeMarker(const FiioPaths &paths, const FiioModel &model)
{
    if (!ensureDir(paths.stateDir))
        return false;
    const std::string path = fiioMarkerPath(paths);
    const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool ok = writeFully(fd, modelKey(model) + "\n") && ::fsync(fd) == 0;
    if (::close(fd) != 0 || !ok)
        return false;
    return syncDir(paths.stateDir);
}

void removeMarker(const FiioPaths &paths)
{
    ::unlink(fiioMarkerPath(paths).c_str());
}

bool validFrames(const FiioProfileFrames &frames)
{
    for (int i = 0; i < fiioBandCount; ++i) {
        if (frames.bands[i].size() != std::size_t(fiioBandPayloadSize) || frames.bands[i][0] != i)
            return false;
    }
    return true;
}

struct Run
{
    FiioResult get(uint8_t cmd, const std::vector<uint8_t> &payload = {})
    {
        return step(frameGet(device.nextSeq(), cmd, payload));
    }

    FiioResult set(uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        return step(frameSet(device.nextSeq(), cmd, payload));
    }

    FiioResult step(const std::vector<uint8_t> &frame)
    {
        if (abandoned && abandoned())
            return {FiioReason::Abandoned, {}};
        return device.transact(frame, deadline);
    }

    FiioDevice &device;
    const FiioBatch::Abandoned &abandoned;
    std::optional<int64_t> deadline;
};

}

FiioPaths fiioPathsFromEnv()
{
    FiioPaths paths;
    paths.runDir = envOr("CASDAP_RUN_DIR", "/run/user/" + std::to_string(::getuid()));
    std::error_code ec;
    paths.runDirExists = fs::is_directory(paths.runDir, ec);
    const std::string home = envOr("HOME", "");
    paths.stateDir = envOr("XDG_STATE_HOME", home + "/.local/state") + "/casdap";
    paths.configDir = envOr("XDG_CONFIG_HOME", home + "/.config") + "/casdap";
    paths.sysRoot = envOr("CASDAP_SYS_ROOT", "/sys");
    paths.asoundDir = envOr("CASDAP_ASOUND_DIR", "/proc/asound");
    return paths;
}

std::string fiioMarkerPath(const FiioPaths &paths)
{
    return paths.stateDir + "/fiio-taking";
}

FiioLock::FiioLock(const FiioPaths &paths)
    : m_path(paths.runDir + "/casdap-fiio.lock")
    , m_none(!paths.runDirExists)
{
}

FiioLock::~FiioLock()
{
    release();
}

bool FiioLock::acquire(int timeoutMs, std::optional<int64_t> deadline, const std::function<bool()> &abandoned)
{
    if (m_held)
        return true;
    if (m_none) {
        m_held = true;
        return true;
    }
    m_fd = ::open(m_path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (m_fd < 0)
        return false;
    int64_t end = fiioNowMs() + std::max(timeoutMs, 0);
    if (deadline)
        end = std::min(end, *deadline);
    for (;;) {
        if (::flock(m_fd, LOCK_EX | LOCK_NB) == 0) {
            m_held = true;
            return true;
        }
        if (errno != EWOULDBLOCK && errno != EINTR)
            break;
        if (abandoned && abandoned())
            break;
        const int64_t now = fiioNowMs();
        if (now >= end)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(10, end - now)));
    }
    ::close(m_fd);
    m_fd = -1;
    return false;
}

void FiioLock::release()
{
    if (m_fd >= 0) {
        ::flock(m_fd, LOCK_UN);
        ::close(m_fd);
    }
    m_fd = -1;
    m_held = false;
}

bool FiioLock::held() const
{
    return m_held;
}

InflightStamp::InflightStamp(const FiioPaths &paths, const std::string &sysPath)
{
    std::error_code ec;
    const fs::path device = fs::canonical(sysPath, ec);
    m_path = paths.runDir + "/casdap-fiio.inflight." + (ec ? fs::path(sysPath) : device).filename().string();
}

bool InflightStamp::write(int64_t ms, const std::string &device) const
{
    const std::string temp = m_path + ".tmp" + std::to_string(::getpid());
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    if (fd < 0)
        return false;
    const bool ok = writeFully(fd, std::to_string(ms) + " " + device + "\n");
    if (::close(fd) != 0 || !ok || ::rename(temp.c_str(), m_path.c_str()) != 0) {
        ::unlink(temp.c_str());
        return false;
    }
    return true;
}

std::optional<InflightStamp::Entry> InflightStamp::read() const
{
    std::error_code ec;
    if (!fs::exists(fs::symlink_status(m_path, ec)))
        return std::nullopt;
    std::ifstream in(m_path);
    std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    Entry entry;
    if (!text.empty() && text.back() == '\n')
        text.pop_back();
    const std::size_t space = text.find(' ');
    const auto digit = [](char c) { return std::isdigit(uint8_t(c)); };
    const auto blank = [](char c) { return std::isspace(uint8_t(c)) || uint8_t(c) < 0x20; };
    if (space != std::string::npos && space > 0 && space <= 18 && space + 1 < text.size()
        && std::all_of(text.begin(), text.begin() + space, digit)
        && std::none_of(text.begin() + space + 1, text.end(), blank)) {
        entry.ms = std::stoll(text.substr(0, space));
        entry.device = text.substr(space + 1);
        entry.parsed = true;
        return entry;
    }
    entry.ms = fiioNowMs();
    return entry;
}

void InflightStamp::remove() const
{
    ::unlink(m_path.c_str());
}

const std::string &InflightStamp::path() const
{
    return m_path;
}

FiioReason waitDriverFree(const std::string &sysPath, int ms, std::optional<int64_t> deadline,
                          const std::function<bool()> &abandoned)
{
    const fs::path dev(sysPath);
    const fs::path iface = dev / (dev.filename().string() + ":1.0");
    int64_t end = fiioNowMs() + std::max(ms, 0);
    bool capped = false;
    if (deadline && *deadline < end) {
        end = *deadline;
        capped = true;
    }
    for (;;) {
        std::error_code ec;
        std::error_code linkEc;
        if (fs::is_directory(iface, ec) && !fs::exists(fs::symlink_status(iface / "driver", linkEc)))
            return FiioReason::None;
        if (abandoned && abandoned())
            return FiioReason::Abandoned;
        const int64_t now = fiioNowMs();
        if (now >= end)
            return capped ? FiioReason::Deadline : FiioReason::HidBound;
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(20, end - now)));
    }
}

FiioDevice::FiioDevice(UsbIo &io, const FiioModel &model, Bounds bounds, FiioPaths paths)
    : m_io(io)
    , m_model(model)
    , m_bounds{std::max(bounds.outMs, 1),         std::max(bounds.readMs, 1),        std::max(bounds.replyWindowMs, 1),
               std::max(bounds.drainReadMs, 1),   std::max(bounds.drainReads, 1),    std::max(bounds.stampWindowMs, 1),
               std::max(bounds.drainCap, 1)}
    , m_paths(std::move(paths))
{
}

FiioDevice::~FiioDevice()
{
    releaseClose();
}

FiioReason FiioDevice::openClaimed(const std::string &sysPath, std::optional<int64_t> deadline)
{
    FiioReason reason = openOnly(sysPath, deadline);
    if (reason != FiioReason::None)
        return reason;
    reason = drain(deadline);
    if (reason != FiioReason::None)
        releaseClose();
    return reason;
}

FiioReason FiioDevice::openClaimedNoDrain(const std::string &sysPath)
{
    const FiioReason reason = openOnly(sysPath, std::nullopt);
    m_noDrain = reason == FiioReason::None;
    return reason;
}

FiioReason FiioDevice::openOnly(const std::string &sysPath, std::optional<int64_t> deadline)
{
    releaseClose();
    if (deadline && fiioNowMs() >= *deadline)
        return FiioReason::Deadline;
    const std::optional<std::string> node = devNode(sysPath);
    if (!node) {
        log("no device node for " + sysPath);
        return FiioReason::Device;
    }
    const int opened = m_io.open(*node);
    if (opened < 0) {
        log("open " + *node + " failed " + std::to_string(-opened));
        return openReason(opened);
    }
    m_open = true;
    std::error_code ec;
    const fs::path canonical = fs::canonical(sysPath, ec);
    m_sysPath = ec ? sysPath : canonical.string();
    if (m_paths.runDirExists)
        m_stamp.emplace(m_paths, m_sysPath);
    else
        m_stamp.reset();
    uint16_t vid = 0;
    uint16_t pid = 0;
    const int described = m_io.readDescriptor(vid, pid);
    if (described < 0) {
        log("descriptor read failed " + std::to_string(-described));
        releaseClose();
        return FiioReason::Device;
    }
    if (vid != m_model.vid || pid != m_model.pid) {
        char buf[64];
        std::snprintf(buf, sizeof buf, "descriptor %04x:%04x, not %04x:%04x", vid, pid, m_model.vid, m_model.pid);
        log(buf);
        releaseClose();
        return FiioReason::Device;
    }
    const int claimed = m_io.claim(m_model.interface);
    if (claimed < 0) {
        log("claim failed " + std::to_string(-claimed));
        releaseClose();
        if (claimed == -EBUSY)
            return FiioReason::Claim;
        return openReason(claimed);
    }
    m_claimed = true;
    return FiioReason::None;
}

FiioReason FiioDevice::drain(std::optional<int64_t> deadline)
{
    std::optional<InflightStamp::Entry> entry;
    if (m_stamp) {
        entry = m_stamp->read();
        if (entry && entry->parsed && entry->device != m_sysPath)
            entry->ms = fiioNowMs();
    }
    const int cap = entry ? m_bounds.drainCap : std::min(m_bounds.drainReads, m_bounds.drainCap);
    const int64_t start = fiioNowMs();
    const int64_t windowEnd = entry ? std::min(entry->ms, start) + m_bounds.stampWindowMs : start;
    uint8_t in[fiioPacketSize];
    for (int i = 0; i < cap; ++i) {
        const int64_t now = fiioNowMs();
        if (deadline && now >= *deadline)
            return FiioReason::Deadline;
        const int64_t uncapped = now < windowEnd ? std::min<int64_t>(m_bounds.readMs, windowEnd - now)
                                                 : m_bounds.drainReadMs;
        const int64_t wait = deadline ? std::min<int64_t>(uncapped, *deadline - now) : uncapped;
        std::fill(std::begin(in), std::end(in), 0);
        const int n = m_io.bulk(m_model.epIn, in, fiioPacketSize, int(wait));
        if (n == -ETIMEDOUT) {
            if (wait < uncapped)
                return FiioReason::Deadline;
            if (fiioNowMs() < windowEnd)
                continue;
            if (entry)
                m_stamp->remove();
            return FiioReason::None;
        }
        if (n == -EOVERFLOW) {
            log("drain overflow");
            return FiioReason::Overflow;
        }
        if (n < 0) {
            log("drain failed " + std::to_string(-n));
            return FiioReason::Device;
        }
        std::vector<uint8_t> raw(in, in + n);
        if (const std::optional<FiioReply> reply = parseReply(raw.data(), raw.size(), m_model.reportId))
            raw.resize(std::min(raw.size(), reply->payload.size() + 9));
        log("drained " + hex(raw));
    }
    log("drain cap " + std::to_string(cap));
    return FiioReason::Device;
}

FiioResult FiioDevice::transact(const std::vector<uint8_t> &cmdFrame, std::optional<int64_t> deadline)
{
    if (m_replyPending || m_noDrain)
        return {FiioReason::Refused, {}};
    if (!m_claimed || cmdFrame.empty())
        return {FiioReason::Device, {}};
    const std::optional<FiioReply> sent = parseReply(cmdFrame.data(), cmdFrame.size(), cmdFrame[0]);
    if (!sent)
        return {FiioReason::Device, {}};
    int64_t now = fiioNowMs();
    if (deadline && now >= *deadline)
        return {FiioReason::Deadline, {}};
    if (m_stamp && !m_stamp->write(now, m_sysPath)) {
        log("stamp write failed");
        return {FiioReason::Device, {}};
    }
    int outMs = m_bounds.outMs;
    if (deadline)
        outMs = int(std::min<int64_t>(outMs, *deadline - now));
    std::vector<uint8_t> out = cmdFrame;
    const int written = m_io.bulk(m_model.epOut, out.data(), int(out.size()), outMs);
    m_replyPending = true;
    if (written == -ETIMEDOUT)
        return {outMs < m_bounds.outMs ? FiioReason::Deadline : FiioReason::Timeout, {}};
    if (written != int(out.size())) {
        log("out failed " + std::to_string(written));
        return {FiioReason::Device, {}};
    }
    const int64_t end = fiioNowMs() + m_bounds.replyWindowMs;
    uint8_t in[fiioPacketSize];
    for (;;) {
        now = fiioNowMs();
        if (deadline && now >= *deadline && *deadline < end)
            return {FiioReason::Deadline, {}};
        if (now >= end)
            return {FiioReason::Timeout, {}};
        int64_t wait = std::min<int64_t>(m_bounds.readMs, end - now);
        if (deadline)
            wait = std::min<int64_t>(wait, *deadline - now);
        std::fill(std::begin(in), std::end(in), 0);
        const int n = m_io.bulk(m_model.epIn, in, fiioPacketSize, int(wait));
        if (n == -ETIMEDOUT)
            continue;
        if (n == -EOVERFLOW) {
            log("in overflow");
            return {FiioReason::Overflow, {}};
        }
        if (n < 0) {
            log("in failed " + std::to_string(-n));
            return {FiioReason::Device, {}};
        }
        std::vector<uint8_t> raw(in, in + n);
        const std::optional<FiioReply> reply = parseReply(raw.data(), raw.size(), m_model.reportId);
        if (reply)
            raw.resize(std::min(raw.size(), reply->payload.size() + 9));
        if (reply && matches(cmdFrame, *reply)) {
            m_replyPending = false;
            if (m_stamp)
                m_stamp->remove();
            if (!reply->crcValid && reply->cmd != fiioCmdVersion)
                log("reply crc mismatch " + hex(raw));
            return {FiioReason::None, reply->payload};
        }
        if (reply && sent->set && reply->set && reply->cmd == sent->cmd) {
            if (m_stamp)
                m_stamp->remove();
            log("ack mismatch " + hex(raw));
            return {FiioReason::Ack, {}};
        }
        ++m_discarded;
        log("discarded " + hex(raw));
    }
}

FiioReason FiioDevice::resetAndClose()
{
    if (!m_open)
        return FiioReason::Device;
    if (m_claimed)
        m_io.release(m_model.interface);
    m_claimed = false;
    const int reset = m_io.reset();
    if (reset < 0) {
        log("reset failed " + std::to_string(-reset));
        releaseClose();
        return FiioReason::Device;
    }
    if (m_stamp) {
        const std::optional<InflightStamp::Entry> entry = m_stamp->read();
        if (entry && (!entry->parsed || entry->device == m_sysPath))
            m_stamp->remove();
    }
    releaseClose();
    log("reset");
    return FiioReason::ResetDone;
}

void FiioDevice::releaseClose()
{
    if (m_claimed)
        m_io.release(m_model.interface);
    if (m_open)
        m_io.close();
    m_claimed = false;
    m_open = false;
    m_replyPending = false;
    m_noDrain = false;
}

void FiioDevice::setLog(Log log)
{
    m_log = std::move(log);
}

int FiioDevice::discarded() const
{
    return m_discarded;
}

uint16_t FiioDevice::nextSeq()
{
    m_seq = m_seq >= 32767 ? 0 : uint16_t(m_seq + 1);
    return m_seq;
}

const FiioModel &FiioDevice::model() const
{
    return m_model;
}

void FiioDevice::log(const std::string &line) const
{
    if (m_log)
        m_log(line);
}

namespace FiioBatch {

namespace {

constexpr std::string_view casdapName = "CASDAP";

FiioResult claimSlot(Run &run, FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths,
                     std::optional<int64_t> deadline)
{
    const FiioModel &model = device.model();
    const uint8_t slot = model.slot;
    if (!writeMarker(paths, model)) {
        device.log("marker failed");
        bypass(device, mirror, paths, {}, {}, deadline);
        return {FiioReason::Backup, {}};
    }
    FiioResult r;
    if ((r = run.set(fiioCmdPreset, presetPayload(slot))).reason != FiioReason::None)
        return r;
    mirror.preset = slot;
    FiioBackup backup;
    backup.model = modelKey(model);
    backup.firmware = mirror.firmware;
    backup.date = isoNow();
    backup.name = mirror.name;
    if ((r = run.get(fiioCmdPreamp)).reason != FiioReason::None)
        return r;
    backup.preamp = r.payload;
    for (int i = 0; i < fiioBandCount; ++i) {
        if ((r = run.get(fiioCmdBand, {uint8_t(i)})).reason != FiioReason::None)
            return r;
        backup.bands[i] = r.payload;
    }
    const std::optional<std::string> written = writeBackup(paths.configDir, model, backup);
    if (!written) {
        device.log("backup failed");
        bypass(device, mirror, paths, {}, {}, deadline);
        return {FiioReason::Backup, {}};
    }
    device.log("backup " + *written);
    if ((r = run.set(fiioCmdName, namePayload(slot, casdapName))).reason != FiioReason::None)
        return r;
    mirror.name = casdapName;
    removeMarker(paths);
    return {};
}

}

FiioResult take(FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths, const FiioProfileFrames &frames,
                Abandoned abandoned, std::optional<int64_t> deadline)
{
    mirror.valid = false;
    if (!validFrames(frames))
        return {FiioReason::Refused, {}};
    const FiioModel &model = device.model();
    const uint8_t slot = model.slot;
    Run run{device, abandoned, deadline};
    FiioResult r;
    if (mirror.firmware.empty()) {
        if ((r = run.get(fiioCmdVersion)).reason != FiioReason::None)
            return r;
        mirror.firmware = firmwareText(r.payload);
        if (mirror.firmware != model.firmware)
            device.log("firmware " + mirror.firmware + ", verified " + std::string(model.firmware));
    }
    if ((r = run.get(fiioCmdPreset)).reason != FiioReason::None)
        return r;
    mirror.preset = r.payload.empty() ? -1 : r.payload[0];
    if ((r = run.get(fiioCmdName, {slot})).reason != FiioReason::None)
        return r;
    mirror.name = nameOfPayload(r.payload);
    if (mirror.name == casdapName) {
        removeMarker(paths);
    } else {
        if ((r = claimSlot(run, device, mirror, paths, deadline)).reason != FiioReason::None)
            return r;
    }
    if (mirror.preset != slot) {
        if ((r = run.set(fiioCmdPreset, presetPayload(slot))).reason != FiioReason::None)
            return r;
        mirror.preset = slot;
    }
    const std::vector<uint8_t> preamp = preampPayload(model, 0.0);
    if ((r = run.set(fiioCmdPreamp, preamp)).reason != FiioReason::None)
        return r;
    mirror.preamp = preamp;
    for (int i = 0; i < fiioBandCount; ++i) {
        if ((r = run.set(fiioCmdBand, frames.bands[i])).reason != FiioReason::None)
            return r;
        mirror.bands[i] = frames.bands[i];
    }
    mirror.valid = true;
    device.log("take");
    return {};
}

FiioResult update(FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths, const FiioProfileFrames &frames,
                  Abandoned abandoned, std::optional<int64_t> deadline)
{
    if (!validFrames(frames)) {
        mirror.valid = false;
        return {FiioReason::Refused, {}};
    }
    const uint8_t slot = device.model().slot;
    Run run{device, abandoned, deadline};
    FiioResult r = run.get(fiioCmdPreset);
    if (r.reason != FiioReason::None) {
        mirror.valid = false;
        return r;
    }
    mirror.preset = r.payload.empty() ? -1 : r.payload[0];
    if (mirror.preset != slot || !mirror.valid)
        return take(device, mirror, paths, frames, abandoned, deadline);
    int sets = 0;
    for (int i = 0; i < fiioBandCount; ++i) {
        if (frames.bands[i] == mirror.bands[i])
            continue;
        if ((r = run.set(fiioCmdBand, frames.bands[i])).reason != FiioReason::None) {
            mirror.valid = false;
            return r;
        }
        mirror.bands[i] = frames.bands[i];
        ++sets;
    }
    device.log("update " + std::to_string(sets));
    return {};
}

FiioResult bypass(FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths, const FiioProfileFrames &,
                  Abandoned, std::optional<int64_t> deadline)
{
    const Abandoned never;
    Run run{device, never, deadline};
    const FiioResult r = run.set(fiioCmdPreset, presetPayload(fiioBypass));
    if (r.reason != FiioReason::None)
        return r;
    mirror.preset = fiioBypass;
    removeMarker(paths);
    device.log("bypass acked");
    return {};
}

FiioResult force(FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths, const FiioProfileFrames &,
                 Abandoned abandoned, std::optional<int64_t> deadline)
{
    Run run{device, abandoned, deadline};
    FiioResult r = run.get(fiioCmdPreset);
    if (r.reason != FiioReason::None)
        return r;
    const std::vector<uint8_t> found = r.payload;
    const int preset = found.empty() ? -1 : found[0];
    mirror.preset = preset;
    if (preset != device.model().slot)
        removeMarker(paths);
    if (preset == fiioBypass)
        return {FiioReason::None, found};
    if ((r = run.set(fiioCmdPreset, presetPayload(fiioBypass))).reason != FiioReason::None)
        return r;
    mirror.preset = fiioBypass;
    removeMarker(paths);
    device.log("force from preset " + std::to_string(preset));
    return {FiioReason::None, found};
}

FiioResult dump(FiioDevice &device, FiioMirror &mirror, const FiioPaths &, const FiioProfileFrames &,
                Abandoned abandoned, std::optional<int64_t> deadline)
{
    mirror.valid = false;
    Run run{device, abandoned, deadline};
    FiioResult r;
    if ((r = run.get(fiioCmdVersion)).reason != FiioReason::None)
        return r;
    mirror.firmware = firmwareText(r.payload);
    if ((r = run.get(fiioCmdPreset)).reason != FiioReason::None)
        return r;
    mirror.preset = r.payload.empty() ? -1 : r.payload[0];
    if ((r = run.get(fiioCmdName, {device.model().slot})).reason != FiioReason::None)
        return r;
    mirror.name = nameOfPayload(r.payload);
    if ((r = run.get(fiioCmdPreamp)).reason != FiioReason::None)
        return r;
    mirror.preamp = r.payload;
    for (int i = 0; i < fiioBandCount; ++i) {
        if ((r = run.get(fiioCmdBand, {uint8_t(i)})).reason != FiioReason::None)
            return r;
        mirror.bands[i] = r.payload;
    }
    return {};
}

FiioResult restoreSlot(FiioDevice &device, FiioMirror &mirror, const FiioPaths &paths,
                       const FiioProfileFrames &frames, Abandoned abandoned, std::optional<int64_t> deadline,
                       const std::string &file)
{
    mirror.valid = false;
    const FiioModel &model = device.model();
    const std::optional<std::string> path = file.empty() ? newestBackup(paths.configDir, model)
                                                         : std::optional<std::string>(file);
    const std::optional<FiioBackup> backup = path ? readBackup(*path) : std::nullopt;
    if (!backup || backup->model != modelKey(model)) {
        device.log("no backup " + path.value_or(paths.configDir));
        return {FiioReason::Backup, {}};
    }
    const uint8_t slot = model.slot;
    Run run{device, abandoned, deadline};
    FiioResult r;
    if ((r = run.get(fiioCmdName, {slot})).reason != FiioReason::None)
        return r;
    mirror.name = nameOfPayload(r.payload);
    mirror.preset = -1;
    if (mirror.name != casdapName) {
        if (mirror.firmware.empty()) {
            if ((r = run.get(fiioCmdVersion)).reason != FiioReason::None)
                return r;
            mirror.firmware = firmwareText(r.payload);
        }
        if ((r = claimSlot(run, device, mirror, paths, deadline)).reason != FiioReason::None)
            return r;
    } else if ((r = run.set(fiioCmdPreset, presetPayload(slot))).reason == FiioReason::None) {
        mirror.preset = slot;
    } else {
        return r;
    }
    if ((r = run.set(fiioCmdPreamp, backup->preamp)).reason != FiioReason::None)
        return r;
    for (int i = 0; i < fiioBandCount; ++i) {
        if ((r = run.set(fiioCmdBand, backup->bands[i])).reason != FiioReason::None)
            return r;
    }
    for (int i = 0; i < fiioBandCount; ++i) {
        if ((r = run.get(fiioCmdBand, {uint8_t(i)})).reason != FiioReason::None)
            return r;
        if (!sameBand(backup->bands[i], r.payload)) {
            device.log("restore band " + std::to_string(i) + " reads " + hex(r.payload));
            bypass(device, mirror, paths, frames, {}, deadline);
            return {FiioReason::Ack, {}};
        }
    }
    if ((r = run.set(fiioCmdName, namePayload(slot, backup->name))).reason != FiioReason::None)
        return r;
    mirror.name = backup->name;
    if ((r = bypass(device, mirror, paths, frames, {}, deadline)).reason != FiioReason::None)
        return r;
    device.log("restored, verified " + *path);
    return {};
}

}

}
