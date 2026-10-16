#include <irreden/ir_engine.hpp>

#include <irreden/render/render_device.hpp>

#include <string>
#include <vector>

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
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>

extern char **environ;
#endif

namespace IREngine::detail {

namespace {

#ifdef _WIN32

std::wstring widen(const std::string &utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int length =
        MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8.data(),
        static_cast<int>(utf8.size()),
        wide.data(),
        length
    );
    return wide;
}

ChildExit runChildToExit(const std::vector<std::string> &argv) {
    ChildExit exit;
    // Without this, DWM swaps the window that stops pumping messages for 5 s
    // for a "Not Responding" ghost, and closing that ghost ends the process.
    // It is process-wide for the rest of the run.
    DisableProcessWindowsGhosting();
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    startup.hStdError = GetStdHandle(STD_ERROR_HANDLE);
    PROCESS_INFORMATION process{};
    std::wstring commandLine = widen(windowsCommandLine(argv));
    if (!CreateProcessW(
            nullptr,
            commandLine.data(),
            nullptr,
            nullptr,
            TRUE,
            0,
            nullptr,
            nullptr,
            &startup,
            &process
        )) {
        exit.error_ = "CreateProcessW failed with error " + std::to_string(GetLastError());
        return exit;
    }
    exit.started_ = true;
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD code = 0;
    GetExitCodeProcess(process.hProcess, &code);
    exit.exitCode_ = static_cast<int>(code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return exit;
}

#else

ChildExit runChildToExit(const std::vector<std::string> &argv) {
    ChildExit exit;
    std::vector<char *> args;
    args.reserve(argv.size() + 1);
    for (const std::string &arg : argv) {
        args.push_back(const_cast<char *>(arg.c_str()));
    }
    args.push_back(nullptr);
    pid_t pid = 0;
    const int spawnError = posix_spawn(&pid, args.front(), nullptr, nullptr, args.data(), environ);
    if (spawnError != 0) {
        exit.error_ = "posix_spawn failed: " + std::string{std::strerror(spawnError)};
        return exit;
    }
    exit.started_ = true;
    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            exit.error_ = "waitpid failed: " + std::string{std::strerror(errno)};
            return exit;
        }
    }
    if (WIFEXITED(status)) {
        exit.exitCode_ = WEXITSTATUS(status);
    } else if (WIFSIGNALED(status)) {
        exit.exitCode_ = 128 + WTERMSIG(status);
    }
    return exit;
}

#endif

class ProcessHandOffPlatform final : public HandOffPlatform {
  public:
    void drainGpu() override {
        IRRender::device()->finish();
    }

    ChildExit runToExit(const std::vector<std::string> &argv) override {
        return runChildToExit(argv);
    }
};

} // namespace

std::unique_ptr<HandOffPlatform> makeProcessHandOffPlatform() {
    return std::make_unique<ProcessHandOffPlatform>();
}

} // namespace IREngine::detail
