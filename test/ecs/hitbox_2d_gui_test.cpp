#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>

#include <type_traits>

using IRComponents::C_HitBox2DGui;
using IRMath::ivec2;

// The component must be plain-data so the ECS column stays trivially
// movable (no surprise side effects when archetypes shuffle rows).
static_assert(std::is_default_constructible_v<C_HitBox2DGui>);
static_assert(std::is_trivially_destructible_v<C_HitBox2DGui>);
static_assert(std::is_copy_constructible_v<C_HitBox2DGui>);
static_assert(std::is_move_constructible_v<C_HitBox2DGui>);

TEST(CHitBox2DGuiTest, DefaultConstructsToZeroSizeNotHovered) {
    C_HitBox2DGui hitbox;
    EXPECT_EQ(hitbox.size_, ivec2(0, 0));
    EXPECT_FALSE(hitbox.hovered_);
}

TEST(CHitBox2DGuiTest, IvecCtorTakesFullExtents) {
    C_HitBox2DGui hitbox{ivec2(64, 16)};
    EXPECT_EQ(hitbox.size_, ivec2(64, 16));
    EXPECT_FALSE(hitbox.hovered_);
}

TEST(CHitBox2DGuiTest, WidthHeightCtorMatchesIvecCtor) {
    C_HitBox2DGui a{32, 8};
    C_HitBox2DGui b{ivec2(32, 8)};
    EXPECT_EQ(a.size_, b.size_);
}

// `forEachComponent<C_HitBox2DGui>` is the scan path
// `SYSTEM_ENTITY_HOVER_DETECT` uses to find the first GUI-hovered entity
// per frame. Tag entities and confirm the scan finds them in
// archetype-iteration order.
class CHitBox2DGuiScanTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(CHitBox2DGuiScanTest, ScanFindsHoveredEntities) {
    auto e1 = IREntity::createEntity(C_HitBox2DGui{16, 16});
    auto e2 = IREntity::createEntity(C_HitBox2DGui{32, 32});
    auto e3 = IREntity::createEntity(C_HitBox2DGui{48, 48});

    IREntity::getComponent<C_HitBox2DGui>(e2).hovered_ = true;
    IREntity::getComponent<C_HitBox2DGui>(e3).hovered_ = true;

    IREntity::EntityId firstHovered = IREntity::kNullEntity;
    IREntity::forEachComponent<C_HitBox2DGui>(
        [&firstHovered](IREntity::EntityId &id, C_HitBox2DGui &hitbox) {
            if (firstHovered == IREntity::kNullEntity && hitbox.hovered_) {
                firstHovered = id;
            }
        }
    );

    // All three entities share the same archetype (just `C_HitBox2DGui`),
    // so `forEachComponent` walks them in insertion order: e1 (not hovered,
    // skipped), e2 (hovered, locked in as firstHovered), e3 (hovered but
    // firstHovered already set). e2 is the documented tie-break.
    EXPECT_EQ(firstHovered, e2);
}
