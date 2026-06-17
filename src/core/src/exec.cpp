#define _GNU_SOURCE // NOLINT -- enables execvpe()/environ via glibc <unistd.h>

#include <lightroot/exec.h>
#include <lightroot/unique_fd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#include <nlohmann/json.hpp>

namespace lightroot {
namespace {

/// exec `argv`; when `env` is non-empty the child runs with inherited `environ`
/// overlaid by the provided pairs (PATH-searched via execvpe in both cases).
[[noreturn]] void execArgv(const std::vector<std::string>& argv,
                           const std::vector<std::pair<std::string, std::string>>& env) {
    std::vector<char*> a;
    a.reserve(argv.size() + 1);
    for (auto& s : argv)
        a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);

    if (env.empty()) {
        ::execvp(a[0], a.data());
    } else {
        std::map<std::string, std::string> merged;
        for (char** e = ::environ; e && *e; ++e) {
            const std::string s = *e;
            const auto eq = s.find('=');
            if (eq != std::string::npos)
                merged.emplace(s.substr(0, eq), s.substr(eq + 1));
        }
        for (const auto& [k, v] : env)
            merged[k] = v;
        std::vector<std::string> owned;
        owned.reserve(merged.size());
        for (const auto& [k, v] : merged)
            owned.push_back(k + "=" + v);
        std::vector<char*> envp;
        envp.reserve(owned.size() + 1);
        for (auto& s : owned)
            envp.push_back(s.data());
        envp.push_back(nullptr);
        ::execvpe(a[0], a.data(), envp.data());
    }

    ::dprintf(STDERR_FILENO, "exec failed: %s: %s\n", a[0], std::strerror(errno));
    std::_Exit(127);
}

/// runCommand child setup: redirect stdout+stderr to `fd`, chdir, then exec.
[[noreturn]] void childExec(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                            int fd, const std::vector<std::pair<std::string, std::string>>& env) {
    if (!cwd.empty()) {
        if (::chdir(cwd.c_str()) != 0) {
            ::dprintf(fd, "chdir(%s) failed: %s\n", cwd.c_str(), std::strerror(errno));
            std::_Exit(127);
        }
    }
    ::dup2(fd, STDOUT_FILENO);
    ::dup2(fd, STDERR_FILENO);
    ::close(fd);
    execArgv(argv, env);
}

CommandResult reap(pid_t pid) {
    CommandResult r;
    r.launched = true;
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0) {
        if (errno == EINTR)
            continue;
        return r; // waitpid failure
    }
    if (WIFEXITED(status)) {
        r.exit_code = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        r.exit_code = 128 + WTERMSIG(status);
    }
    return r;
}

} // namespace

CommandResult runCommand(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         const std::filesystem::path& log_path,
                         const std::vector<std::pair<std::string, std::string>>& env) {
    if (argv.empty())
        return {};
    std::error_code ec;
    if (log_path.has_parent_path())
        std::filesystem::create_directories(log_path.parent_path(), ec);

    UniqueFd fd(::open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644));
    if (!fd)
        return {};

    const pid_t pid = ::fork();
    if (pid < 0)
        return {}; // fd closed by UniqueFd on every return path
    if (pid == 0)
        childExec(argv, cwd, fd.get(), env);

    return reap(pid); // fd closed by UniqueFd on return
}

CommandResult runCapture(const std::vector<std::string>& argv, const std::filesystem::path& cwd,
                         std::string& captured,
                         const std::vector<std::pair<std::string, std::string>>& env) {
    captured.clear();
    if (argv.empty())
        return {};

    // pipe[0] read, pipe[1] write; both ends closed by UniqueFd on every return.
    int pipefd[2];
    if (::pipe(pipefd) != 0)
        return {};
    UniqueFd readEnd(pipefd[0]);
    UniqueFd writeEnd(pipefd[1]);

    const pid_t pid = ::fork();
    if (pid < 0)
        return {}; // both ends closed by UniqueFd

    if (pid == 0) {
        ::close(pipefd[0]);
        ::dup2(pipefd[1], STDOUT_FILENO);
        ::dup2(pipefd[1], STDERR_FILENO);
        ::close(pipefd[1]);
        if (!cwd.empty()) {
            if (::chdir(cwd.c_str()) != 0)
                std::_Exit(127);
        }
        execArgv(argv, env);
    }

    // Parent: close the write end so the child sees EOF, then drain the read end.
    writeEnd.reset();

    std::string buf(4096, '\0');
    while (true) {
        const ssize_t n = ::read(readEnd.get(), buf.data(), buf.size());
        if (n > 0)
            captured.append(buf, 0, static_cast<std::size_t>(n));
        else if (n == 0)
            break;
        else if (errno != EINTR)
            break;
    }
    return reap(pid); // readEnd closed by UniqueFd on return
}

std::int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    std::string out;
    if (in) {
        in.seekg(0, std::ios::end);
        out.resize(static_cast<std::size_t>(in.tellg()));
        in.seekg(0, std::ios::beg);
        in.read(out.data(), static_cast<std::streamsize>(out.size()));
    }
    return out;
}

int cpuCount() {
    const long n = ::sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? static_cast<int>(n) : 1;
}

std::optional<std::filesystem::path> findOnPath(std::string_view name) {
    const char* path_env = std::getenv("PATH");
    if (path_env == nullptr)
        return std::nullopt;
    const std::string path(path_env);
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto colon = path.find(':', start);
        const std::string dir =
            path.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!dir.empty()) {
            std::error_code ec;
            const auto candidate = std::filesystem::path(dir) / std::string(name);
            if (std::filesystem::is_regular_file(candidate, ec) &&
                ::access(candidate.c_str(), X_OK) == 0)
                return candidate;
        }
        if (colon == std::string::npos)
            break;
        start = colon + 1;
    }
    return std::nullopt;
}

EventLog::EventLog(std::filesystem::path path) : path_(std::move(path)) {
    std::error_code ec;
    if (path_.has_parent_path())
        std::filesystem::create_directories(path_.parent_path(), ec);
    out_.open(path_, std::ios::out | std::ios::trunc);
}

void EventLog::event(const std::string& package, const std::string& phase,
                     const std::string& command, const std::string& status,
                     std::int64_t duration_ms, const std::string& log_path,
                     const std::string& message) {
    if (!out_.is_open())
        return;
    nlohmann::json j;
    j["ts"] = nowMs();
    j["package"] = package;
    j["phase"] = phase;
    if (!command.empty())
        j["command"] = command;
    j["status"] = status;
    j["duration_ms"] = duration_ms;
    if (!log_path.empty())
        j["log"] = log_path;
    if (!message.empty())
        j["message"] = message;
    out_ << j.dump() << "\n";
    out_.flush();
}

} // namespace lightroot
