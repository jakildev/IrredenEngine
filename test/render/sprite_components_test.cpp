#include <gtest/gtest.h>

#include <irreden/render/components/component_sprite.hpp>
#include <irreden/render/components/component_sprite_sheet.hpp>

namespace {

TEST(SpriteComponents, SpriteDefaults) {
    IRComponents::C_Sprite sprite{};
    EXPECT_EQ(sprite.textureHandle_, 0u);
    EXPECT_FLOAT_EQ(sprite.size_.x, 0.0f);
    EXPECT_FLOAT_EQ(sprite.size_.y, 0.0f);
    EXPECT_FLOAT_EQ(sprite.uvRect_.x, 0.0f);
    EXPECT_FLOAT_EQ(sprite.uvRect_.y, 0.0f);
    EXPECT_FLOAT_EQ(sprite.uvRect_.z, 1.0f);
    EXPECT_FLOAT_EQ(sprite.uvRect_.w, 1.0f);
    EXPECT_FLOAT_EQ(sprite.anchor_.x, 0.5f);
    EXPECT_FLOAT_EQ(sprite.anchor_.y, 0.0f);
    EXPECT_EQ(sprite.tint_.alpha_, 0xFFu);
    EXPECT_FALSE(sprite.screenPixelSmooth_);
}

TEST(SpriteComponents, SpriteFullConstructorAcceptsScreenPixelSmooth) {
    IRComponents::C_Sprite sprite{
        7u,
        IRMath::vec2{16.0f, 16.0f},
        IRMath::vec4{0.0f, 0.0f, 1.0f, 1.0f},
        IRMath::vec2{0.5f, 0.5f},
        IRMath::IRColors::kWhite,
        true
    };
    EXPECT_TRUE(sprite.screenPixelSmooth_);
}

TEST(SpriteComponents, SpriteTwoArgConstructorPicksAnchorDefault) {
    IRComponents::C_Sprite sprite{42u, IRMath::vec2{32.0f, 48.0f}};
    EXPECT_EQ(sprite.textureHandle_, 42u);
    EXPECT_FLOAT_EQ(sprite.size_.x, 32.0f);
    EXPECT_FLOAT_EQ(sprite.size_.y, 48.0f);
    EXPECT_FLOAT_EQ(sprite.anchor_.x, 0.5f);
    EXPECT_FLOAT_EQ(sprite.anchor_.y, 0.0f);
}

TEST(SpriteComponents, SpriteSheetDefaultsAndPopulate) {
    IRComponents::C_SpriteSheet sheet{};
    EXPECT_EQ(sheet.textureHandle_, 0u);
    EXPECT_TRUE(sheet.frames_.empty());
    EXPECT_TRUE(sheet.animations_.empty());

    sheet.frames_.push_back(
        IRComponents::SpriteFrame{IRMath::vec4{0.0f, 0.0f, 0.5f, 1.0f}, IRMath::ivec2{16, 32}}
    );
    sheet.animations_.push_back(
        IRComponents::NamedAnimation{"walk_left", IRComponents::SpriteAnimation{0, 1, 12.0f}}
    );

    EXPECT_EQ(sheet.frames_.size(), 1u);
    const int idx = sheet.findAnimationIndex("walk_left");
    ASSERT_EQ(idx, 0);
    EXPECT_EQ(sheet.animations_[idx].animation_.firstFrame_, 0);
    EXPECT_EQ(sheet.animations_[idx].animation_.frameCount_, 1);
    EXPECT_FLOAT_EQ(sheet.animations_[idx].animation_.fps_, 12.0f);
    EXPECT_EQ(sheet.findAnimationIndex("missing"), -1);
}

} // namespace
