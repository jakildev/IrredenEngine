#ifndef PROCESS_SPAWN_H
#define PROCESS_SPAWN_H

#include <expected>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace IREngine::detail {

struct ChildProcessState;

struct ChildProcess {
    std::unique_ptr<ChildProcessState> state_;
    std::string error_;

    ChildProcess();
    ~ChildProcess();
    ChildProcess(ChildProcess &&) noexcept;
    ChildProcess &operator=(ChildProcess &&) noexcept;
    ChildProcess(const ChildProcess &) = delete;
    ChildProcess &operator=(const ChildProcess &) = delete;

    bool started() const;
};

struct SpawnChildRequest {
    std::vector<std::string> argv_;
    std::filesystem::path cwd_;
    std::vector<std::pair<std::string, std::optional<std::string>>> environment_;
    std::optional<std::string> verbatimWindowsCommandLine_;
    bool pipeOutput_ = false;
    bool processTree_ = false;
};

std::expected<std::string, std::string> windowsCommandScriptLine(
    const std::filesystem::path &commandInterpreter,
    const std::filesystem::path &script,
    const std::filesystem::path &argument
);

ChildProcess spawnChild(const SpawnChildRequest &request);
std::string readChildOutput(
    ChildProcess &child, std::size_t maxBytes = std::numeric_limits<std::size_t>::max()
);
std::optional<int> pollChildExit(ChildProcess &child);
int waitChild(ChildProcess &child);
int killChild(ChildProcess &child);

} // namespace IREngine::detail

#endif /* PROCESS_SPAWN_H */
