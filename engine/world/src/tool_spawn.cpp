#include <irreden/world/tool_spawn.hpp>

#include "../../process_spawn.hpp"

#include <irreden/asset/creation_manifest.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/utility/path_utils.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <cctype>
#include <deque>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace IREngine {

namespace {

constexpr std::size_t kMaxBufferedLines = 2048;
constexpr std::size_t kMaxBufferedLineBytes = 64 * 1024;
constexpr std::size_t kMaxDrainBytesPerTick = 64 * 1024;

std::optional<std::filesystem::path> fixedTool(std::string_view name) {
    const std::string wrapper = IRUtility::envString("IR_RUN_WRAPPER");
    if (!wrapper.empty()) {
        std::error_code error;
        const std::filesystem::path beside = std::filesystem::weakly_canonical(
            std::filesystem::path{wrapper}.parent_path() / name,
            error
        );
        if (!error && std::filesystem::is_regular_file(beside, error) && !error) {
            return beside;
        }
    }
    return IRUtility::findOnPath(name, {""});
}

#ifdef _WIN32
bool isCommandScript(const std::filesystem::path &path) {
    std::string extension = path.extension().string();
    std::ranges::transform(extension, extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return extension == ".cmd" || extension == ".bat";
}

#endif

[[noreturn]] void raise(const std::string &message) {
    throw sol::error{"IREngine.spawnTool: " + message};
}

void requireOnlyKeys(const sol::table &table, std::initializer_list<std::string_view> allowed) {
    for (const auto &[key, value] : table) {
        (void)value;
        if (key.get_type() != sol::type::string) {
            raise("request keys must be strings");
        }
        const std::string_view name = key.as<std::string_view>();
        if (std::ranges::find(allowed, name) == allowed.end()) {
            raise("unknown key '" + std::string{name} + "'");
        }
    }
}

std::string requiredString(const sol::table &table, const char *name) {
    const sol::object value = table[name];
    if (!value.valid() || value.get_type() != sol::type::string) {
        raise(std::string{name} + " must be a string");
    }
    const std::string text = value.as<std::string>();
    if (text.find('\0') != std::string::npos) {
        raise(std::string{name} + " contains a NUL byte");
    }
    return text;
}

} // namespace

struct ToolService::Impl {
    std::optional<std::filesystem::path> root_;
    detail::ChildProcess child_;
    std::deque<std::string> lines_;
    std::string partial_;
    bool droppingPartial_ = false;
    std::optional<int> exitCode_;
    std::uint64_t generation_ = 0;
    std::uint64_t dropped_ = 0;

    void requireHandle(std::uint64_t generation) const {
        if (generation != generation_) {
            throw std::runtime_error("stale tool handle");
        }
    }

    void pushLine(std::string line) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (lines_.size() == kMaxBufferedLines) {
            lines_.pop_front();
            ++dropped_;
        }
        lines_.push_back(std::move(line));
    }

    void consume(std::string_view output) {
        std::size_t begin = 0;
        while (begin < output.size()) {
            const std::size_t newline = output.find('\n', begin);
            const std::size_t end = newline == std::string_view::npos ? output.size() : newline;
            if (!droppingPartial_) {
                const std::size_t available = kMaxBufferedLineBytes - partial_.size();
                const std::size_t length = end - begin;
                if (length <= available) {
                    partial_.append(output.substr(begin, length));
                } else {
                    partial_.clear();
                    droppingPartial_ = true;
                    ++dropped_;
                }
            }
            if (newline == std::string_view::npos) {
                return;
            }
            if (droppingPartial_) {
                droppingPartial_ = false;
            } else {
                pushLine(std::move(partial_));
                partial_.clear();
            }
            begin = newline + 1;
        }
    }

    void drain() {
        const std::string output = detail::readChildOutput(child_, kMaxDrainBytesPerTick);
        consume(output);
        if (!exitCode_.has_value()) {
            exitCode_ = detail::pollChildExit(child_);
        }
        if (exitCode_.has_value() && output.size() < kMaxDrainBytesPerTick) {
            if (!droppingPartial_ && !partial_.empty()) {
                pushLine(std::move(partial_));
            }
            partial_.clear();
            droppingPartial_ = false;
        }
    }
};

ToolHandle::ToolHandle(ToolService *service, std::uint64_t generation)
    : m_service(service)
    , m_generation(generation) {}

