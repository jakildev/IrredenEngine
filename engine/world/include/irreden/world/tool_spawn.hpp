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

struct ToolSpawnRequest {
    ToolName tool_ = ToolName::IR_BUILD;
    std::string target_;
    std::string path_;
    /// One of @ref editorNames for the `editor` tool; empty runs the first
    /// entry found on PATH.
    std::string editor_;
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

    void setRoot(const std::filesystem::path &root);
    std::optional<std::filesystem::path> root() const;
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
