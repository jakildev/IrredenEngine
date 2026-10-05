#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_persistent.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_part.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_entity_canvas_teardown_hook.hpp>
#include <irreden/render/entity_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_DetachedCanvas;
using IRComponents::C_EntityCanvas;
using IRComponents::C_EntityCanvasTeardownHook;
using IRComponents::C_Persistent;
using IRComponents::C_RotationMode;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRComponents::RotationMode;
using IREntity::EntityId;

class EntityCanvasTeardown : public testing::Test {
  protected:
    static EntityId makeCanvas() {
        return IREntity::createEntity(C_DetachedCanvas{});
    }

    static EntityId makeOwner(EntityId canvas) {
        return IREntity::createEntity(C_EntityCanvas{canvas, IRMath::ivec2(16)});
    }

    IREntity::EntityManager m_entityManager;
};

TEST_F(EntityCanvasTeardown, DeferredOwnerDestroyRemovesCanvasInSameDrain) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = makeCanvas();
    const EntityId owner = makeOwner(canvas);

    IREntity::destroyTree(owner);
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(owner));
    EXPECT_FALSE(IREntity::entityExists(canvas));
}

TEST_F(EntityCanvasTeardown, NullCanvasIsIgnored) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId owner = makeOwner(IREntity::kNullEntity);

    IREntity::destroyTree(owner);
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(owner));
}

TEST_F(EntityCanvasTeardown, OwnerAndCanvasMarkedTogetherAreDestroyedOnce) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = makeCanvas();
    const EntityId owner = makeOwner(canvas);

    IREntity::destroyTree(owner);
    IREntity::destroyTree(canvas);
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(owner));
    EXPECT_FALSE(IREntity::entityExists(canvas));
}

TEST_F(EntityCanvasTeardown, RemovingWrapperLeavesCanvasAlive) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = makeCanvas();
    const EntityId owner = makeOwner(canvas);
    IREntity::removeComponent<C_EntityCanvas>(owner);

    IREntity::destroyTree(owner);
    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(owner));
    EXPECT_TRUE(IREntity::entityExists(canvas));
}

TEST_F(EntityCanvasTeardown, ArmingTwiceKeepsOneHook) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const IREntity::PreDestroyHookId first =
        IREntity::singleton<C_EntityCanvasTeardownHook>().hookId_;

    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();

    EXPECT_NE(first, IREntity::kInvalidPreDestroyHookId);
    EXPECT_EQ(IREntity::singleton<C_EntityCanvasTeardownHook>().hookId_, first);
}

TEST_F(EntityCanvasTeardown, HostedPartSurvivesHostOnGrid) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = IREntity::createEntity(
        C_DetachedCanvas{},
        C_VoxelPool{IRMath::ivec3(16)},
        C_CanvasLocalRotation{}
    );
    const EntityId owner = IREntity::createEntity(
        C_EntityCanvas{canvas, IRMath::ivec2(16)},
        C_RotationMode{RotationMode::DETACHED_REVOXELIZE}
    );
    const EntityId part = IREntity::createEntity(
        C_VoxelSetNew{
            IRMath::ivec3(2),
            IRMath::Color{200, 80, 40, 255},
            EntityAnchor::CENTER,
            canvas
        },
        C_RotationMode{RotationMode::GRID}
    );
    ASSERT_TRUE(IRPrefab::CanvasPart::attach(part, owner));
    ASSERT_EQ(IREntity::getComponent<C_VoxelSetNew>(part).canvasEntity_, canvas);

    IREntity::destroyTree(owner);
    m_entityManager.destroyMarkedEntities();

    EXPECT_TRUE(IREntity::entityExists(part));
    EXPECT_EQ(IREntity::getComponent<C_RotationMode>(part).mode_, RotationMode::GRID);
    const EntityId newCanvas = IREntity::getComponent<C_VoxelSetNew>(part).canvasEntity_;
    EXPECT_NE(newCanvas, canvas);
    EXPECT_TRUE(newCanvas == IREntity::kNullEntity || IREntity::entityExists(newCanvas));
    EXPECT_FALSE(IREntity::entityExists(canvas));
}

TEST_F(EntityCanvasTeardown, NonDetachedAndPersistentCanvasesAreProtected) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId persistentCanvas = IREntity::createEntity(C_DetachedCanvas{}, C_Persistent{});
    const EntityId ordinaryEntity = IREntity::createEntity();
    const EntityId persistentOwner = makeOwner(persistentCanvas);
    const EntityId ordinaryOwner = makeOwner(ordinaryEntity);

    IREntity::destroyTree(persistentOwner);
    IREntity::destroyTree(ordinaryOwner);
    m_entityManager.destroyMarkedEntities();

    EXPECT_TRUE(IREntity::entityExists(persistentCanvas));
    EXPECT_TRUE(IREntity::entityExists(ordinaryEntity));
}

TEST_F(EntityCanvasTeardown, EagerOwnerDestroyLeavesCanvasMarkedUntilDrain) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = makeCanvas();
    const EntityId owner = makeOwner(canvas);

    m_entityManager.destroyTree(owner);

    EXPECT_FALSE(IREntity::entityExists(owner));
    EXPECT_TRUE(IREntity::entityExists(canvas));
    EXPECT_TRUE(m_entityManager.isMarkedForDeletion(canvas));

    m_entityManager.destroyMarkedEntities();

    EXPECT_FALSE(IREntity::entityExists(canvas));
}

TEST_F(EntityCanvasTeardown, ResetDestroysOwnerCanvasAndClearsItsMark) {
    IRPrefab::EntityCanvas::ensureOwnerTeardownHook();
    const EntityId canvas = makeCanvas();
    const EntityId owner = makeOwner(canvas);

    IREntity::resetGameplay();

    EXPECT_FALSE(IREntity::entityExists(owner));
    EXPECT_FALSE(IREntity::entityExists(canvas));
    EXPECT_FALSE(m_entityManager.isMarkedForDeletion(canvas));
}

} // namespace