std::vector<std::string> ToolHandle::poll() {
    m_service->m_impl->requireHandle(m_generation);
    m_service->m_impl->drain();
    std::vector<std::string> lines;
    lines.reserve(m_service->m_impl->lines_.size());
    while (!m_service->m_impl->lines_.empty()) {
        lines.push_back(std::move(m_service->m_impl->lines_.front()));
        m_service->m_impl->lines_.pop_front();
    }
    return lines;
}

std::optional<int> ToolHandle::exitCode() {
    m_service->m_impl->requireHandle(m_generation);
    m_service->m_impl->drain();
    return m_service->m_impl->exitCode_;
}

void ToolHandle::kill() {
    m_service->m_impl->requireHandle(m_generation);
    if (!m_service->m_impl->exitCode_.has_value()) {
        m_service->m_impl->exitCode_ = detail::killChild(m_service->m_impl->child_);
        m_service->m_impl->drain();
    }
}

std::uint64_t ToolHandle::droppedLines() const {
    m_service->m_impl->requireHandle(m_generation);
    return m_service->m_impl->dropped_;
}

ToolService::ToolService()
    : m_impl(std::make_unique<Impl>()) {}

ToolService::ToolService(const std::filesystem::path &root)
    : ToolService() {
    setRoot(root);
}

ToolService::~ToolService() {
    shutdown();
}

void ToolService::setRoot(const std::filesystem::path &root) {
    if (m_impl->root_.has_value()) {
        throw std::invalid_argument("tool root is already set");
    }
    std::error_code error;
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical, error) || error) {
        throw std::invalid_argument("tool root must be an existing directory");
    }
    m_impl->root_ = canonical;
}

std::optional<std::filesystem::path> ToolService::root() const {
    return m_impl->root_;
}

std::expected<ToolHandle, std::string> ToolService::spawn(const ToolSpawnRequest &request) {
    if (!m_impl->root_.has_value()) {
        return std::unexpected("tool root is not set");
    }
    if (request.tool_ != ToolName::IR_BUILD && request.tool_ != ToolName::EDITOR &&
        request.tool_ != ToolName::OPEN_FOLDER) {
        return std::unexpected("unknown tool");
    }
    m_impl->drain();
    if (m_impl->child_.started() && !m_impl->exitCode_.has_value()) {
        return std::unexpected("a tool is already running");
    }

    detail::SpawnChildRequest child;
    child.cwd_ = *m_impl->root_;
    child.pipeOutput_ = true;
    child.processTree_ = true;
    auto addScript = [&](const std::filesystem::path &script) {
#ifdef _WIN32
        const std::string shell = IRUtility::envString("IR_RUN_BASH");
        if (!shell.empty()) {
            child.argv_.push_back(shell);
        }
#endif
        child.argv_.push_back(script.string());
    };

    if (request.tool_ == ToolName::IR_BUILD) {
        if (request.target_.find('\0') != std::string::npos) {
            return std::unexpected("target contains a NUL byte");
        }
        if (!IRAsset::isTargetName(request.target_)) {
            return std::unexpected("target is not a valid target name");
        }
        const auto tool = fixedTool("ir-build");
        if (!tool.has_value()) {
            return std::unexpected("ir-build was not found beside ir-run or on PATH");
        }
        addScript(*tool);
        child.argv_.insert(child.argv_.end(), {"--target", request.target_});
        child.environment_.push_back({"IRREDEN_BUILD_DIR", (*m_impl->root_ / "build").string()});
    } else {
        const auto resolved = IRUtility::resolveContainedPath(*m_impl->root_, request.path_);
        if (!resolved.has_value()) {
            return std::unexpected("path must resolve inside the tool root");
        }
        std::optional<std::filesystem::path> tool;
        if (request.tool_ == ToolName::EDITOR) {
#ifdef _WIN32
            tool = IRUtility::findOnPath("cursor", {".exe", ".cmd", ".bat"});
            if (!tool.has_value()) {
                tool = IRUtility::findOnPath("code", {".exe", ".cmd", ".bat"});
            }
#else
            tool = IRUtility::findOnPath("cursor", {""});
            if (!tool.has_value()) {
                tool = IRUtility::findOnPath("code", {""});
            }
#endif
        } else {
#ifdef _WIN32
            tool = IRUtility::findOnPath("explorer", {".exe", ".cmd", ".bat"});
#elif defined(__APPLE__)
            tool = IRUtility::findOnPath("open", {""});
#else
            tool = IRUtility::findOnPath("xdg-open", {""});
#endif
        }
        if (!tool.has_value()) {
            return std::unexpected(
                request.tool_ == ToolName::EDITOR
                    ? "no supported editor was found on PATH"
                    : "the platform folder opener was not found on PATH"
            );
        }
#ifdef _WIN32
        if (isCommandScript(*tool)) {
            std::string commandInterpreter = IRUtility::envString("COMSPEC");
            if (commandInterpreter.empty()) {
                commandInterpreter = "cmd.exe";
            }
            const auto commandLine =
                detail::windowsCommandScriptLine(commandInterpreter, *tool, *resolved);
            if (!commandLine.has_value()) {
                return std::unexpected(commandLine.error());
            }
            child.argv_ = {std::move(commandInterpreter)};
            child.verbatimWindowsCommandLine_ = *commandLine;
        } else {
            child.argv_ = {tool->string(), resolved->string()};
        }
#else
        child.argv_ = {tool->string(), resolved->string()};
#endif
        child.environment_.push_back({"IRREDEN_BUILD_DIR", std::nullopt});
    }

    detail::ChildProcess process = detail::spawnChild(child);
    if (!process.started()) {
        return std::unexpected(process.error_);
    }
    m_impl->child_ = std::move(process);
    m_impl->lines_.clear();
    m_impl->partial_.clear();
    m_impl->droppingPartial_ = false;
    m_impl->exitCode_.reset();
    m_impl->dropped_ = 0;
    ++m_impl->generation_;
    return ToolHandle{this, m_impl->generation_};
}

