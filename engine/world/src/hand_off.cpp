#include <irreden/world/hand_off.hpp>

#include "tool_target.hpp"

#include <irreden/ir_profile.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/utility/path_utils.hpp>
#include <irreden/utility/contained_path.hpp>

#include <sol/sol.hpp>

#include <string_view>
#include <fstream>
#include <filesystem>
#include <iterator>
#include <ranges>
#include <utility>

namespace IREngine {

namespace {

bool requestsProfile(const std::vector<std::string> &argv) {
    for (const std::string &arg : argv) {
        const std::string_view view{arg};
        if (view == "--auto-profile" || view.substr(0, 15) == "--auto-profile=") {
            return true;
        }
    }
    return false;
}

std::optional<std::string> readVerdict(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    const std::string text(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>()
    );
    const std::string key = "\"verdict\"";
    std::size_t position = text.find(key);
    if (position == std::string::npos) {
        return std::nullopt;
    }
    position = text.find(':', position + key.size());
    position = text.find('"', position == std::string::npos ? position : position + 1);
    if (position == std::string::npos) {
        return std::nullopt;
    }
    const std::size_t end = text.find('"', position + 1);
    if (end == std::string::npos) {
        return std::nullopt;
    }
    return text.substr(position + 1, end - position - 1);
}

std::vector<std::string> wrapperCommand(
    const HandOffEnvironment &environment,
    const std::string &tool,
    const std::vector<std::string> &args
) {
    std::vector<std::string> command;
    command.reserve(args.size() + 2);
    if (!environment.shellPath_.empty()) {
        command.push_back(environment.shellPath_);
    }
    command.push_back(tool);
    command.insert(command.end(), args.begin(), args.end());
    return command;
}

bool exitedZero(const ChildExit &exit) {
    return exit.started_ && exit.exitCode_ == 0;
}

std::string describeExit(const ChildExit &exit) {
    return exit.started_ ? "exit " + std::to_string(exit.exitCode_) : exit.error_;
}

constexpr double kMaxArgvIndex = 4294967295.0;

bool containsNul(const std::vector<std::string> &argv) {
    for (const std::string &arg : argv) {
        if (arg.find('\0') != std::string::npos) {
            return true;
        }
    }
    return false;
}

[[noreturn]] void raise(const std::string &message) {
    throw sol::error{"IREngine.handOff: " + message};
}

std::vector<std::string> parseArgv(const sol::object &argvObject) {
    if (argvObject.get_type() != sol::type::table) {
        raise("argv must be a non-empty array of strings");
    }
    const sol::table argvTable = argvObject.as<sol::table>();
    std::size_t count = 0;
    std::size_t maxIndex = 0;
    for (const auto &[key, value] : argvTable) {
        if (key.get_type() != sol::type::number) {
            raise("argv must be an array; it has a non-integer key");
        }
        const double index = key.as<double>();
        if (index < 1.0) {
            raise("argv must be an array; it has a non-integer key");
        }
        // Converting a double beyond std::size_t's range is undefined, so the
        // range check precedes the cast. No table reaches this bound, so a
        // key past it is necessarily a hole.
        if (index > kMaxArgvIndex) {
            raise("argv must be a dense array; it has holes");
        }
        const auto position = static_cast<std::size_t>(index);
        if (static_cast<double>(position) != index) {
            raise("argv must be an array; it has a non-integer key");
        }
        if (value.get_type() != sol::type::string) {
            raise("argv[" + std::to_string(position) + "] must be a string");
        }
        if (value.as<std::string_view>().find('\0') != std::string_view::npos) {
            raise("argv[" + std::to_string(position) + "] contains a NUL byte");
        }
        ++count;
        if (position > maxIndex) {
            maxIndex = position;
        }
    }
    if (count == 0) {
        raise("argv must be a non-empty array of strings");
    }
    if (maxIndex != count) {
        raise("argv must be a dense array; it has holes");
    }
    std::vector<std::string> argv;
    argv.reserve(count);
    for (std::size_t i = 1; i <= count; ++i) {
        argv.push_back(argvTable.get<std::string>(i));
    }
    return argv;
}

HandOffRequest parseRequest(const sol::object &arg) {
    if (arg.get_type() != sol::type::table) {
        raise("expects a table { argv = {...}, resume = true }");
    }
    const sol::table options = arg.as<sol::table>();
    for (const auto &[key, value] : options) {
        (void)value;
        if (key.get_type() != sol::type::string) {
            raise("request keys must be strings");
        }
        const std::string name = key.as<std::string>();
        if (name != "argv" && name != "resume" && name != "buildDir") {
            raise("unknown key '" + name + "'");
        }
    }
    HandOffRequest request;
    request.argv_ = parseArgv(options["argv"]);
    const sol::object buildDir = options["buildDir"];
    if (buildDir.valid() && buildDir.get_type() != sol::type::lua_nil) {
        if (buildDir.get_type() != sol::type::string) {
            raise("buildDir must be a string");
        }
        request.buildDir_ = buildDir.as<std::string>();
        if (request.buildDir_->find('\0') != std::string::npos) {
            raise("buildDir contains a NUL byte");
        }
    }
    const sol::object resume = options["resume"];
    if (resume.valid() && resume.get_type() != sol::type::lua_nil) {
        if (resume.get_type() != sol::type::boolean) {
            raise("resume must be a boolean");
        }
        if (!resume.as<bool>()) {
            raise("resume = false is not supported; the process always resumes");
        }
    }
    return request;
}

} // namespace

HandOffEnvironment HandOffEnvironment::fromProcess() {
    HandOffEnvironment environment;
    environment.wrapperPath_ = IRUtility::envString("IR_RUN_WRAPPER");
    environment.shellPath_ = IRUtility::envString("IR_RUN_BASH");
#ifdef _WIN32
    environment.shellRequired_ = true;
#endif
    environment.leaseToken_ = IRUtility::envString("IR_RUN_LOCK_TOKEN");
    environment.acquireToolPath_ = IRUtility::envString("IR_ACQUIRE_TOOL");
    environment.heldVerb_ = IRUtility::envString("IR_ACQUIRE_HELD_VERB");
    environment.inheritedBuildDir_ = IRUtility::envString("IRREDEN_BUILD_DIR");
    return environment;
}

HandOffOutcome runHandOff(
    const HandOffRequest &request,
    const HandOffEnvironment &environment,
    HandOffPlatform &platform,
    const std::optional<std::filesystem::path> &toolRoot
) {
    HandOffOutcome outcome;
    HandOffResult &result = outcome.result_;
    if (environment.wrapperPath_.empty() ||
        (environment.shellRequired_ && environment.shellPath_.empty())) {
        IRE_LOG_INFO(
            "IREngine.handOff: not launched through ir-run; ignoring the handoff (launch with "
            "fleet-run / ir-run to enable it)"
        );
        result.error_ = "not launched through ir-run";
        return outcome;
    }
    result.supported_ = true;
    if (request.argv_.empty()) {
        result.error_ = "argv is empty";
        return outcome;
    }
    if (!detail::isToolTargetName(request.argv_.front())) {
        result.error_ = "argv[0] is not a valid target name";
        return outcome;
    }
    // The OS receives each argument NUL-terminated, so an embedded NUL would
    // silently truncate what the child runs.
    if (containsNul(request.argv_)) {
        result.error_ = "argv contains a NUL byte";
        return outcome;
    }
    // A benchmark parent keeps its cpu and perf locks while handed off, so a
    // profiling child would queue on them until its queue timeout.
    if (environment.heldVerb_ == "benchmark" && requestsProfile(request.argv_)) {
        result.error_ = "a benchmark run cannot hand off to an --auto-profile child";
        return outcome;
    }
    const bool holdsLease = !environment.leaseToken_.empty();
    if (holdsLease && environment.acquireToolPath_.empty()) {
        result.error_ = "IR_RUN_LOCK_TOKEN is set without IR_ACQUIRE_TOOL";
        return outcome;
    }

    std::optional<std::filesystem::path> buildDir;
    if (request.buildDir_.has_value()) {
        if (!toolRoot.has_value()) {
            result.error_ = "buildDir requires a configured tool root";
            return outcome;
        }
        buildDir = IRUtility::resolveContainedPath(*toolRoot, *request.buildDir_);
        if (!buildDir.has_value()) {
            result.error_ = "buildDir must resolve inside the tool root";
            return outcome;
        }
    } else if (!environment.inheritedBuildDir_.empty()) {
        buildDir = std::filesystem::path{environment.inheritedBuildDir_};
    }
    std::optional<std::filesystem::path> resultFile;
    if (buildDir.has_value()) {
        resultFile = *buildDir / "ir-run-results" / (request.argv_.front() + ".last.json");
        std::error_code error;
        std::filesystem::create_directories(resultFile->parent_path(), error);
        if (error) {
            result.error_ = "could not create the result-file directory";
            return outcome;
        }
    }

    IRE_LOG_INFO("IREngine.handOff: suspending for {}", request.argv_.front());
    platform.drainGpu();
    outcome.suspended_ = true;
    if (holdsLease) {
        const ChildExit release = platform.runToExit(wrapperCommand(
            environment,
            environment.acquireToolPath_,
            {"--lease-release", environment.leaseToken_}
        ));
        if (!exitedZero(release)) {
            result.error_ = "could not release the gpu lease (" + describeExit(release) + ")";
            return outcome;
        }
        IRE_LOG_INFO("IREngine.handOff: released the gpu lease");
    }

    std::vector<std::string> runArgs;
    if (buildDir.has_value()) {
        runArgs.insert(runArgs.end(), {"--build-dir", buildDir->string()});
    }
    if (resultFile.has_value()) {
        runArgs.insert(runArgs.end(), {"--result-file", resultFile->string()});
    }
    runArgs.push_back("--");
    runArgs.insert(runArgs.end(), request.argv_.begin(), request.argv_.end());
    const ChildExit child =
        platform.runToExit(wrapperCommand(environment, environment.wrapperPath_, runArgs));
    if (child.started_) {
        result.exitCode_ = child.exitCode_;
        if (resultFile.has_value()) {
            result.verdict_ = readVerdict(*resultFile);
        }
    }

    if (holdsLease) {
        const ChildExit reacquire = platform.runToExit(wrapperCommand(
            environment,
            environment.acquireToolPath_,
            {"--lease-reacquire", environment.leaseToken_}
        ));
        if (!exitedZero(reacquire)) {
            outcome.leaseLost_ = true;
            result.error_ = "could not reacquire the gpu lease (" + describeExit(reacquire) + ")";
            IRE_LOG_ERROR("IREngine.handOff: {}; closing without rendering again", result.error_);
            return outcome;
        }
        IRE_LOG_INFO("IREngine.handOff: reacquired the gpu lease");
    }

    if (!child.started_) {
        result.error_ = "the child did not start: " + child.error_;
    } else if (child.exitCode_ != 0) {
        result.error_ = "the child exited with " + std::to_string(child.exitCode_);
    } else {
        result.ok_ = true;
    }
    IRE_LOG_INFO("IREngine.handOff: resumed ({})", result.ok_ ? "ok" : result.error_);
    return outcome;
}

void bindHandOffLua(
    IRScript::LuaScript &script, HandOffInvoker invoker, std::function<bool()> isMainThread
) {
    sol::state &lua = script.lua();
    sol::table engine = lua["IREngine"].valid() ? lua["IREngine"].get<sol::table>()
                                                : lua.create_named_table("IREngine");
    engine["handOff"] = IRScript::detail::statefulLuaFunction(
        [invoker = std::move(invoker),
         isMainThread = std::move(isMainThread)](sol::object arg, sol::this_state state) {
            if (!isMainThread()) {
                raise("must be called from the main thread");
            }
            const HandOffResult result = invoker(parseRequest(arg));
            sol::state_view view{state};
            sol::table out = view.create_table();
            out["ok"] = result.ok_;
            out["supported"] = result.supported_;
            if (result.exitCode_.has_value()) {
                out["exitCode"] = *result.exitCode_;
            }
            if (result.verdict_.has_value()) {
                out["verdict"] = *result.verdict_;
            }
            if (!result.error_.empty()) {
                out["error"] = result.error_;
            }
            return out;
        }
    );
}

namespace detail {

std::string windowsCommandLine(const std::vector<std::string> &argv) {
    std::string line;
    for (const std::string &arg : argv) {
        if (!line.empty()) {
            line += ' ';
        }
        if (!arg.empty() && arg.find_first_of(" \t\n\v\"") == std::string::npos) {
            line += arg;
            continue;
        }
        line += '"';
        std::size_t backslashes = 0;
        for (const char c : arg) {
            if (c == '\\') {
                ++backslashes;
                continue;
            }
            // Backslashes escape only a following quote: double them there,
            // and escape the quote itself.
            line.append(c == '"' ? backslashes * 2 + 1 : backslashes, '\\');
            line += c;
            backslashes = 0;
        }
        // ...and before the closing quote.
        line.append(backslashes * 2, '\\');
        line += '"';
    }
    return line;
}

} // namespace detail

} // namespace IREngine
