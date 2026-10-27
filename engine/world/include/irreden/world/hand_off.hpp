#ifndef IR_WORLD_HAND_OFF_H
#define IR_WORLD_HAND_OFF_H

// Hand the running process off to a child run and resume when it exits
// (`IREngine.handOff`, engine/script/CLAUDE.md). The sequence is fixed:
// validate, drain the GPU, optionally release MIDI, lend the ir-run gpu
// lease, launch and wait for the child, reacquire the lease, restore MIDI,
// resume. Drain precedes release on every path, so the
// child never links shaders against a parent with GPU work in flight.

#include <functional>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace IRScript {
class LuaScript;
}

namespace IREngine {

class ToolService;

struct HandOffRequest {
    /// The child's ir-run arguments (executable name first), passed as
    /// separate arguments, never joined into a shell command. The OS takes
    /// each NUL-terminated, so `runHandOff` refuses one holding a NUL byte.
    std::vector<std::string> argv_;
    bool releaseMidi_ = false;
};

struct HandOffResult {
    bool ok_ = false;
    /// False when this process was not launched by ir-run: nothing happened.
    bool supported_ = false;
    /// Set once the child started.
    std::optional<int> exitCode_;
    std::string error_;
    std::optional<std::string> verdict_;
    std::vector<std::string> midiLost_;
};

class HandOffMidiBracket {
  public:
    virtual ~HandOffMidiBracket() = default;
    virtual void release() = 0;
    virtual std::vector<std::string> restore() = 0;
};

/// What ir-run and ir-acquire exported to this process.
struct HandOffEnvironment {
    std::string wrapperPath_;
    /// The bash that runs the wrapper; native Windows cannot execute it.
    std::string shellPath_;
    bool shellRequired_ = false;
    std::string leaseToken_;
    std::string acquireToolPath_;
    std::string heldVerb_;

    static HandOffEnvironment fromProcess();
};

struct ChildExit {
    bool started_ = false;
    int exitCode_ = -1;
    std::string error_;
};

/// The process-facing half, injected so tests drive the sequence with stubs.
class HandOffPlatform {
  public:
    virtual ~HandOffPlatform() = default;
    /// Returns once every queued GPU command has completed.
    virtual void drainGpu() = 0;
    /// Runs @p argv to exit, inheriting stdio, sleeping in the OS wait.
    virtual ChildExit runToExit(const std::vector<std::string> &argv) = 0;
};

struct HandOffOutcome {
    HandOffResult result_;
    /// The GPU was drained: the caller re-baselines its clock.
    bool suspended_ = false;
    /// The lease was released and could not be reacquired: the caller must
    /// not run another update or render.
    bool leaseLost_ = false;
};

HandOffOutcome runHandOff(
    const HandOffRequest &request,
    const HandOffEnvironment &environment,
    HandOffPlatform &platform,
    const std::optional<std::filesystem::path> &toolRoot = std::nullopt,
    HandOffMidiBracket *midiBracket = nullptr
);

/// Runs from @p toolService's active root, re-resolved now; a worktree that is
/// gone refuses before the drain and launches nothing.
HandOffOutcome runHandOff(
    const HandOffRequest &request,
    const HandOffEnvironment &environment,
    HandOffPlatform &platform,
    ToolService &toolService,
    HandOffMidiBracket *midiBracket = nullptr
);

using HandOffInvoker = std::function<HandOffResult(const HandOffRequest &)>;

/// Installs
/// `IREngine.handOff{ argv = {...}, resume = true, releaseMidi = false }`.
/// Malformed input, `resume = false`, and a call off the main thread raise
/// before @p invoker runs.
void bindHandOffLua(
    IRScript::LuaScript &script, HandOffInvoker invoker, std::function<bool()> isMainThread
);

namespace detail {

/// The CreateProcessW command line for @p argv, quoted so CommandLineToArgvW
/// recovers every argument exactly.
std::string windowsCommandLine(const std::vector<std::string> &argv);

} // namespace detail

} // namespace IREngine

#endif /* IR_WORLD_HAND_OFF_H */
