#ifndef IR_SCRIPT_PATH_GRANTS_H
#define IR_SCRIPT_PATH_GRANTS_H

#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace IRScript {

/// The absolute paths a Lua session may read outside its base directories.
/// A confined reader (`IRFile`, the `IRGui` and `IRCanvas` image loaders) resolves a
/// root-relative path through `IRUtility::resolveContainedPath` and an absolute
/// one through this set, which starts empty and which no Lua function adds to:
/// only C++ grants, and the engine does so for exactly what the user picked in
/// the native open panel (`IRDialog.open`) or dropped on the window
/// (`IRInput.droppedFiles`). Grants are read-only and die with the owning
/// `LuaScript`. docs/design/native-file-dialog.md.
class PathGrants {
  public:
    /// Grants exactly the regular file @p path names, nothing beside it.
    /// False when it does not resolve to one.
    bool grantFile(const std::filesystem::path &path);

    /// Grants the directory @p path names and everything beneath it. False
    /// when it does not resolve to one.
    bool grantDirectory(const std::filesystem::path &path);

    /// @p path (UTF-8) with every existing component resolved, when it is
    /// absolute and that resolved form is a granted file or lies inside a
    /// granted directory. A `..` or a symlink that leaves the grant resolves
    /// outside it and is refused, as is any relative path.
    std::optional<std::filesystem::path> resolve(std::string_view path) const;

    std::size_t size() const {
        return m_files.size() + m_directories.size();
    }

    void clear();

  private:
    std::vector<std::filesystem::path> m_files;
    std::vector<std::filesystem::path> m_directories;
};

/// @p path for a confined read: contained in @p canonicalBase when relative,
/// granted when absolute, nullopt otherwise. Writers keep calling
/// `IRUtility::resolveContainedPath` alone.
std::optional<std::filesystem::path> resolveReadablePath(
    const PathGrants &grants, const std::filesystem::path &canonicalBase, std::string_view path
);

} // namespace IRScript

#endif /* IR_SCRIPT_PATH_GRANTS_H */
