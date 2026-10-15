#include "FiioCli.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

extern char **environ;

namespace microdap {

namespace {

namespace fs = std::filesystem;

constexpr int sessionMs = 500;
constexpr int driverMs = 5000;
constexpr int lockMs = 2000;
constexpr int64_t stampSkipMs = 10000;
constexpr long long maxDeadlineMs = 86400000;
constexpr const char *sessionUnit = "gamescope-session-plus@microdap.service";

void say(const std::string &line)
{
    std::cerr << "microdap-fiio: " << line << '\n';
}

int64_t bootMs()
{
    timespec ts{};
    ::clock_gettime(CLOCK_BOOTTIME, &ts);
    return int64_t(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool expired(std::optional<int64_t> deadline)
{
    return deadline && fiioNowMs() >= *deadline;
}

std::string hexText(const std::vector<uint8_t> &bytes)
{
    std::string out;
    char buf[3];
    for (uint8_t b : bytes) {
        std::snprintf(buf, sizeof buf, "%02x", b);
        out += buf;
    }
    return out;
}

std::string deviceName(const FiioMatch &match)
{
    return fs::path(match.devPath).filename().string();
}

fs::path interfacePath(const FiioMatch &match)
{
    const fs::path dev(match.devPath);
    return dev / (dev.filename().string() + ":1." + std::to_string(match.model->interface));
}

std::string resetStampPath(const FiioPaths &paths)
{
    return paths.runDir + "/microdap-fiio.reset";
}

enum class Answer { Proceed, Skip, Deadline };

struct Session
{
    Answer answer = Answer::Skip;
    std::string state;
};

Session sessionCheck(const FiioPaths &paths, std::optional<int64_t> deadline)
{
    std::error_code ec;
    if (!fs::exists(fs::path(paths.runDir) / "systemd/private", ec)) {
        say("no user manager");
        return {Answer::Proceed, "none"};
    }
    const char *bin = std::getenv("MICRODAP_SYSTEMCTL_BIN");
    const std::string program = bin && *bin ? bin : "systemctl";
    std::vector<std::string> args{program, "--user", "show", "-p", "ActiveState", "--value", sessionUnit};
    std::vector<std::string> env;
    for (char **e = environ; e && *e; ++e) {
        if (std::strncmp(*e, "XDG_RUNTIME_DIR=", 16) != 0)
            env.emplace_back(*e);
    }
    env.push_back("XDG_RUNTIME_DIR=" + paths.runDir);
    std::vector<char *> argv;
    for (std::string &a : args)
        argv.push_back(a.data());
    argv.push_back(nullptr);
    std::vector<char *> envp;
    for (std::string &e : env)
        envp.push_back(e.data());
    envp.push_back(nullptr);

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0)
        return {Answer::Skip, "no answer"};
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    pid_t pid = 0;
    const int spawned = posix_spawnp(&pid, program.c_str(), &actions, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&actions);
    ::close(fds[1]);
    if (spawned != 0) {
        ::close(fds[0]);
        say("systemctl failed " + std::string(std::strerror(spawned)));
        return {Answer::Skip, "no answer"};
    }

    int64_t end = fiioNowMs() + sessionMs;
    if (deadline)
        end = std::min(end, *deadline);
    std::string out;
    bool eof = false;
    while (!eof) {
        const int64_t left = end - fiioNowMs();
        if (left <= 0)
            break;
        pollfd p{fds[0], POLLIN, 0};
        const int n = ::poll(&p, 1, int(left));
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            break;
        char buf[256];
        const ssize_t r = ::read(fds[0], buf, sizeof buf);
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            eof = true;
        else
            out.append(buf, std::size_t(r));
    }
    ::close(fds[0]);
    int status = 0;
    bool exited = false;
    for (;;) {
        const pid_t w = ::waitpid(pid, &status, WNOHANG);
        if (w == pid) {
            exited = true;
            break;
        }
        if (w < 0 && errno == ECHILD)
            return {Answer::Skip, "no answer"};
        if (w < 0 && errno != EINTR)
            break;
        if (fiioNowMs() >= end)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    if (!exited) {
        ::kill(pid, SIGKILL);
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
        if (expired(deadline))
            return {Answer::Deadline, {}};
        return {Answer::Skip, "no answer"};
    }
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0)
        return {Answer::Skip, "no answer"};
    const std::string state = out.substr(0, out.find('\n'));
    if (state == "deactivating" || state == "inactive" || state == "failed")
        return {Answer::Proceed, state};
    return {Answer::Skip, state.empty() ? "no answer" : state};
}

enum class Wait { Ready, NotReady, Bound, Deadline };

Wait driverWait(const FiioMatch &match, bool noWait, std::optional<int64_t> deadline)
{
    const fs::path iface = interfacePath(match);
    std::error_code ec;
    if (noWait) {
        if (!fs::is_directory(iface, ec))
            return Wait::NotReady;
        if (fs::exists(fs::symlink_status(iface / "driver", ec)))
            return Wait::Bound;
        return Wait::Ready;
    }
    const FiioReason reason = waitDriverFree(match.devPath, driverMs, deadline, {});
    if (reason == FiioReason::None)
        return Wait::Ready;
    if (reason == FiioReason::Deadline)
        return Wait::Deadline;
    return fs::is_directory(iface, ec) ? Wait::Bound : Wait::NotReady;
}

bool waitSkipped(Wait wait, const FiioMatch &match)
{
    if (wait == Wait::NotReady)
        say(deviceName(match) + " interface not ready, skipped");
    else if (wait == Wait::Bound)
        say(deviceName(match) + " hid bound, skipped");
    return wait == Wait::NotReady || wait == Wait::Bound;
}

std::vector<FiioMatch> supportedDacs(const FiioPaths &paths)
{
    std::vector<FiioMatch> dacs = usbDevicesByModel(paths.sysRoot);
    if (dacs.empty())
        say("no supported dac");
    return dacs;
}

int deadlineExit()
{
    say("deadline");
    return 0;
}

std::string foundWord(int preset, bool marker, const FiioModel &model)
{
    if (preset == model.slot)
        return "microdap slot";
    if (marker)
        return "marker";
    return "preset " + std::to_string(preset);
}

bool recentlyStamped(const FiioPaths &paths)
{
    std::ifstream in(resetStampPath(paths));
    long long ms = 0;
    if (!(in >> ms))
        return false;
    const int64_t age = bootMs() - ms;
    if (age < 0 || age >= stampSkipMs)
        return false;
    say("reset " + std::to_string(age) + " ms ago, skipped");
    return true;
}

bool writeResetStamp(const FiioPaths &paths)
{
    const std::string path = resetStampPath(paths);
    const std::string temp = path + ".tmp" + std::to_string(::getpid());
    {
        std::ofstream out(temp, std::ios::trunc);
        out << bootMs() << '\n';
        if (!out.flush()) {
            ::unlink(temp.c_str());
            return false;
        }
    }
    if (::rename(temp.c_str(), path.c_str()) != 0) {
        ::unlink(temp.c_str());
        return false;
    }
    return true;
}

FiioDevice::Log deviceLog(const FiioMatch &match)
{
    const std::string name = deviceName(match);
    return [name](const std::string &line) { say(name + " " + line); };
}

int resetVerb(const std::vector<std::string> &args, UsbIoFactory &factory, const FiioPaths &paths)
{
    bool noWait = false;
    bool stamp = false;
    bool skipStamped = false;
    std::optional<int64_t> deadline;
    const int64_t start = fiioNowMs();
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == "--no-wait") {
            noWait = true;
        } else if (args[i] == "--stamp") {
            stamp = true;
        } else if (args[i] == "--skip-stamped") {
            skipStamped = true;
        } else if (args[i] == "--deadline") {
            if (i + 1 >= args.size()) {
                say("missing deadline");
                return 1;
            }
            char *endp = nullptr;
            errno = 0;
            const long long ms = std::strtoll(args[++i].c_str(), &endp, 10);
            if (args[i].empty() || !endp || *endp || errno == ERANGE || ms < 0) {
                say("bad deadline " + args[i]);
                return 1;
            }
            deadline = start + std::min(ms, maxDeadlineMs);
        } else {
            say("unknown option " + args[i]);
            return 1;
        }
    }
    if (!paths.runDirExists)
        say("no runtime dir");
    if (skipStamped && paths.runDirExists && recentlyStamped(paths))
        return 0;
    const std::vector<FiioMatch> dacs = supportedDacs(paths);
    if (dacs.empty()) {
        if (stamp && paths.runDirExists)
            writeResetStamp(paths);
        return 0;
    }
    Session session = sessionCheck(paths, deadline);
    if (session.answer == Answer::Deadline)
        return deadlineExit();
    if (session.answer == Answer::Skip) {
        say("microdap session " + session.state + ", skipped");
        return 0;
    }
    bool allDone = true;
    bool failed = false;
    for (const FiioMatch &match : dacs) {
        const Wait wait = driverWait(match, noWait, deadline);
        if (wait == Wait::Deadline)
            return deadlineExit();
        if (waitSkipped(wait, match)) {
            allDone = false;
            continue;
        }
        if (!interfaceSupported(match.devPath, *match.model)) {
            say(deviceName(match) + " unsupported interface, skipped");
            allDone = false;
            continue;
        }
        FiioLock lock(paths);
        std::error_code ec;
        if (!lock.acquire(lockMs, deadline, {})) {
            if (expired(deadline))
                return deadlineExit();
            say(deviceName(match) + " failed lock");
            failed = true;
            continue;
        }
        session = sessionCheck(paths, deadline);
        if (session.answer == Answer::Deadline)
            return deadlineExit();
        if (session.answer == Answer::Skip) {
            say("microdap session " + session.state + ", skipped");
            return 0;
        }
        std::unique_ptr<UsbIo> io = factory();
        FiioDevice device(*io, *match.model, {}, paths);
        device.setLog(deviceLog(match));
        FiioReason reason = device.openClaimed(match.devPath, deadline);
        FiioResult result{reason, {}};
        const bool marker = fs::exists(fiioMarkerPath(paths), ec);
        if (reason == FiioReason::None) {
            FiioMirror mirror;
            result = FiioBatch::force(device, mirror, paths, {}, {}, deadline);
        }
        device.releaseClose();
        if (result.reason == FiioReason::Deadline)
            return deadlineExit();
        if (result.reason != FiioReason::None) {
            say(deviceName(match) + " failed " + std::string(reasonText(result.reason)));
            failed = true;
            continue;
        }
        const int preset = result.payload.empty() ? -1 : result.payload[0];
        if (preset == fiioBypass)
            say(deviceName(match) + " bypass");
        else
            say(deviceName(match) + " " + foundWord(preset, marker, *match.model) + ", set 240");
    }
    if (failed)
        return 1;
    if (stamp && allDone && paths.runDirExists)
        writeResetStamp(paths);
    return 0;
}

template<typename Batch>
int lockedVerb(UsbIoFactory &factory, const FiioPaths &paths, bool checkSession, bool single, Batch batch)
{
    if (!paths.runDirExists)
        say("no runtime dir");
    const std::vector<FiioMatch> dacs = supportedDacs(paths);
    if (dacs.empty())
        return 0;
    if (single && dacs.size() > 1) {
        say("more than one dac, refused");
        return 1;
    }
    if (checkSession) {
        const Session session = sessionCheck(paths, std::nullopt);
        if (session.answer != Answer::Proceed) {
            say("microdap session " + session.state + ", refused");
            return 1;
        }
    }
    bool failed = false;
    for (const FiioMatch &match : dacs) {
        if (waitSkipped(driverWait(match, false, std::nullopt), match)) {
            failed = true;
            continue;
        }
        if (!interfaceSupported(match.devPath, *match.model)) {
            say(deviceName(match) + " unsupported interface, skipped");
            failed = true;
            continue;
        }
        FiioLock lock(paths);
        if (!lock.acquire(lockMs, std::nullopt, {})) {
            say(deviceName(match) + " failed lock");
            failed = true;
            continue;
        }
        if (checkSession) {
            const Session session = sessionCheck(paths, std::nullopt);
            if (session.answer != Answer::Proceed) {
                say("microdap session " + session.state + ", refused");
                return 1;
            }
        }
        std::unique_ptr<UsbIo> io = factory();
        FiioDevice device(*io, *match.model, {}, paths);
        device.setLog(deviceLog(match));
        FiioReason reason = device.openClaimed(match.devPath, std::nullopt);
        if (reason == FiioReason::None)
            reason = batch(device, match);
        device.releaseClose();
        if (reason != FiioReason::None) {
            say(deviceName(match) + " failed " + std::string(reasonText(reason)));
            failed = true;
        }
    }
    return failed ? 1 : 0;
}

int dumpVerb(UsbIoFactory &factory, const FiioPaths &paths)
{
    return lockedVerb(factory, paths, false, false, [&paths](FiioDevice &device, const FiioMatch &match) {
        FiioMirror mirror;
        const FiioResult result = FiioBatch::dump(device, mirror, paths, {}, {}, std::nullopt);
        if (result.reason != FiioReason::None)
            return result.reason;
        std::cout << "device=" << match.devPath << '\n'
                  << "firmware=" << mirror.firmware << '\n'
                  << "preset=" << mirror.preset << '\n'
                  << "name=" << mirror.name << '\n'
                  << "preamp=" << hexText(mirror.preamp) << '\n';
        for (int i = 0; i < fiioBandCount; ++i)
            std::cout << "band" << i << '=' << hexText(mirror.bands[i]) << '\n';
        std::cout.flush();
        return FiioReason::None;
    });
}

int restoreVerb(const std::vector<std::string> &args, UsbIoFactory &factory, const FiioPaths &paths)
{
    if (args.size() > 1) {
        say("usage: restore-slot [<file>]");
        return 1;
    }
    const std::string file = args.empty() ? std::string() : args[0];
    return lockedVerb(factory, paths, true, true, [&paths, &file](FiioDevice &device, const FiioMatch &) {
        FiioMirror mirror;
        return FiioBatch::restoreSlot(device, mirror, paths, {}, {}, std::nullopt, file).reason;
    });
}

}

int runCli(int argc, char **argv, UsbIoFactory factory, const FiioPaths &paths)
{
    std::vector<std::string> args;
    for (int i = 2; i < argc; ++i)
        args.emplace_back(argv[i]);
    const std::string verb = argc > 1 ? argv[1] : "";
    if (verb == "reset")
        return resetVerb(args, factory, paths);
    if (verb == "dump" && args.empty())
        return dumpVerb(factory, paths);
    if (verb == "restore-slot")
        return restoreVerb(args, factory, paths);
    say("usage: reset [--no-wait] [--deadline <ms>] [--stamp] [--skip-stamped] | dump | restore-slot [<file>]");
    return 1;
}

}
