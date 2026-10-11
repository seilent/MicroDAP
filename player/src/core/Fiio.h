#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace casdap {

constexpr uint8_t fiioReportId = 7;
constexpr uint8_t fiioCmdVersion = 0x0B;
constexpr uint8_t fiioCmdBand = 0x15;
constexpr uint8_t fiioCmdPreset = 0x16;
constexpr uint8_t fiioCmdPreamp = 0x17;
constexpr uint8_t fiioCmdName = 0x30;
constexpr uint8_t fiioBypass = 0xF0;
constexpr int fiioPacketSize = 64;
constexpr int fiioBandCount = 10;
constexpr int fiioNameLength = 8;
constexpr int fiioBandPayloadSize = 8;

struct FiioModel
{
    uint16_t vid;
    uint16_t pid;
    std::string_view name;
    int interface;
    uint8_t epOut;
    uint8_t epIn;
    uint8_t reportId;
    uint8_t slot;
    double minGain;
    double maxGain;
    double minFreq;
    double maxFreq;
    double minQ;
    double maxQ;
    std::string_view firmware;
};

struct FiioBand
{
    int index = 0;
    double gain = 0.0;
    double freq = 1000.0;
    double q = 0.71;
    int type = 0;
    bool enabled = true;
};

struct FiioReply
{
    bool set = false;
    uint16_t seq = 0;
    uint8_t cmd = 0;
    std::vector<uint8_t> payload;
    bool crcValid = false;
};

struct FiioMatch
{
    const FiioModel *model = nullptr;
    std::string devPath;
};

struct FiioBackup
{
    std::string model;
    std::string firmware;
    std::string date;
    std::string name;
    std::vector<uint8_t> preamp;
    std::array<std::vector<uint8_t>, fiioBandCount> bands;
};

uint8_t crc8Maxim(const uint8_t *data, std::size_t size);
std::vector<uint8_t> frameSet(uint16_t seq, uint8_t cmd, const std::vector<uint8_t> &payload,
                              uint8_t reportId = fiioReportId);
std::vector<uint8_t> frameGet(uint16_t seq, uint8_t cmd, const std::vector<uint8_t> &payload = {},
                              uint8_t reportId = fiioReportId);
std::optional<FiioReply> parseReply(const uint8_t *data, std::size_t size, uint8_t reportId = fiioReportId);
bool matches(const std::vector<uint8_t> &request, const FiioReply &reply);

std::vector<uint8_t> bandPayload(const FiioModel &model, const FiioBand &band);
std::optional<FiioBand> bandOfPayload(const std::vector<uint8_t> &payload);
std::vector<uint8_t> preampPayload(const FiioModel &model, double gainDb);
std::vector<uint8_t> namePayload(uint8_t preset, std::string_view name);
std::string nameOfPayload(const std::vector<uint8_t> &payload);
std::vector<uint8_t> presetPayload(uint8_t preset);

const std::vector<FiioModel> &fiioModels();
const FiioModel *fiioModel(uint16_t vid, uint16_t pid);
std::optional<FiioMatch> supportedModel(const std::string &sysRoot, int alsaIndex);
std::vector<FiioMatch> usbDevicesByModel(const std::string &sysRoot);
bool interfaceSupported(const std::string &devPath, const FiioModel &model);

std::string backupName(const FiioModel &model, int number);
std::optional<std::string> newestBackup(const std::string &dir, const FiioModel &model);
std::optional<FiioBackup> readBackup(const std::string &path);
std::optional<std::string> writeBackup(const std::string &dir, const FiioModel &model, const FiioBackup &backup);

}
