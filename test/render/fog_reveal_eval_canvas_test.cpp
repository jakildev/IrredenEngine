#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/canvas_part.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_fog_field.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_canvas.hpp>
#include <irreden/render/systems/system_fog_subject_adopt_canvas.hpp>
#include <irreden/render/systems/system_fog_subject_exempt_canvas.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/voxel_pool_teardown.hpp>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_EntityCanvas;
using IRComponents::C_FogExempt;
using IRComponents::C_FogField;
using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
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
    const IREntity::EntityId activeCanvas = IREntity::createEntity(fog);
    IRRender::setHeadlessActiveCanvasEntity(activeCanvas);
    IREntity::EntityId entity = 99;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_EntityCanvas canvas{};
    canvas.canvasEntity_ = IREntity::createEntity(
        C_VoxelPool{IRMath::ivec3(8, 1, 1)},
        IRComponents::C_DetachedCanvas{}
    );
    canvas.visible_ = false;
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> system;
    system.beginTick();
    system.fog_ = &fog;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;
    system.settings_.staggerPeriod_ = 1;

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
    IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
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
        const IRSystem::SystemId exempt =
            IRSystem::createSystem<IRSystem::FOG_SUBJECT_EXEMPT_CANVAS>();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, {exempt, adopt});
    }

    ~FogRevealEvalCanvasAdoptTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IREntity::EntityId createCanvasOwner(bool screenLocked = false, bool detached = true) {
        const IREntity::EntityId privateCanvas =
            detached ? IREntity::createEntity(
                           C_VoxelPool{IRMath::ivec3(8, 1, 1)},
                           IRComponents::C_DetachedCanvas{}
                       )
                     : IREntity::createEntity(C_VoxelPool{IRMath::ivec3(8, 1, 1)});
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

class FogSubjectExemptCanvasTest : public FogRevealEvalCanvasAdoptTest {};

TEST_F(FogSubjectExemptCanvasTest, PolicyCoversLaterAllocationReuseAndSeed) {
    const IREntity::EntityId exempt = createCanvasOwner();
    IREntity::setComponent(exempt, C_FogExempt{});
    auto &canvas = IREntity::getComponent<C_EntityCanvas>(exempt);
    auto &pool = IREntity::getComponent<C_VoxelPool>(canvas.canvasEntity_);

    runFrame();

    EXPECT_FLOAT_EQ(canvas.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(canvas.fogHidden_);
    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::BODY);
    EXPECT_EQ(pool.fogBodyFactor(), 255u);
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(exempt).has_value());
    const std::uint32_t exemptCarrier = IRComponents::VoxelReserved::kFogBody |
                                        (255u << IRComponents::VoxelReserved::kFogBodyFactorShift);
    const auto &originalRecords = pool.getColors();
    ASSERT_GE(originalRecords.size(), 2u);
    EXPECT_EQ(
        originalRecords[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
        exemptCarrier
    );
    EXPECT_EQ(
        originalRecords[1].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
        exemptCarrier
    );
    const std::uint64_t realizedGeneration = pool.getContentGeneration();

    runFrame();
    EXPECT_EQ(pool.getContentGeneration(), realizedGeneration);

    const auto fresh = pool.allocateVoxels(1);
    EXPECT_EQ(
        fresh.voxels_[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
        exemptCarrier
    );
    pool.deallocateVoxels(fresh.startIndex_, 1);
    const auto reused = pool.allocateVoxels(1);
    EXPECT_EQ(
        reused.voxels_[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
        exemptCarrier
    );

    const IREntity::EntityId attached = IREntity::createEntity(
        C_VoxelSetNew{
            IRMath::ivec3(1),
            IRMath::Color{10, 20, 30, 255},
            true,
            canvas.canvasEntity_,
        }
    );
    const auto &set = IREntity::getComponent<C_VoxelSetNew>(attached);
    ASSERT_EQ(set.numVoxels_, 1);
    EXPECT_EQ(
        set.voxels_[0].reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
        exemptCarrier
    );

    IRPrefab::Fog::setSubjectClass(exempt, IRPrefab::Fog::FogSubjectClass::FIELD);
    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::FIELD);
    for (const IRComponents::C_Voxel &voxel : pool.getColors()) {
        EXPECT_EQ(voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask, 0u);
    }
}

TEST_F(FogSubjectExemptCanvasTest, CombinedDetachedOwnerSetterClassifiesTheCanvas) {
    const IREntity::EntityId privateCanvas = IREntity::createEntity(
        C_VoxelPool{IRMath::ivec3(4, 1, 1)},
        IRComponents::C_DetachedCanvas{}
    );
    const IREntity::EntityId owner = IREntity::createEntity(
        C_VoxelSetNew{
            IRMath::ivec3(2, 1, 1),
            IRMath::Color{40, 80, 120, 255},
            true,
            privateCanvas,
        },
        C_EntityCanvas{privateCanvas, IRMath::ivec2(16), false, false}
    );

    IRPrefab::Fog::setSubjectClass(owner, IRPrefab::Fog::FogSubjectClass::BODY);
    ASSERT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(owner).has_value());
    auto &bodySet = IREntity::getComponent<C_VoxelSetNew>(owner);
    auto &bodyPool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    EXPECT_TRUE(bodySet.visible_);
    EXPECT_EQ(bodyPool.getActiveMask()[0] & 0x3u, 0x3u);

    C_CanvasFogOfWar fog = fogWithCircle(10.0f, 2.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> eval;
    eval.beginTick();
    eval.fog_ = &fog;
    eval.tick(
        owner,
        IREntity::getComponent<C_FogRevealed>(owner),
        IREntity::getComponent<C_WorldTransform>(owner),
        IREntity::getComponent<C_EntityCanvas>(owner)
    );
    EXPECT_FALSE(IREntity::getComponent<C_EntityCanvas>(owner).fogHidden_);
    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(owner).visible_);
    EXPECT_EQ(bodyPool.getActiveMask()[0] & 0x3u, 0x3u);

    IRPrefab::Fog::setSubjectClass(owner, IRPrefab::Fog::FogSubjectClass::EXEMPT);

    const auto &exemptCanvas = IREntity::getComponent<C_EntityCanvas>(owner);
    const auto &exemptSet = IREntity::getComponent<C_VoxelSetNew>(owner);
    const auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    EXPECT_FALSE(exemptCanvas.visible_);
    EXPECT_FLOAT_EQ(exemptCanvas.fogRevealFactor_, 1.0f);
    EXPECT_FALSE(exemptCanvas.fogHidden_);
    EXPECT_TRUE(exemptSet.visible_);
    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::BODY);
    EXPECT_EQ(pool.fogBodyFactor(), 255u);
    EXPECT_FALSE(IREntity::getComponentOptional<C_FogRevealed>(owner).has_value());
    for (const IRComponents::C_Voxel &voxel : IRPrefab::Fog::poolRecords(
             IREntity::getComponent<C_VoxelPool>(privateCanvas),
             IREntity::getComponent<C_VoxelSetNew>(owner)
         )) {
        EXPECT_EQ(
            voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
            IRComponents::VoxelReserved::kFogBody |
                (255u << IRComponents::VoxelReserved::kFogBodyFactorShift)
        );
    }

    IRPrefab::Fog::setSubjectClass(owner, IRPrefab::Fog::FogSubjectClass::FIELD);
    EXPECT_TRUE(IREntity::getComponent<C_VoxelSetNew>(owner).visible_);
    EXPECT_FLOAT_EQ(IREntity::getComponent<C_EntityCanvas>(owner).fogRevealFactor_, 1.0f);
    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::FIELD);
    for (const IRComponents::C_Voxel &voxel : pool.getColors()) {
        EXPECT_EQ(voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask, 0u);
    }
}

