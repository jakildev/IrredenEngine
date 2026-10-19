#ifndef IR_CATALOG_ASSET_INDEX_H
#define IR_CATALOG_ASSET_INDEX_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace IRCatalog {

enum class AssetKind : std::uint8_t {
    VOXEL_SET = 0,
    PREFAB,
    PNG,
    DATA,
};

struct AssetEntry {
    AssetKind kind_ = AssetKind::DATA;
    std::string path_;
    double mtime_ = 0.0;
    std::map<std::string, std::string> fields_;
    std::vector<std::string> parts_;
};

struct AssetIndex {
    std::vector<AssetEntry> entries_;
};

} // namespace IRCatalog

#endif // IR_CATALOG_ASSET_INDEX_H
