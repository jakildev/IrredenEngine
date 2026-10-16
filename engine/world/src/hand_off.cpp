#include <irreden/world/hand_off.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/utility/path_utils.hpp>

#include <sol/sol.hpp>

#include <string_view>
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
        if (index < 1.0 || index != static_cast<double>(static_cast<std::size_t>(index))) {
            raise("argv must be an array; it has a non-integer key");
        }
        if (value.get_type() != sol::type::string) {
            raise("argv[" + std::to_string(static_cast<std::size_t>(index)) + "] must be a string");
        }
        ++count;
        if (static_cast<std::size_t>(index) > maxIndex) {
            maxIndex = static_cast<std::size_t>(index);
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
    HandOffRequest request;
    request.argv_ = parseArgv(options["argv"]);
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
    return environment;
}

HandOffOutcome runHandOff(
    const HandOffRequest &request, const HandOffEnvironment &environment, HandOffPlatform &platform
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

    const ChildExit child =
        platform.runToExit(wrapperCommand(environment, environment.wrapperPath_, request.argv_));
    if (child.started_) {
        result.exitCode_ = child.exitCode_;
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