TEST_F(FogSubjectExemptCanvasTest, SetterLeavesNonDetachedPoolsUnmanaged) {
    const IREntity::EntityId owner = createCanvasOwner(false, false);
    const auto &canvas = IREntity::getComponent<C_EntityCanvas>(owner);
    auto &pool = IREntity::getComponent<C_VoxelPool>(canvas.canvasEntity_);

    IRPrefab::Fog::setSubjectClass(owner, IRPrefab::Fog::FogSubjectClass::EXEMPT);

    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::UNMANAGED);
    for (const IRComponents::C_Voxel &voxel : pool.getColors()) {
        EXPECT_EQ(voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask, 0u);
    }
}

class FogRevealEvalCanvasPromotionTest : public testing::Test {
  protected:
    FogRevealEvalCanvasPromotionTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_worldCanvas = IREntity::createEntity(
            C_VoxelPool{IRMath::ivec3(64, 4, 4)},
            C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_worldCanvas);
        std::list<IRSystem::SystemId> systems = IRPrefab::Fog::revealSystems();
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, systems);
        m_canvasEval = IRSystem::findSystem(IRSystem::FOG_REVEAL_EVAL_CANVAS);
    }

    ~FogRevealEvalCanvasPromotionTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    IREntity::EntityId createGridBody(bool visible) {
        if (visible) {
            IREntity::getComponent<C_CanvasFogOfWar>(m_worldCanvas)
                .setCell(0, 0, IRComponents::kFogStateVisible);
        }
        const IREntity::EntityId body = IREntity::createEntity(
            C_WorldTransform{},
            C_VoxelSetNew{
                IRMath::ivec3(2),
                IRMath::Color{80, 120, 160, 255},
                true,
                m_worldCanvas,
            }
        );
        runFrame();
        EXPECT_TRUE(IREntity::getComponentOptional<C_FogRevealed>(body).has_value());
        return body;
    }

    IREntity::EntityId
    promote(IREntity::EntityId body, bool screenLocked = false, bool detached = true) {
        const IREntity::EntityId privateCanvas =
            detached ? IREntity::createEntity(
                           C_VoxelPool{IRMath::ivec3(32, 4, 4)},
                           IRComponents::C_DetachedCanvas{}
                       )
                     : IREntity::createEntity(C_VoxelPool{IRMath::ivec3(32, 4, 4)});
        auto &set = IREntity::getComponent<C_VoxelSetNew>(body);
        IRPrefab::VoxelPool::restageSet(set);
        EXPECT_TRUE(set.attachToCanvas(privateCanvas));
        IREntity::setComponent(
            body,
            C_EntityCanvas{privateCanvas, IRMath::ivec2(16), true, screenLocked}
        );
        IREntity::setComponent(
            body,
            IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED_REVOXELIZE}
        );
        return privateCanvas;
    }

    void runFrame() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> &canvasEval() {
        return *m_systemManager.getSystemParams<IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS>>(
            m_canvasEval
        );
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_worldCanvas = IREntity::kNullEntity;
    IRSystem::SystemId m_canvasEval = IRSystem::kNullSystemId;
};

