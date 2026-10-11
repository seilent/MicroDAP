#pragma once

#include "Fiio.h"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace casdap {

class FakeUsbIo : public UsbIo
{
public:
    enum class Op { Open, Descriptor, Claim, Release, Out, In, Reset, Close };

    struct Call
    {
        Op op;
        std::string path;
        int len = 0;
        int timeoutMs = 0;
        int result = 0;
        std::vector<uint8_t> data;
    };

    class Hold
    {
    public:
        void release()
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_released = true;
            m_cv.notify_all();
        }

        bool waitEntered(int ms)
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            return m_cv.wait_for(lock, std::chrono::milliseconds(ms), [this] { return m_entered; });
        }

    private:
        friend class FakeUsbIo;

        void enterAndWait()
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_entered = true;
            m_cv.notify_all();
            m_cv.wait(lock, [this] { return m_released; });
        }

        std::mutex m_mutex;
        std::condition_variable m_cv;
        bool m_entered = false;
        bool m_released = false;
    };

    FakeUsbIo()
    {
        for (int p : {0, 1, 2, 3, 4, 5, 6, 160, 161, 162, int(fiioBypass)}) {
            Preset preset;
            for (int i = 0; i < fiioBandCount; ++i) {
                const int freq = 125 * (1 << i) / 4;
                preset.bands[i] = {uint8_t(i), 0, 0, uint8_t(freq >> 8), uint8_t(freq & 0xFF), 0, 70, 0};
            }
            preset.preamp = {0, 0};
            preset.name.assign(fiioNameLength, 0);
            m_presets[p] = preset;
        }
        m_presets[0].preamp = {0xFF, 0xC4};
        m_presets[160].preamp = {0x00, 0x46};
        m_presets[161].preamp = {0x00, 0x14};
        m_presets[162].preamp = {0x00, 0x50};
        m_presets[fiioBypass].preamp = {0x00, 0x14};
        setName(160, "FD15");
        setName(161, "FF5");
        setName(162, "FH3");
        m_other[0x02] = {0x3C};
    }

    int open(const std::string &path) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Open, lock);
        int result = 0;
        if (m_openError)
            result = m_openError;
        else {
            m_open = true;
            m_gone = false;
        }
        m_calls.push_back({Op::Open, path, 0, 0, result, {}});
        return result;
    }

    int readDescriptor(uint16_t &vid, uint16_t &pid) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Descriptor, lock);
        int result = closedError();
        if (!result && m_descriptorError)
            result = m_descriptorError;
        if (!result) {
            vid = m_vid;
            pid = m_pid;
        }
        m_calls.push_back({Op::Descriptor, {}, 0, 0, result, {}});
        return result;
    }

    int claim(int iface) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Claim, lock);
        int result = closedError();
        if (!result && m_claimError)
            result = m_claimError;
        if (!result)
            m_claimed = true;
        m_calls.push_back({Op::Claim, {}, iface, 0, result, {}});
        return result;
    }

    int release(int iface) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Release, lock);
        int result = closedError();
        if (!result && !m_claimed)
            result = -EINVAL;
        m_claimed = false;
        m_calls.push_back({Op::Release, {}, iface, 0, result, {}});
        return result;
    }

    int bulk(uint8_t ep, uint8_t *data, int len, int timeoutMs) override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        const bool in = ep & 0x80;
        pause(in ? Op::In : Op::Out, lock);
        if (in) {
            const int result = readIn(lock, data, len, timeoutMs);
            std::vector<uint8_t> got;
            if (result > 0)
                got.assign(data, data + result);
            m_calls.push_back({Op::In, {}, len, timeoutMs, result, got});
            return result;
        }
        const std::vector<uint8_t> frame = data && len > 0 ? std::vector<uint8_t>(data, data + len)
                                                           : std::vector<uint8_t>{};
        const int result = writeOut(frame, timeoutMs);
        m_calls.push_back({Op::Out, {}, len, timeoutMs, result, frame});
        return result;
    }

    int reset() override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Reset, lock);
        int result = closedError();
        if (!result && m_resetError)
            result = m_resetError;
        if (!result) {
            m_pending.clear();
            m_wedged = false;
            m_cv.notify_all();
        }
        m_calls.push_back({Op::Reset, {}, 0, 0, result, {}});
        return result;
    }

    void close() override
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        pause(Op::Close, lock);
        m_open = false;
        m_claimed = false;
        m_cv.notify_all();
        m_calls.push_back({Op::Close, {}, 0, 0, 0, {}});
    }

    void simulateReplug()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending.clear();
        m_wedged = false;
        m_counter = 0;
        if (m_open)
            m_gone = true;
        m_open = false;
        m_claimed = false;
        m_cv.notify_all();
    }

    std::shared_ptr<Hold> hold(Op op)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto handle = std::make_shared<Hold>();
        m_holds[op].push_back(handle);
        return handle;
    }

    void setReplyDelayMs(int ms)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_delayMs = ms;
    }

    void setDescriptor(uint16_t vid, uint16_t pid)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_vid = vid;
        m_pid = pid;
    }

    void setDescriptorError(int error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_descriptorError = error;
    }

    void setOpenError(int error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_openError = error;
    }

    void setClaimError(int error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_claimError = error;
    }

    void setOutError(int error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_outError = error;
    }

    void setResetError(int error)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_resetError = error;
    }

    void strayBeforeReply(const std::vector<std::vector<uint8_t>> &packets)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_strays = packets;
    }

    std::vector<Call> calls() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_calls;
    }

    int count(Op op) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return int(std::count_if(m_calls.begin(), m_calls.end(), [op](const Call &c) { return c.op == op; }));
    }

    uint8_t preset() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_preset;
    }

    std::vector<uint8_t> band(int preset, int index) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_presets.at(preset).bands.at(index);
    }

    std::vector<uint8_t> preamp(int preset) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_presets.at(preset).preamp;
    }

    std::string name(int preset) const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<uint8_t> payload{uint8_t(preset)};
        const std::vector<uint8_t> &bytes = m_presets.at(preset).name;
        payload.insert(payload.end(), bytes.begin(), bytes.end());
        return nameOfPayload(payload);
    }

    uint16_t counter() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_counter;
    }

    bool wedged() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_wedged;
    }

    int pending() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return int(m_pending.size());
    }

    int refusedReads() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_refusedReads;
    }

    int badFrames() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_badFrames;
    }

    bool claimed() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_claimed;
    }

    bool isOpen() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_open;
    }

