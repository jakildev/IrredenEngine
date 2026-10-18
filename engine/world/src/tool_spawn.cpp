#include <irreden/world/tool_spawn.hpp>

#include "../../process_spawn.hpp"
#include "tool_target.hpp"

#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/utility/contained_path.hpp>
#include <irreden/utility/path_utils.hpp>

#include <sol/sol.hpp>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace IREngine {

namespace {

constexpr std::size_t kMaxBufferedLines = 2048;

std::optional<std::filesystem::path> findOnPath(std::string_view name) {
    const char *rawPath = std::getenv("PATH");
    if (rawPath == nullptr) {
        return std::nullopt;
    }
#ifdef _WIN32
    constexpr char separator = ';';
    static constexpr std::string_view suffixes[] = {"", ".exe", ".cmd", ".bat"};
#else
    constexpr char separator = ':';
    static constexpr std::string_view suffixes[] = {""};
#endif
    std::string path{rawPath};
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find(separator, begin);
        const std::filesystem::path directory = path.substr(begin, end - begin);
        for (const std::string_view suffix : suffixes) {
            std::error_code error;
            const std::filesystem::path candidate =
                directory / (std::string{name} + std::string{suffix});
            if (std::filesystem::is_regular_file(candidate, error) && !error) {
                return std::filesystem::weakly_canonical(candidate, error);
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return std::nullopt;
}

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
    return findOnPath(name);
}

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
    std::optional<int> exitCode_;
    std::uint64_t generation_ = 0;
    std::uint64_t id_ = 0;
    std::uint64_t dropped_ = 0;

    void requireHandle(std::uint64_t id, std::uint64_t generation) const {
        if (id != id_ || generation != generation_) {
            throw std::runtime_error("stale tool handle");
        }
    }

    void consumeCompleteLines() {
        std::size_t newline = 0;
        while ((newline = partial_.find('\n')) != std::string::npos) {
            std::string line = partial_.substr(0, newline);
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            partial_.erase(0, newline + 1);
            if (lines_.size() == kMaxBufferedLines) {
                lines_.pop_front();
                ++dropped_;
            }
            lines_.push_back(std::move(line));
        }
    }

    void drain() {
        partial_ += detail::readChildOutput(child_);
        consumeCompleteLines();
        if (!exitCode_.has_value()) {
            exitCode_ = detail::pollChildExit(child_);
            if (exitCode_.has_value()) {
                partial_ += detail::readChildOutput(child_);
                consumeCompleteLines();
                if (!partial_.empty()) {
                    if (lines_.size() == kMaxBufferedLines) {
                        lines_.pop_front();
                        ++dropped_;
                    }
                    lines_.push_back(std::move(partial_));
                    partial_.clear();
                }
            }
        }
    }
};

ToolHandle::ToolHandle(ToolService *service, std::uint64_t id, std::uint64_t generation)
    : m_service(service)
    , m_id(id)
    , m_generation(generation) {}

std::vector<std::string> ToolHandle::poll() {
    m_service->m_impl->requireHandle(m_id, m_generation);
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
    m_service->m_impl->requireHandle(m_id, m_generation);
    m_service->m_impl->drain();
    return m_service->m_impl->exitCode_;
}

void ToolHandle::kill() {
    m_service->m_impl->requireHandle(m_id, m_generation);
    if (!m_service->m_impl->exitCode_.has_value()) {
        m_service->m_impl->exitCode_ = detail::killChild(m_service->m_impl->child_);
        m_service->m_impl->drain();
    }
}

std::uint64_t ToolHandle::droppedLines() const {
    m_service->m_impl->requireHandle(m_id, m_generation);
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

ToolService::ToolService(ToolService &&) noexcept = default;
ToolService &ToolService::operator=(ToolService &&) noexcept = default;

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
    const std::string shell = IRUtility::envString("IR_RUN_BASH");
    auto addScript = [&](const std::filesystem::path &script) {
#ifdef _WIN32
        if (!shell.empty()) {
            child.argv_.push_back(shell);
        }
#endif
        child.argv_.push_back(script.string());
    };

    if (request.tool_ == ToolName::IR_BUILD) {
        if (!detail::isToolTargetName(request.target_)) {
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
            tool = findOnPath("cursor");
            if (!tool.has_value()) {
                tool = findOnPath("code");
            }
        } else {
#ifdef _WIN32
            tool = findOnPath("explorer");
#elif defined(__APPLE__)
            tool = findOnPath("open");
#else
            tool = findOnPath("xdg-open");
#endif
        }
        if (!tool.has_value()) {
            return std::unexpected(
                request.tool_ == ToolName::EDITOR
                    ? "no supported editor was found on PATH"
                    : "the platform folder opener was not found on PATH"
            );
        }
        child.argv_ = {tool->string(), resolved->string()};
        child.environment_.push_back({"IRREDEN_BUILD_DIR", std::nullopt});
    }

    detail::ChildProcess process = detail::spawnChild(child);
    if (!process.started()) {
        return std::unexpected(process.error_);
    }
    m_impl->child_ = std::move(process);
    m_impl->lines_.clear();
    m_impl->partial_.clear();
    m_impl->exitCode_.reset();
    m_impl->dropped_ = 0;
    ++m_impl->generation_;
    ++m_impl->id_;
    return ToolHandle{this, m_impl->id_, m_impl->generation_};
}

void ToolService::tick() {
    if (m_impl->child_.started() && !m_impl->exitCode_.has_value()) {
        m_impl->drain();
    }
}

bool ToolService::live() const {
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
