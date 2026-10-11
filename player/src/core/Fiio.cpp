#include "Fiio.h"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace casdap {

namespace {

namespace fs = std::filesystem;

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
            if (!bytes)
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
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec)
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

}