void ToolService::tick() {
    if (m_impl->child_.started()) {
        m_impl->drain();
    }
}

bool ToolService::live() {
    if (m_impl->child_.started() && !m_impl->exitCode_.has_value()) {
        m_impl->drain();
    }
    return m_impl->child_.started() && !m_impl->exitCode_.has_value();
}

void ToolService::shutdown() {
    if (m_impl != nullptr && live()) {
        m_impl->exitCode_ = detail::killChild(m_impl->child_);
        m_impl->drain();
    }
}

void bindToolSpawnLua(
    IRScript::LuaScript &script, ToolService &service, std::function<bool()> isMainThread
) {
    sol::state &lua = script.lua();
    sol::table engine = lua["IREngine"].valid() ? lua["IREngine"].get<sol::table>()
                                                : lua.create_named_table("IREngine");
    lua.new_usertype<ToolHandle>(
        "IREngineToolHandle",
        sol::no_constructor,
        "poll",
        &ToolHandle::poll,
        "exit",
        &ToolHandle::exitCode,
        "kill",
        &ToolHandle::kill,
        "droppedLines",
        &ToolHandle::droppedLines
    );
    engine["spawnTool"] = IRScript::detail::statefulLuaFunction(
        [&service, isMainThread = std::move(isMainThread)](const sol::object &arg) {
            if (!isMainThread()) {
                raise("must be called from the main thread");
            }
            if (arg.get_type() != sol::type::table) {
                raise("expects a table");
            }
            const sol::table requestTable = arg.as<sol::table>();
            const std::string tool = requiredString(requestTable, "tool");
            ToolSpawnRequest request;
            if (tool == "ir-build") {
                requireOnlyKeys(requestTable, {"tool", "target"});
                request.tool_ = ToolName::IR_BUILD;
                request.target_ = requiredString(requestTable, "target");
            } else if (tool == "editor" || tool == "open-folder") {
                requireOnlyKeys(requestTable, {"tool", "path"});
                request.tool_ = tool == "editor" ? ToolName::EDITOR : ToolName::OPEN_FOLDER;
                request.path_ = requiredString(requestTable, "path");
            } else {
                raise("unknown tool '" + tool + "'");
            }
            std::expected<ToolHandle, std::string> handle = service.spawn(request);
            if (!handle.has_value()) {
                raise(handle.error());
            }
            return std::move(*handle);
        }
    );
    engine["toolRoot"] = [&service]() -> sol::optional<std::string> {
        const auto root = service.root();
        return root.has_value() ? sol::optional<std::string>{root->string()} : sol::nullopt;
    };
}

} // namespace IREngine
