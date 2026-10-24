#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/input/systems/system_hitbox_mouse_test_gui.hpp>
#include <irreden/render/components/component_widget.hpp>

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
    EXPECT_EQ(hitbox.offset_, ivec2(0, 0));
    EXPECT_FALSE(hitbox.hovered_);
}

namespace {

bool hoveredAt(C_HitBox2DGui hitbox, ivec2 pos, IRMath::vec2 mouse) {
    IRSystem::System<IRSystem::HITBOX_MOUSE_TEST_GUI> test;
    test.mouseGuiTrixel_ = mouse;
    test.tick(hitbox, IRComponents::C_GuiPosition{pos});
    return hitbox.hovered_;
}

} // namespace

TEST(CHitBox2DGuiTest, OffsetMovesTheHitRegionWithoutMovingThePosition) {
    C_HitBox2DGui hitbox{ivec2(100, 20)};
    hitbox.offset_ = ivec2(0, -30);
    const ivec2 pos{10, 50};
    EXPECT_TRUE(hoveredAt(hitbox, pos, IRMath::vec2(20.0f, 20.0f))) << "the region's top edge";
    EXPECT_TRUE(hoveredAt(hitbox, pos, IRMath::vec2(20.0f, 39.5f)));
    EXPECT_FALSE(hoveredAt(hitbox, pos, IRMath::vec2(20.0f, 19.5f))) << "above the region";
    EXPECT_FALSE(hoveredAt(hitbox, pos, IRMath::vec2(20.0f, 40.0f))) << "half-open bottom edge";
}

// The hitbox WIDGET_APPLY_DROPDOWN writes for an open dropdown whose strip
// opens upward makes the strip's first row hoverable; the same point misses
// the rect a below-only placement would cover.
TEST(CHitBox2DGuiTest, UpwardDropdownOffsetMakesItsFirstStripRowHoverable) {
    IRComponents::C_WidgetDropdown dropdown;
    dropdown.itemHeight_ = 10;
    dropdown.items_.assign(40, "ITEM");
    const ivec2 header{100, 20};
    const ivec2 pos{0, 365};
    const IRComponents::C_WidgetDropdown::StripLayout layout =
        dropdown.stripLayout(pos.y, header.y, 400);
    ASSERT_LT(layout.offsetY_, 0);

    C_HitBox2DGui upward{ivec2(header.x, dropdown.expandedHeight(header.y, layout))};
    upward.offset_ = ivec2(0, dropdown.expandedOffsetY(layout));
    const IRMath::vec2 firstRow(
        50.0f,
        static_cast<float>(pos.y) + dropdown.itemCenterOffsetY(layout, dropdown.topIndex(layout))
    );
    EXPECT_TRUE(hoveredAt(upward, pos, firstRow));
    EXPECT_TRUE(hoveredAt(upward, pos, IRMath::vec2(50.0f, static_cast<float>(pos.y) + 5.0f)))
        << "the header stays inside the grown region";

    const C_HitBox2DGui belowOnly{upward.size_};
    EXPECT_FALSE(hoveredAt(belowOnly, pos, firstRow));
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
    // The EntityManager constructor sets `g_entityManager`; the free-function
    // `IREntity::createEntity` used below routes through that global. The
    // member exists for that side-effect, not for direct use.
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
