#ifndef IR_CATALOG_H
#define IR_CATALOG_H

#include <irreden/catalog/asset_index.hpp>
#include <irreden/catalog/catalog.hpp>

#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace IRCatalog {

class CatalogService {
  public:
    explicit CatalogService(std::filesystem::path toolRoot);

    Catalog scan(std::string_view root = ".", std::string_view buildDir = "build") const;
    /// The worktree roots under the tool root (`IRUtility::discoverWorktreeRoots`).
    std::vector<std::string> roots() const;
    /// Every listed worktree root, each once in id order, with its own
    /// `<root>/build` and its docs resolved inside it. `Creation::root_` is the
    /// root id; every path stays tool-root-relative. Raises for an id that is
    /// not spelled as a worktree root id; skips one that no longer resolves.
    Catalog scanRoots(std::span<const std::string> roots) const;
    AssetIndex assets(std::string_view root = ".") const;

    const std::filesystem::path &toolRoot() const {
        return m_toolRoot;
    }

  private:
    std::filesystem::path resolve(std::string_view relative, std::string_view field) const;
    void scanTree(
        Catalog &catalog,
        const std::filesystem::path &resolvedRoot,
        const std::filesystem::path &resolvedBuildDir,
        const std::string &rootId,
        const std::filesystem::path &docsBase
    ) const;

    std::filesystem::path m_toolRoot;
};

} // namespace IRCatalog

#endif // IR_CATALOG_H
