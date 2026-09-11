#include <gtest/gtest.h>

#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

namespace {

using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FrameDataFogObservers;

FrameDataFogObservers oneCircle(
    float radius,
    float edge,
    float observerZ = 0.0f,
    float zCostUp = 0.0f,
    float zCostDown = 0.0f,
    float freeBand = 0.0f
) {
    FrameDataFogObservers observers{};
    observers.visionCircles_[0] = IRMath::vec4(0.0f, 0.0f, radius, edge);
    observers.visionCircleHeights_[0] = IRMath::vec4(observerZ, zCostUp, zCostDown, freeBand);
    observers.visionCircleCount_ = 1;
    return observers;
}

TEST(FogRevealEvalTest, MirrorsDiscHeightPenaltyAndSoftEdge) {
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(oneCircle(10.0f, 0.0f), IRMath::vec3(3, 4, 0)),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(oneCircle(10.0f, 0.0f), IRMath::vec3(11, 0, 0)),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(oneCircle(10.0f, 0.0f, 0.0f, 2.0f), IRMath::vec3(0, 0, -6)),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(
            oneCircle(10.0f, 0.0f, 0.0f, 0.0f, 2.0f, 2.0f),
            IRMath::vec3(0, 0, 6)
        ),
        1.0f
    );
    EXPECT_NEAR(
        IRPrefab::Fog::evalVisionReveal(oneCircle(10.0f, 2.0f), IRMath::vec3(10, 0, 0)),
        0.5f,
        1e-6f
    );
}

TEST(FogRevealEvalTest, RejectsOutsideBoundingRadiusBeforeExactCurve) {
    FrameDataFogObservers observers = oneCircle(4.0f, 1.0f, 1000.0f, 1000.0f, 1000.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(5.01f, 0.0f, 1000.0f)),
        0.0f
    );
}

TEST(FogRevealEvalTest, HysteresisAndStaggerControlEntityVerdict) {
    C_VoxelPool pool{IRMath::ivec3(1)};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pendingByWorker_.resize(1);
    system.fogAttached_ = true;
    system.activeCanvas_ = IREntity::kNullEntity;
    system.activePool_ = &pool;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;
    system.settings_.staggerPeriod_ = 2;
    system.frameCounter_ = 1;
    system.observers_ = oneCircle(10.0f, 2.0f);

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_VoxelSetNew voxelSet{};

    transform.translation_ = IRMath::vec3(0);
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_TRUE(revealed.shown_);

    transform.translation_ = IRMath::vec3(10.5f, 0, 0);
    system.frameCounter_ = 3;
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_TRUE(revealed.shown_);
    EXPECT_GT(revealed.revealFactor_, system.settings_.hideThreshold_);

    transform.translation_ = IRMath::vec3(12.0f, 0, 0);
    system.frameCounter_ = 4;
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_TRUE(revealed.shown_) << "off-phase entity must retain its prior verdict";
    system.frameCounter_ = 5;
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_FALSE(revealed.shown_);
}

TEST(FogRevealEvalTest, MissingPoolDoesNotLatchAnUnappliedTransition) {
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pendingByWorker_.resize(1);
    system.fogAttached_ = false;
    system.activeCanvas_ = IREntity::kNullEntity;

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_VoxelSetNew voxelSet{};
    system.tick(entity, revealed, transform, voxelSet);

    EXPECT_FLOAT_EQ(revealed.revealFactor_, 1.0f);
    EXPECT_FALSE(revealed.shown_);
    EXPECT_TRUE(system.pendingByWorker_[0].empty());
}

TEST(FogRevealEvalTest, ActiveMaskHideAndRestoreAreAlphaPreserving) {
    C_VoxelPool pool{IRMath::ivec3(4, 1, 1)};
    auto allocation = pool.allocateVoxels(4);
    C_VoxelSetNew voxelSet{};
    voxelSet.voxelStartIdx_ = 0;
    voxelSet.numVoxels_ = 4;
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pendingByWorker_.resize(1);
    allocation.voxels_[0].color_.alpha_ = 255;
    allocation.voxels_[1].color_.alpha_ = 0;
    allocation.voxels_[2].color_.alpha_ = 255;
    allocation.voxels_[3].color_.alpha_ = 255;
    pool.resyncActiveMaskFromColors(0, 4);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0xdu);

    system.pendingByWorker_[0].push_back({&voxelSet, &pool, false});
    system.endTick();
    EXPECT_FALSE(voxelSet.visible_);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0u);
    EXPECT_EQ(allocation.voxels_[0].color_.alpha_, 255);
    EXPECT_EQ(allocation.voxels_[1].color_.alpha_, 0);

    system.pendingByWorker_[0][0].visible_ = true;
    system.endTick();
    EXPECT_TRUE(voxelSet.visible_);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0xdu);
}

} // namespace