TEST_F(FogRevealEvalCanvasPromotionTest, PromotedBodyPolicyCoversLaterCanvasParts) {
    const IREntity::EntityId body = createGridBody(true);
    const IREntity::EntityId privateCanvas = promote(body);
    auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    ASSERT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::UNMANAGED);

    runFrame();

    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::BODY);
    EXPECT_EQ(pool.fogBodyFactor(), 255u);
    const std::uint64_t repairedGeneration = pool.getContentGeneration();
    runFrame();
    EXPECT_EQ(pool.getContentGeneration(), repairedGeneration);

    const IREntity::EntityId part = IRPrefab::CanvasPart::create(
        body,
        IRComponents::C_LocalTransform{},
        IRMath::ivec3(2),
        IRMath::Color{20, 40, 60, 255}
    );
    const auto &partSet = IREntity::getComponent<C_VoxelSetNew>(part);
    for (const IRComponents::C_Voxel &voxel : IRPrefab::Fog::poolRecords(pool, partSet)) {
        EXPECT_EQ(
            voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask,
            IRComponents::VoxelReserved::kFogBody |
                (255u << IRComponents::VoxelReserved::kFogBodyFactorShift)
        );
    }
}

TEST_F(FogRevealEvalCanvasPromotionTest, PromotionPreservesHiddenFactorBeforeCadence) {
    const IREntity::EntityId body = createGridBody(false);
    const IREntity::EntityId privateCanvas = promote(body);
    auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    auto &revealed = IREntity::getComponent<C_FogRevealed>(body);
    ASSERT_FLOAT_EQ(revealed.revealFactor_, 0.0f);
    canvasEval().settings_.staggerPeriod_ = 4096;
    canvasEval().frameCounter_ = 0;

    runFrame();

    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::BODY);
    EXPECT_EQ(pool.fogBodyFactor(), 0u);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, 0.0f);
}

TEST_F(FogRevealEvalCanvasPromotionTest, GhostPolicyUsesTheSamePromotionRepair) {
    const IREntity::EntityId body = createGridBody(true);
    ASSERT_TRUE(IRPrefab::Fog::setHiddenPolicy(body, IRComponents::FogHiddenPolicy::GHOST));
    const IREntity::EntityId privateCanvas = promote(body);
    auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    ASSERT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::UNMANAGED);

    runFrame();

    EXPECT_EQ(pool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::BODY);
    EXPECT_EQ(pool.fogBodyFactor(), 255u);
}

TEST_F(FogRevealEvalCanvasPromotionTest, ExistingAndExcludedPoolPoliciesStayUnchanged) {
    const IREntity::EntityId body = createGridBody(true);
    const IREntity::EntityId privateCanvas = promote(body);
    IREntity::getComponent<C_VoxelPool>(privateCanvas)
        .setFogCarrierPolicy(C_VoxelPool::FogCarrierPolicy::BODY, 17);
    const std::uint64_t managedGeneration =
        IREntity::getComponent<C_VoxelPool>(privateCanvas).getContentGeneration();
    const IREntity::EntityId lockedBody = createGridBody(true);
    const IREntity::EntityId lockedCanvas = promote(lockedBody, true);
    const IREntity::EntityId nonDetachedBody = createGridBody(true);
    const IREntity::EntityId nonDetachedCanvas = promote(nonDetachedBody, false, false);

    runFrame();

    const auto &pool = IREntity::getComponent<C_VoxelPool>(privateCanvas);
    const auto &lockedPool = IREntity::getComponent<C_VoxelPool>(lockedCanvas);
    const auto &nonDetachedPool = IREntity::getComponent<C_VoxelPool>(nonDetachedCanvas);
    EXPECT_EQ(pool.fogBodyFactor(), 17u);
    EXPECT_EQ(pool.getContentGeneration(), managedGeneration);
    EXPECT_EQ(lockedPool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::UNMANAGED);
    EXPECT_EQ(nonDetachedPool.fogCarrierPolicy(), C_VoxelPool::FogCarrierPolicy::UNMANAGED);
}

} // namespace
