#include <gtest/gtest.h>

#include <irreden/ir_asset.hpp>

#include <cstdio>
#include <string>

namespace {

// All sidecar files are written to /tmp so no test fixtures or setup is needed.
const std::string kTmpDir = "/tmp";

TEST(SpriteSheetMeta, RoundTrip) {
    const std::string name = "ir_test_sprite_sheet_rt";

    IRAsset::SpriteSheetMeta meta{};
    meta.cellSizePx_ = uvec2{32, 48};
    meta.margin_     = 1;
    meta.padding_    = 2;
    meta.animations_ = {
        {"idle",       0, 4,  8.0f},
        {"walk_left",  4, 6, 12.0f},
        {"walk_right", 10, 6, 12.0f},
    };

    IRAsset::saveSpriteSheetMeta(name, kTmpDir, meta);

    const IRAsset::SpriteSheetMeta loaded = IRAsset::loadSpriteSheetMeta(name, kTmpDir);

    EXPECT_EQ(loaded.cellSizePx_.x, 32u);
    EXPECT_EQ(loaded.cellSizePx_.y, 48u);
    EXPECT_EQ(loaded.margin_,       1);
    EXPECT_EQ(loaded.padding_,      2);
    ASSERT_EQ(loaded.animations_.size(), 3u);

    EXPECT_EQ(loaded.animations_[0].name_,       "idle");
    EXPECT_EQ(loaded.animations_[0].firstFrame_,  0);
    EXPECT_EQ(loaded.animations_[0].frameCount_,  4);
    EXPECT_FLOAT_EQ(loaded.animations_[0].fps_,  8.0f);

    EXPECT_EQ(loaded.animations_[1].name_,       "walk_left");
    EXPECT_EQ(loaded.animations_[1].firstFrame_,  4);
    EXPECT_EQ(loaded.animations_[1].frameCount_,  6);
    EXPECT_FLOAT_EQ(loaded.animations_[1].fps_,  12.0f);

    EXPECT_EQ(loaded.animations_[2].name_,       "walk_right");
    EXPECT_EQ(loaded.animations_[2].firstFrame_,  10);
    EXPECT_EQ(loaded.animations_[2].frameCount_,  6);
    EXPECT_FLOAT_EQ(loaded.animations_[2].fps_,  12.0f);

    std::remove((kTmpDir + "/" + name + ".irsprite").c_str());
}

TEST(SpriteSheetMeta, OptionalFieldsDefaultToZero) {
    const std::string name = "ir_test_sprite_sheet_defaults";
    const std::string path = kTmpDir + "/" + name + ".irsprite";

    FILE *f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr) << "Could not write test sidecar to /tmp";
    fprintf(f, "cellWidth 16\ncellHeight 24\n");
    fclose(f);

    const IRAsset::SpriteSheetMeta loaded = IRAsset::loadSpriteSheetMeta(name, kTmpDir);

    EXPECT_EQ(loaded.cellSizePx_.x, 16u);
    EXPECT_EQ(loaded.cellSizePx_.y, 24u);
    EXPECT_EQ(loaded.margin_,       0);
    EXPECT_EQ(loaded.padding_,      0);
    EXPECT_TRUE(loaded.animations_.empty());

    std::remove(path.c_str());
}

TEST(SpriteSheetMeta, CommentLinesIgnored) {
    const std::string name = "ir_test_sprite_sheet_comments";
    const std::string path = kTmpDir + "/" + name + ".irsprite";

    FILE *f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fprintf(f, "# IRSprite sheet metadata\n");
    fprintf(f, "cellWidth 64\n");
    fprintf(f, "# another comment\n");
    fprintf(f, "cellHeight 64\n");
    fprintf(f, "anim run 0 8 24.0\n");
    fclose(f);

    const IRAsset::SpriteSheetMeta loaded = IRAsset::loadSpriteSheetMeta(name, kTmpDir);

    EXPECT_EQ(loaded.cellSizePx_.x, 64u);
    EXPECT_EQ(loaded.cellSizePx_.y, 64u);
    ASSERT_EQ(loaded.animations_.size(), 1u);
    EXPECT_EQ(loaded.animations_[0].name_, "run");
    EXPECT_EQ(loaded.animations_[0].frameCount_, 8);
    EXPECT_FLOAT_EQ(loaded.animations_[0].fps_, 24.0f);

    std::remove(path.c_str());
}

TEST(SpriteSheetMeta, MissingFileReturnsDefault) {
    const IRAsset::SpriteSheetMeta loaded =
        IRAsset::loadSpriteSheetMeta("ir_test_nonexistent_sheet", kTmpDir);

    EXPECT_EQ(loaded.cellSizePx_.x, 0u);
    EXPECT_EQ(loaded.cellSizePx_.y, 0u);
    EXPECT_TRUE(loaded.animations_.empty());
}

} // namespace
