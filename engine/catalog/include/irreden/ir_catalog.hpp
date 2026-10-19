#ifndef IR_CATALOG_H
#define IR_CATALOG_H

#include <irreden/catalog/asset_index.hpp>
#include <irreden/catalog/catalog.hpp>

#include <filesystem>
#include <string_view>

namespace IRCatalog {

class CatalogService {
  public:
    explicit CatalogService(std::filesystem::path toolRoot);

    Catalog scan(std::string_view root = ".", std::string_view buildDir = "build") const;
    AssetIndex assets(std::string_view root = ".") const;

    const std::filesystem::path &toolRoot() const {
        return m_toolRoot;
    }

  private:
    std::filesystem::path resolve(std::string_view relative, std::string_view field) const;

    std::filesystem::path m_toolRoot;
};

} // namespace IRCatalog

#endif // IR_CATALOG_H
