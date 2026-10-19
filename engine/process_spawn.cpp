#if defined(__linux__) && !defined(_GNU_SOURCE)
#define _GNU_SOURCE
#endif

#include "process_spawn.hpp"

#include <irreden/ir_math.hpp>
#include <irreden/world/hand_off.hpp>

#include <algorithm>
#include <chrono>
#include <cstring>
#include <map>
#include <thread>
#include <unordered_map>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;
#endif

namespace IREngine::detail {

struct ChildProcessState {
#ifdef _WIN32
    HANDLE process_ = nullptr;
    HANDLE thread_ = nullptr;
    HANDLE output_ = nullptr;
    HANDLE job_ = nullptr;
#else
    pid_t pid_ = -1;
    int output_ = -1;
#endif
    std::optional<int> exitCode_;

    ~ChildProcessState() {
#ifdef _WIN32
        if (output_ != nullptr) {
            CloseHandle(output_);
        }
        if (thread_ != nullptr) {
            CloseHandle(thread_);
        }
        if (process_ != nullptr) {
            CloseHandle(process_);
        }
        if (job_ != nullptr) {
            CloseHandle(job_);
        }
#else
        if (output_ >= 0) {
            close(output_);
        }
#endif
    }
};

ChildProcess::ChildProcess() = default;
ChildProcess::~ChildProcess() = default;
ChildProcess::ChildProcess(ChildProcess &&) noexcept = default;
ChildProcess &ChildProcess::operator=(ChildProcess &&) noexcept = default;

bool ChildProcess::started() const {
    return state_ != nullptr;
}

namespace {

#ifdef _WIN32

std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int size =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), wide.data(), size);
    return wide;
}

std::vector<wchar_t>
environmentBlock(const std::vector<std::pair<std::string, std::optional<std::string>>> &changes) {
    std::map<std::wstring, std::wstring> values;
    LPWCH current = GetEnvironmentStringsW();
    if (current != nullptr) {
        for (const wchar_t *entry = current; *entry != L'\0'; entry += std::wcslen(entry) + 1) {
            const std::wstring value{entry};
            const std::size_t equals = value.find(L'=', value[0] == L'=' ? 1 : 0);
            if (equals != std::wstring::npos) {
                values[value.substr(0, equals)] = value.substr(equals + 1);
            }
        }
        FreeEnvironmentStringsW(current);
    }
    for (const auto &[name, value] : changes) {
        const std::wstring wideName = widen(name);
        if (value.has_value()) {
            values[wideName] = widen(*value);
        } else {
            values.erase(wideName);
        }
    }
    std::vector<wchar_t> block;
    for (const auto &[name, value] : values) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
}

void closeHandle(HANDLE &handle) {
    if (handle != nullptr) {
        CloseHandle(handle);
        handle = nullptr;
    }
}

#else

std::vector<std::string>
environmentValues(const std::vector<std::pair<std::string, std::optional<std::string>>> &changes) {
    std::unordered_map<std::string, std::string> values;
    for (char **entry = environ; entry != nullptr && *entry != nullptr; ++entry) {
        const std::string value{*entry};
        const std::size_t equals = value.find('=');
        if (equals != std::string::npos) {
            values[value.substr(0, equals)] = value.substr(equals + 1);
        }
    }
    for (const auto &[name, value] : changes) {
        if (value.has_value()) {
            values[name] = *value;
        } else {
            values.erase(name);
        }
    }
    std::vector<std::string> environment;
    environment.reserve(values.size());
    for (const auto &[name, value] : values) {
        environment.push_back(name + "=" + value);
    }
    return environment;
}

int decodeStatus(int status) {
    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }
    if (WIFSIGNALED(status)) {
        return 128 + WTERMSIG(status);
    }
    return -1;
}

#endif

} // namespace

std::expected<std::string, std::string> windowsCommandScriptLine(
    const std::filesystem::path &commandInterpreter,
    const std::filesystem::path &script,
    const std::filesystem::path &argument
) {
    if (argument.string().find('%') != std::string::npos) {
        return std::unexpected("path contains '%' and cannot be passed safely to cmd.exe");
    }
    return windowsCommandLine({commandInterpreter.string()}) + " /d /s /c \"\"" + script.string() +
           "\" \"" + argument.string() + "\"\"";
}

