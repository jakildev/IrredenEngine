#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_canvas.hpp>
#include <irreden/render/systems/system_fog_subject_adopt_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogField;
using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_WorldTransform;

C_CanvasFogOfWar fogWithCircle(float radius, float edge) {
    C_CanvasFogOfWar fog{C_CanvasFogOfWar::HeadlessInit{}};
    fog.observers_.visionCircles_[0] = IRMath::vec4(0.0f, 0.0f, radius, edge);
    fog.observers_.visionCircleCount_ = 1;
    return fog;
}

TEST(FogRevealEvalCanvasTest, OwnerVerdictDrivesIndependentFogState) {
    IREntity::EntityManager entityManager;
    C_CanvasFogOfWar fog = fogWithCircle(10.0f, 2.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> system;
    system.fog_ = &fog;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_EntityCanvas canvas{};
    canvas.canvasEntity_ = IREntity::createEntity(IRComponents::C_DetachedCanvas{});
    canvas.visible_ = false;

    transform.translation_ = IRMath::vec3(0.0f);
    system.tick(entity, revealed, transform, canvas);
    EXPECT_FLOAT_EQ(canvas.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(canvas.fogHidden_);
    EXPECT_FALSE(canvas.visible_);

    transform.translation_ = IRMath::vec3(10.5f, 0.0f, 0.0f);
    system.tick(entity, revealed, transform, canvas);
    EXPECT_TRUE(revealed.shown_);
    EXPECT_GT(canvas.fogRevealFactor_, system.settings_.hideThreshold_);

    transform.translation_ = IRMath::vec3(12.0f, 0.0f, 0.0f);
    system.tick(entity, revealed, transform, canvas);
    EXPECT_FALSE(revealed.shown_);
    EXPECT_TRUE(canvas.fogHidden_);
    EXPECT_FLOAT_EQ(canvas.fogRevealFactor_, 0.0f);
    EXPECT_FALSE(canvas.visible_);
}

TEST(FogRevealEvalCanvasTest, FogHiddenCanvasDoesNotCastWorldShadow) {
    IRComponents::C_CanvasLocalRotation rotation{};
    C_WorldTransform transform{};
    C_EntityCanvas canvas{};
    canvas.fogHidden_ = true;

    IRPrefab::CanvasPose::write(
        rotation,
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        transform,
        IRComponents::RotationMode::DETACHED_REVOXELIZE,
        canvas
    );

    EXPECT_FALSE(rotation.castsWorldShadow_);
    canvas.fogHidden_ = false;
    IRPrefab::CanvasPose::write(
        rotation,
        IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f),
        transform,
        IRComponents::RotationMode::DETACHED_REVOXELIZE,
        canvas
    );
    EXPECT_TRUE(rotation.castsWorldShadow_);
}

class FogRevealEvalCanvasAdoptTest : public testing::Test {
  protected:
    FogRevealEvalCanvasAdoptTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_worldCanvas = IREntity::createEntity(C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}});
        IREntity::setComponent(m_worldCanvas, C_WorldTransform{});
        IREntity::setComponent(m_worldCanvas, C_EntityCanvas{m_worldCanvas, IRMath::ivec2(16)});
        IRRender::setHeadlessActiveCanvasEntity(m_worldCanvas);
        const IRSystem::SystemId adopt =
            IRSystem::createSystem<IRSystem::FOG_SUBJECT_ADOPT_CANVAS>();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, {adopt});
    }

    ~FogRevealEvalCanvasAdoptTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IREntity::EntityId createCanvasOwner(bool screenLocked = false, bool detached = true) {
        const IREntity::EntityId privateCanvas =
            detached ? IREntity::createEntity(
                           C_VoxelPool{IRMath::ivec3(2, 1, 1)},
                           IRComponents::C_DetachedCanvas{}
                       )
                     : IREntity::createEntity(C_VoxelPool{IRMath::ivec3(2, 1, 1)});
        auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
        const IRRender::VoxelPoolAllocation allocation = pool.allocateVoxels(2);
        allocation.voxels_[0].color_ = IRMath::IRColors::kWhite;
        allocation.voxels_[1].color_ = IRMath::IRColors::kWhite;
        return IREntity::createEntity(
            C_WorldTransform{},
            C_EntityCanvas{privateCanvas, IRMath::ivec2(16), true, screenLocked}
        );
    }

    void runFrame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_worldCanvas = IREntity::kNullEntity;
};

TEST_F(FogRevealEvalCanvasAdoptTest, AdoptsWorldPlacedCanvasAndStampsItsPrivatePool) {
    const IREntity::EntityId body = createCanvasOwner();
    const auto &ownerCanvas = IREntity::getComponent<C_EntityCanvas>(body);
    auto &pool = IREntity::getComponent<C_VoxelPool>(ownerCanvas.canvasEntity_);
    const std::uint64_t beforeAdoption = pool.getContentGeneration();

    runFrame();

    EXPECT_GT(pool.getContentGeneration(), beforeAdoption);
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(m_worldCanvas).has_value());
    ASSERT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(body).has_value());
    const auto &canvas = IREntity::getComponent<C_EntityCanvas>(body);
    EXPECT_TRUE(canvas.fogHidden_);
    const auto &records = IREntity::getComponent<C_VoxelPool>(canvas.canvasEntity_).getColors();
    ASSERT_GE(records.size(), 2u);
    EXPECT_NE(records[0].reserved_ & IRComponents::VoxelReserved::kFogBody, 0u);
    EXPECT_NE(records[1].reserved_ & IRComponents::VoxelReserved::kFogBody, 0u);

    const std::uint64_t beforeReclassification = pool.getContentGeneration();
    IRPrefab::Fog::setSubjectClass(body, IRPrefab::Fog::FogSubjectClass::FIELD);
    EXPECT_GT(pool.getContentGeneration(), beforeReclassification);
}

TEST_F(FogRevealEvalCanvasAdoptTest, FieldAndScreenLockedCanvasesStayOutsideBodyAdoption) {
    const IREntity::EntityId field = createCanvasOwner();
    IREntity::setComponent(field, C_FogField{});
    const IREntity::EntityId screenLocked = createCanvasOwner(true);
    const IREntity::EntityId nonDetached = createCanvasOwner(false, false);

    runFrame();

    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(field).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(screenLocked).has_value());
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(nonDetached).has_value());
    const auto &fieldCanvas = IREntity::getComponent<C_EntityCanvas>(field);
    const auto &fieldRecords =
        IREntity::getComponent<C_VoxelPool>(fieldCanvas.canvasEntity_).getColors();
    EXPECT_EQ(fieldRecords[0].reserved_ & IRComponents::VoxelReserved::kFogBody, 0u);
}

} // namespace