private:
    using Clock = std::chrono::steady_clock;

    struct Preset
    {
        std::array<std::vector<uint8_t>, fiioBandCount> bands;
        std::vector<uint8_t> preamp;
        std::vector<uint8_t> name;
    };

    struct Pending
    {
        std::vector<uint8_t> packet;
        Clock::time_point ready;
    };

    static std::vector<uint8_t> storedBand(std::vector<uint8_t> payload)
    {
        static const std::map<int, int> recorded{{71, 70}, {300, 301}, {800, 803}, {1000, 1003}};
        const int q = (payload[5] << 8) | payload[6];
        auto it = recorded.find(q);
        if (it != recorded.end()) {
            payload[5] = uint8_t(it->second >> 8);
            payload[6] = uint8_t(it->second & 0xFF);
        }
        return payload;
    }

    void setName(int preset, std::string_view name)
    {
        const std::vector<uint8_t> payload = namePayload(uint8_t(preset), name);
        m_presets[preset].name.assign(payload.begin() + 1, payload.end());
    }

    void pause(Op op, std::unique_lock<std::mutex> &lock)
    {
        auto it = m_holds.find(op);
        if (it == m_holds.end() || it->second.empty())
            return;
        const std::shared_ptr<Hold> handle = it->second.front();
        it->second.pop_front();
        lock.unlock();
        handle->enterAndWait();
        lock.lock();
    }

    int closedError() const
    {
        if (m_open)
            return 0;
        return m_gone ? -ENODEV : -EBADF;
    }

    int readIn(std::unique_lock<std::mutex> &lock, uint8_t *data, int len, int timeoutMs)
    {
        if (const int error = closedError())
            return error;
        if (!data || len != fiioPacketSize || timeoutMs <= 0) {
            ++m_refusedReads;
            return -EINVAL;
        }
        const Clock::time_point until = Clock::now() + std::chrono::milliseconds(timeoutMs);
        for (;;) {
            if (const int error = closedError())
                return error;
            if (m_wedged)
                return -EOVERFLOW;
            const Clock::time_point now = Clock::now();
            if (!m_pending.empty() && m_pending.front().ready <= now) {
                const std::vector<uint8_t> packet = m_pending.front().packet;
                m_pending.pop_front();
                std::memcpy(data, packet.data(), fiioPacketSize);
                return fiioPacketSize;
            }
            if (now >= until)
                return -ETIMEDOUT;
            Clock::time_point wake = until;
            if (!m_pending.empty())
                wake = std::min(wake, m_pending.front().ready);
            m_cv.wait_until(lock, wake);
        }
    }

    int writeOut(const std::vector<uint8_t> &frame, int timeoutMs)
    {
        if (const int error = closedError())
            return error;
        if (frame.empty() || timeoutMs <= 0)
            return -EINVAL;
        if (const int error = m_outError) {
            m_outError = 0;
            return error;
        }
        if (m_wedged)
            return int(frame.size());
        if (!m_pending.empty()) {
            m_wedged = true;
            m_cv.notify_all();
            return int(frame.size());
        }
        if (!validFrame(frame)) {
            ++m_badFrames;
            return int(frame.size());
        }
        const bool set = frame[1] == 0xAA;
        const uint8_t cmd = frame[5];
        const std::vector<uint8_t> payload(frame.begin() + 7, frame.begin() + 7 + frame[6]);
        const Clock::time_point ready = Clock::now() + std::chrono::milliseconds(m_delayMs);
        for (std::vector<uint8_t> stray : m_strays) {
            stray.resize(fiioPacketSize, 0);
            m_pending.push_back({stray, ready});
        }
        m_strays.clear();
        m_pending.push_back({replyPacket(set, cmd, set ? applySet(cmd, payload) : answerGet(cmd, payload)), ready});
        m_cv.notify_all();
        return int(frame.size());
    }

    static bool validFrame(const std::vector<uint8_t> &frame)
    {
        if (frame.size() < 9 || frame[0] != fiioReportId)
            return false;
        const bool set = frame[1] == 0xAA && frame[2] == 0x0A;
        const bool get = frame[1] == 0xBB && frame[2] == 0x0B;
        const std::size_t len = frame[6];
        return (set || get) && frame.size() == len + 9 && frame[len + 7] == crc8Maxim(frame.data(), len + 7)
               && frame[len + 8] == 0xEE;
    }

    std::vector<uint8_t> replyPacket(bool set, uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        const uint16_t seq = m_counter;
        m_counter = m_counter >= 32767 ? 0 : uint16_t(m_counter + 1);
        std::vector<uint8_t> out{fiioReportId, uint8_t(set ? 0xAA : 0xBB), uint8_t(set ? 0x0A : 0x0B),
                                 uint8_t(seq >> 8), uint8_t(seq & 0xFF), cmd, uint8_t(payload.size())};
        out.insert(out.end(), payload.begin(), payload.end());
        if (set || cmd != fiioCmdVersion) {
            out.push_back(crc8Maxim(out.data() + 1, out.size() - 1));
            out.push_back(0xEE);
        }
        out.resize(fiioPacketSize, 0);
        return out;
    }

    std::vector<uint8_t> answerGet(uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        const Preset &active = m_presets.at(m_preset);
        switch (cmd) {
        case fiioCmdVersion:
            return {0x01, 0x03, 0x00};
        case fiioCmdPreset:
            return {m_preset};
        case fiioCmdPreamp:
            return active.preamp;
        case fiioCmdBand: {
            const uint8_t index = payload.empty() ? 0 : payload[0];
            if (m_preset == fiioBypass || index >= fiioBandCount) {
                std::vector<uint8_t> zero(fiioBandPayloadSize, 0);
                zero[0] = index;
                return zero;
            }
            return active.bands[index];
        }
        case fiioCmdName: {
            const uint8_t preset = payload.empty() ? 0 : payload[0];
            std::vector<uint8_t> out{preset};
            auto it = m_presets.find(preset);
            if (it == m_presets.end())
                out.resize(1 + fiioNameLength, 0);
            else
                out.insert(out.end(), it->second.name.begin(), it->second.name.end());
            return out;
        }
        default: {
            auto it = m_other.find(cmd);
            return it == m_other.end() ? std::vector<uint8_t>{0} : it->second;
        }
        }
    }

    std::vector<uint8_t> applySet(uint8_t cmd, const std::vector<uint8_t> &payload)
    {
        switch (cmd) {
        case fiioCmdPreset:
            if (payload.size() == 1 && m_presets.count(payload[0]))
                m_preset = payload[0];
            return payload;
        case fiioCmdPreamp:
            if (payload.size() == 2)
                m_presets.at(m_preset).preamp = payload;
            return payload;
        case fiioCmdBand: {
            if (payload.size() != std::size_t(fiioBandPayloadSize))
                return payload;
            const std::vector<uint8_t> stored = storedBand(payload);
            if (stored[0] < fiioBandCount && m_preset != fiioBypass)
                m_presets.at(m_preset).bands[stored[0]] = stored;
            return stored;
        }
        case fiioCmdName: {
            if (payload.size() < std::size_t(1 + fiioNameLength))
                return payload;
            if (payload[0] >= 160 && payload[0] <= 162)
                m_presets.at(payload[0]).name.assign(payload.begin() + 1, payload.begin() + 1 + fiioNameLength);
            return std::vector<uint8_t>(payload.begin(), payload.begin() + 1 + fiioNameLength);
        }
        default:
            m_other[cmd] = payload;
            return payload;
        }
    }

    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    std::map<Op, std::deque<std::shared_ptr<Hold>>> m_holds;
    std::vector<Call> m_calls;
    std::deque<Pending> m_pending;
    std::vector<std::vector<uint8_t>> m_strays;
    std::map<int, Preset> m_presets;
    std::map<uint8_t, std::vector<uint8_t>> m_other;
    uint8_t m_preset = fiioBypass;
    uint16_t m_counter = 0;
    uint16_t m_vid = 0x2972;
    uint16_t m_pid = 0x0149;
    int m_delayMs = 0;
    int m_openError = 0;
    int m_descriptorError = 0;
    int m_claimError = 0;
    int m_resetError = 0;
    int m_outError = 0;
    int m_refusedReads = 0;
    int m_badFrames = 0;
    bool m_open = false;
    bool m_gone = false;
    bool m_claimed = false;
    bool m_wedged = false;
};

}