ChildProcess spawnChild(const SpawnChildRequest &request) {
    ChildProcess child;
    if (request.argv_.empty()) {
        child.error_ = "child argv is empty";
        return child;
    }

#ifdef _WIN32
    DisableProcessWindowsGhosting();
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    HANDLE outputRead = nullptr;
    HANDLE outputWrite = nullptr;
    if (request.pipeOutput_) {
        if (!CreatePipe(&outputRead, &outputWrite, &security, 0) ||
            !SetHandleInformation(outputRead, HANDLE_FLAG_INHERIT, 0)) {
            child.error_ = "CreatePipe failed with error " + std::to_string(GetLastError());
            closeHandle(outputRead);
            closeHandle(outputWrite);
            return child;
        }
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = request.pipeOutput_ ? outputWrite : GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = request.pipeOutput_ ? outputWrite : GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring command =
        widen(request.verbatimWindowsCommandLine_.value_or(windowsCommandLine(request.argv_)));
    std::wstring cwd = request.cwd_.empty() ? std::wstring{} : request.cwd_.wstring();
    std::vector<wchar_t> environment = environmentBlock(request.environment_);
    const DWORD flags = CREATE_UNICODE_ENVIRONMENT |
                        (request.processTree_ ? CREATE_SUSPENDED | CREATE_NEW_PROCESS_GROUP : 0);
    if (!CreateProcessW(
            nullptr,
            command.data(),
            nullptr,
            nullptr,
            TRUE,
            flags,
            environment.data(),
            cwd.empty() ? nullptr : cwd.c_str(),
            &startup,
            &process
        )) {
        child.error_ = "CreateProcessW failed with error " + std::to_string(GetLastError());
        closeHandle(outputRead);
        closeHandle(outputWrite);
        return child;
    }
    closeHandle(outputWrite);

    auto state = std::make_unique<ChildProcessState>();
    state->process_ = process.hProcess;
    state->thread_ = process.hThread;
    state->output_ = outputRead;
    if (request.processTree_) {
        state->job_ = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (state->job_ == nullptr ||
            !SetInformationJobObject(
                state->job_,
                JobObjectExtendedLimitInformation,
                &limits,
                sizeof(limits)
            ) ||
            !AssignProcessToJobObject(state->job_, state->process_)) {
            TerminateProcess(state->process_, 1);
            WaitForSingleObject(state->process_, INFINITE);
            child.error_ = "could not assign child to a job object";
            closeHandle(state->job_);
            closeHandle(state->output_);
            closeHandle(state->thread_);
            closeHandle(state->process_);
            return child;
        }
        ResumeThread(state->thread_);
    }
    closeHandle(state->thread_);
    child.state_ = std::move(state);
#else
    int outputPipe[2] = {-1, -1};
    if (request.pipeOutput_ && pipe(outputPipe) != 0) {
        child.error_ = "pipe failed: " + std::string{std::strerror(errno)};
        return child;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    if (request.pipeOutput_) {
        posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDERR_FILENO);
        posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
        posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
    }
    if (!request.cwd_.empty()) {
#if defined(__APPLE__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        posix_spawn_file_actions_addchdir_np(&actions, request.cwd_.c_str());
#if defined(__APPLE__)
#pragma clang diagnostic pop
#endif
    }

    posix_spawnattr_t attributes;
    posix_spawnattr_init(&attributes);
    if (request.processTree_) {
        posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        posix_spawnattr_setpgroup(&attributes, 0);
    }
    std::vector<char *> argv;
    argv.reserve(request.argv_.size() + 1);
    for (const std::string &arg : request.argv_) {
        argv.push_back(const_cast<char *>(arg.c_str()));
    }
    argv.push_back(nullptr);
    std::vector<std::string> environment = environmentValues(request.environment_);
    std::vector<char *> envp;
    envp.reserve(environment.size() + 1);
    for (std::string &value : environment) {
        envp.push_back(value.data());
    }
    envp.push_back(nullptr);

    pid_t pid = -1;
    const int error =
        posix_spawnp(&pid, argv.front(), &actions, &attributes, argv.data(), envp.data());
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    if (request.pipeOutput_) {
        close(outputPipe[1]);
    }
    if (error != 0) {
        if (request.pipeOutput_) {
            close(outputPipe[0]);
        }
        child.error_ = "posix_spawn failed: " + std::string{std::strerror(error)};
        return child;
    }
    auto state = std::make_unique<ChildProcessState>();
    state->pid_ = pid;
    state->output_ = request.pipeOutput_ ? outputPipe[0] : -1;
    if (state->output_ >= 0) {
        const int flags = fcntl(state->output_, F_GETFL, 0);
        fcntl(state->output_, F_SETFL, flags | O_NONBLOCK);
    }
    child.state_ = std::move(state);
#endif
    return child;
}

std::string readChildOutput(ChildProcess &child, std::size_t maxBytes) {
    std::string output;
    if (!child.started()) {
        return output;
    }
#ifdef _WIN32
    if (child.state_->output_ == nullptr) {
        return output;
    }
    while (output.size() < maxBytes) {
        DWORD available = 0;
        if (!PeekNamedPipe(child.state_->output_, nullptr, 0, nullptr, &available, nullptr) ||
            available == 0) {
            break;
        }
        char buffer[4096];
        DWORD read = 0;
        const std::size_t remaining = maxBytes - output.size();
        const std::size_t requestedBytes = IRMath::min(
            static_cast<std::size_t>(available),
            IRMath::min(sizeof(buffer), remaining)
        );
        const DWORD requested = static_cast<DWORD>(requestedBytes);
        if (!ReadFile(child.state_->output_, buffer, requested, &read, nullptr) || read == 0) {
            break;
        }
        output.append(buffer, read);
    }
#else
    if (child.state_->output_ < 0) {
        return output;
    }
    while (output.size() < maxBytes) {
        char buffer[4096];
        const std::size_t remaining = maxBytes - output.size();
        const ssize_t count =
            read(child.state_->output_, buffer, IRMath::min(sizeof(buffer), remaining));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
#endif
    return output;
}

std::optional<int> pollChildExit(ChildProcess &child) {
    if (!child.started()) {
        return std::nullopt;
    }
    if (child.state_->exitCode_.has_value()) {
        return child.state_->exitCode_;
    }
#ifdef _WIN32
    if (WaitForSingleObject(child.state_->process_, 0) != WAIT_OBJECT_0) {
        return std::nullopt;
    }
    DWORD code = 0;
    GetExitCodeProcess(child.state_->process_, &code);
    child.state_->exitCode_ = static_cast<int>(code);
#else
    int status = 0;
    const pid_t result = waitpid(child.state_->pid_, &status, WNOHANG);
    if (result == 0) {
        return std::nullopt;
    }
    if (result < 0) {
        if (errno == EINTR) {
            return std::nullopt;
        }
        child.state_->exitCode_ = -1;
    } else {
        child.state_->exitCode_ = decodeStatus(status);
    }
#endif
    return child.state_->exitCode_;
}

int waitChild(ChildProcess &child) {
    if (!child.started()) {
        return -1;
    }
    if (child.state_->exitCode_.has_value()) {
        return *child.state_->exitCode_;
    }
#ifdef _WIN32
    WaitForSingleObject(child.state_->process_, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(child.state_->process_, &code);
    child.state_->exitCode_ = static_cast<int>(code);
#else
    int status = 0;
    while (waitpid(child.state_->pid_, &status, 0) < 0) {
        if (errno != EINTR) {
            child.state_->exitCode_ = -1;
            return -1;
        }
    }
    child.state_->exitCode_ = decodeStatus(status);
#endif
    return *child.state_->exitCode_;
}

int killChild(ChildProcess &child) {
    if (!child.started()) {
        return -1;
    }
    if (const std::optional<int> code = pollChildExit(child); code.has_value()) {
        return *code;
    }
#ifdef _WIN32
    if (child.state_->job_ != nullptr) {
        TerminateJobObject(child.state_->job_, 1);
    } else {
        TerminateProcess(child.state_->process_, 1);
    }
#else
    kill(-child.state_->pid_, SIGTERM);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < deadline) {
        if (const std::optional<int> code = pollChildExit(child); code.has_value()) {
            return *code;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    kill(-child.state_->pid_, SIGKILL);
#endif
    return waitChild(child);
}

} // namespace IREngine::detail
