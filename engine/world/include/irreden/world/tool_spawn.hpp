#ifndef TOOL_SPAWN_H
#define TOOL_SPAWN_H

#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IRScript {
class LuaScript;
}

namespace IREngine {

enum class ToolName : int { IR_BUILD = 0, EDITOR, OPEN_FOLDER };

/// Which root a request's process state and `path_` belong to. `ACTIVE_ROOT`
/// is every `IREngine.spawnTool` request: cwd and `IRREDEN_BUILD_DIR` follow
/// the active root, and `path_` must land inside it. `BASE` serves the
/// launcher's own files (its update log): `editor` and `open-folder` only,
/// confined to the base.
enum class ToolScope : int { ACTIVE_ROOT = 0, BASE };

struct ToolSpawnRequest {
    ToolName tool_ = ToolName::IR_BUILD;
    std::string target_;
    /// Relative to the base, whatever the scope.
    std::string path_;
    /// One of @ref editorNames for the `editor` tool; empty runs the first
    /// entry found on PATH.
    std::string editor_;
    ToolScope scope_ = ToolScope::ACTIVE_ROOT;
};

/// The `editor` tool's fixed table, in fallback order. A caller chooses an
/// entry by name; it never supplies a command.
std::span<const std::string_view> editorNames();

/// Whether the @ref editorNames entry @p name resolves on PATH.
bool editorAvailable(std::string_view name);

class ToolService;

class ToolHandle {
  public:
    std::vector<std::string> poll();
    std::optional<int> exitCode();
    void kill();
    std::uint64_t droppedLines() const;

  private:
    friend class ToolService;
    ToolHandle(ToolService *service, std::uint64_t generation);

    ToolService *m_service = nullptr;
    std::uint64_t m_generation = 0;
};

class ToolService {
  public:
    ToolService();
    explicit ToolService(const std::filesystem::path &root);
    ~ToolService();
    ToolService(ToolService &&) = delete;
    ToolService &operator=(ToolService &&) = delete;
    ToolService(const ToolService &) = delete;
    ToolService &operator=(const ToolService &) = delete;

    /// Sets the immutable base, once; the active root starts as the base.
    void setRoot(const std::filesystem::path &root);
    /// The immutable base.
    std::optional<std::filesystem::path> root() const;
    /// Makes the worktree root @p id (`IRUtility::resolveWorktreeRoot` under
    /// the base) the active root. Refused with no base, when @p id is not a
    /// live worktree root, or when it names another root while a tool run is
    /// live; re-selecting the active root only re-checks it.
    std::expected<void, std::string> selectRoot(std::string_view id);
    /// The active root's id; `.` until a @ref selectRoot.
    const std::string &activeRootId() const;
    /// The active root, re-resolved now: nullopt with no base or once the
    /// selected worktree is gone.
    std::optional<std::filesystem::path> activeRoot() const;
    std::expected<ToolHandle, std::string> spawn(const ToolSpawnRequest &request);
    void tick();
    bool live();
    void shutdown();

  private:
    friend class ToolHandle;
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

void bindToolSpawnLua(
    IRScript::LuaScript &script, ToolService &service, std::function<bool()> isMainThread
);

} // namespace IREngine

#endif /* TOOL_SPAWN_H */
