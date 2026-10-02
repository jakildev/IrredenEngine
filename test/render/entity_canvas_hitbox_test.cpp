#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/input/components/component_hitbox_2d.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/systems/system_entity_canvas_to_framebuffer.hpp>

namespace {

using IRComponents::C_EntityCanvas;
using IRComponents::C_HitBox2D;
using IRComponents::C_WorldTransform;
using IRSystem::ENTITY_CANVAS_TO_FRAMEBUFFER;

TEST(EntityCanvasHitbox, PlacementConvertsFramebufferYToMouseY) {
    C_HitBox2D hitbox;

    IRSystem::System<ENTITY_CANVAS_TO_FRAMEBUFFER>::publishHitboxPlacement(
        hitbox,
        723.0f,
        IRMath::vec2{642.0f, 377.0f},
        IRMath::vec2{256.0f, 128.0f},
        0,
        42
    );

    EXPECT_EQ(hitbox.centerScreen_, IRMath::vec2(642.0f, 346.0f));
    EXPECT_EQ(hitbox.halfExtent_, IRMath::vec2(128.0f, 64.0f));
    EXPECT_TRUE(hitbox.screenSpacePlaced_);
}

TEST(EntityCanvasHitbox, RemovingCanvasClearsScreenPlacement) {
    IREntity::EntityManager entityManager;
    const IREntity::EntityId entity =
        IREntity::createEntity(C_HitBox2D{}, C_EntityCanvas{}, C_WorldTransform{});
    IREntity::getComponent<C_HitBox2D>(entity).screenSpaceCenter_ = true;
    IREntity::getComponent<C_HitBox2D>(entity).screenSpacePlaced_ = true;

    IREntity::removeComponent<C_EntityCanvas>(entity);
    IRSystem::System<ENTITY_CANVAS_TO_FRAMEBUFFER> system;
    system.collectHitboxes();

    const C_HitBox2D &hitbox = IREntity::getComponent<C_HitBox2D>(entity);
    EXPECT_FALSE(hitbox.screenSpaceCenter_);
    EXPECT_FALSE(hitbox.screenSpacePlaced_);
}

} // namespace
