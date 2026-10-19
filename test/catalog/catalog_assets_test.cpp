#include <irreden/ir_catalog.hpp>

#include <gtest/gtest.h>

#include <filesystem>

namespace {

const std::filesystem::path kFixtureRoot = std::filesystem::path(IR_CATALOG_FIXTURE_ROOT) / "root";

const IRCatalog::AssetEntry *
findAsset(const IRCatalog::AssetIndex &index, IRCatalog::AssetKind kind) {
    for (const auto &entry : index.entries_) {
        if (entry.kind_ == kind) {
            return &entry;
        }
    }
    return nullptr;
}

TEST(CatalogAssetsTest, FixtureTree) {
    const IRCatalog::CatalogService service(kFixtureRoot);
    const auto index = service.assets();

    const auto *voxel = findAsset(index, IRCatalog::AssetKind::VOXEL_SET);
    ASSERT_NE(voxel, nullptr);
    EXPECT_EQ(voxel->path_, "assets/voxel/entities/crate.vxs");
    EXPECT_EQ(voxel->fields_.at("mode"), "DENSE");
    EXPECT_EQ(voxel->fields_.at("frame_count"), "6");
    EXPECT_EQ(voxel->fields_.at("bounds"), "min=0,1,2;max=3,4,5");

    const auto *prefab = findAsset(index, IRCatalog::AssetKind::PREFAB);
    ASSERT_NE(prefab, nullptr);
    ASSERT_EQ(prefab->parts_.size(), 2u);
    EXPECT_EQ(prefab->parts_[0], "stem");
    EXPECT_EQ(prefab->parts_[1], "crown");
    EXPECT_EQ(prefab->fields_.at("part_count"), "2");

    const auto *png = findAsset(index, IRCatalog::AssetKind::PNG);
    ASSERT_NE(png, nullptr);
    EXPECT_EQ(png->path_, "test/references/fixture.png");

    const auto *data = findAsset(index, IRCatalog::AssetKind::DATA);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->path_, "creations/demos/valid/data/readme.txt");
}

} // namespace
