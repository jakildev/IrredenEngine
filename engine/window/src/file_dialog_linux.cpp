// Linux half of file_dialog.hpp (macOS: file_dialog_cocoa.mm, Windows:
// file_dialog_win32.cpp). GLFW links no toolkit here, so the panel is the
// desktop's `zenity` when one is installed and a display is reachable.
#include <irreden/window/file_dialog.hpp>

#include <irreden/utility/path_utils.hpp>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

namespace IRWindow {

namespace {

// zenity's exit codes: 0 accepted, 1 cancelled or closed.
constexpr int kZenityAccepted = 0;
constexpr int kZenityCancelled = 1;

// '\n' then 32 hex digits from the OS entropy source: a selected path holds it
// only by guessing 128 random bits before the panel opens.
std::string multipleSelectionSeparator() {
    constexpr std::string_view kHexDigits = "0123456789abcdef";
    std::random_device entropy;
    std::string separator{"\n"};
    for (int word = 0; word < 4; ++word) {
        std::uint32_t bits = entropy();
        for (int nibble = 0; nibble < 8; ++nibble) {
            separator += kHexDigits[bits & 0xFu];
            bits >>= 4;
        }
    }
    return separator;
}

FileDialogResult outcome(FileDialogStatus status, std::string error) {
    FileDialogResult result;
    result.status_ = status;
    result.error_ = std::move(error);
    return result;
}

} // namespace

FileDialogResult platformOpenFileDialog(GLFWwindow *, const FileDialogRequest &request) {
    if (IRUtility::envString("DISPLAY").empty() &&
        IRUtility::envString("WAYLAND_DISPLAY").empty()) {
        return outcome(FileDialogStatus::UNSUPPORTED, "no display to show a file dialog on");
    }
    const std::optional<std::filesystem::path> zenity = IRUtility::findOnPath("zenity", {""});
    if (!zenity.has_value()) {
        return outcome(
            FileDialogStatus::UNSUPPORTED,
            "no file dialog on this host: install zenity"
        );
    }

    const std::string separator = multipleSelectionSeparator();
    std::vector<std::string> arguments = detail::zenityArguments(request, separator);
    arguments.insert(arguments.begin(), zenity->string());
    std::vector<char *> argv;
    argv.reserve(arguments.size() + 1);
    for (std::string &argument : arguments) {
        argv.push_back(argument.data());
    }
    argv.push_back(nullptr);

    int outputPipe[2];
    if (pipe(outputPipe) != 0) {
        return outcome(
            FileDialogStatus::FAILED,
            std::string{"pipe failed: "} + std::strerror(errno)
        );
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, outputPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_addclose(&actions, outputPipe[0]);
    posix_spawn_file_actions_addclose(&actions, outputPipe[1]);
    pid_t pid = 0;
    const int spawnError = posix_spawn(&pid, argv.front(), &actions, nullptr, argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    close(outputPipe[1]);
    if (spawnError != 0) {
        close(outputPipe[0]);
        return outcome(
            FileDialogStatus::FAILED,
            std::string{"could not start zenity: "} + std::strerror(spawnError)
        );
    }

    std::string output;
    char buffer[4096];
    for (;;) {
        const ssize_t count = read(outputPipe[0], buffer, sizeof(buffer));
        if (count > 0) {
            output.append(buffer, static_cast<std::size_t>(count));
            continue;
        }
        if (count < 0 && errno == EINTR) {
            continue;
        }
        break;
    }
    close(outputPipe[0]);

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return outcome(
                FileDialogStatus::FAILED,
                std::string{"waitpid failed: "} + std::strerror(errno)
            );
        }
    }
    if (!WIFEXITED(status)) {
        return outcome(FileDialogStatus::FAILED, "zenity did not exit normally");
    }
    if (WEXITSTATUS(status) == kZenityCancelled) {
        return outcome(FileDialogStatus::CANCELLED, {});
    }
    if (WEXITSTATUS(status) != kZenityAccepted) {
        return outcome(
            FileDialogStatus::FAILED,
            "zenity exited with " + std::to_string(WEXITSTATUS(status))
        );
    }
    FileDialogResult result;
    result.paths_ = detail::parseZenityOutput(output, request.mode_, separator);
    result.status_ = FileDialogStatus::ACCEPTED;
    return result;
}

} // namespace IRWindow
