#include <gtest/gtest.h>

#include <cstddef>

#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <vector>

namespace {

using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FogLineOfSightField;
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

// A horizon image with every cell clear, and a setter for one source's cell.
std::vector<float> clearHorizons() {
    return std::vector<float>(IRComponents::kFogLosHorizonCount, IRComponents::kFogLosHorizonClear);
}

void setHorizon(std::vector<float> &horizons, int source, int x, int y, float value) {
    horizons[FogLineOfSightField::horizonIndex(source, x, y)] = value;
}

void setHorizonColumnsFromX(std::vector<float> &horizons, int source, int fromX, float value) {
    for (int y = -20; y <= 20; ++y) {
        for (int x = fromX; x <= 20; ++x) {
            setHorizon(horizons, source, x, y, value);
        }
    }
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

// An occluded sample is unrevealed with no cost at all; clearing only the
// source's mask bit restores the full reveal.
TEST(FogRevealEvalTest, OccludedSampleIsUnrevealedRegardlessOfCost) {
    std::vector<float> horizons = clearHorizons();
    setHorizon(horizons, 0, 3, 4, -2.0f);
    const FogLineOfSightField field{horizons.data()};
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    observers.losSourceMask_ = 1;
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, 0)), 0.0f);
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, -2)), 1.0f)
        << "a sample at the horizon is visible";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3.3f, 3.6f, -1.6f)),
        1.0f
    ) << "the gate reads the rounded voxel (3, 4, -2)";

    observers.losSourceMask_ = 0;
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, 0)), 1.0f);
}

// An unoccluded sample takes the unchanged cost curve: over an all-clear field
// the gated reveal equals the cost-only overload, height penalty included.
TEST(FogRevealEvalTest, UnoccludedSampleTakesTheCostCurve) {
    const std::vector<float> horizons = clearHorizons();
    const FogLineOfSightField field{horizons.data()};
    FrameDataFogObservers observers = oneCircle(10.0f, 2.0f, 0.0f, 0.3f, 0.1f, 1.0f);
    observers.losSourceMask_ = 1;
    for (const IRMath::vec3 sample :
         {IRMath::vec3(3, 4, 0), IRMath::vec3(6, 5, -7), IRMath::vec3(9.5f, 0, 3)}) {
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, field, sample),
            IRPrefab::Fog::evalVisionReveal(observers, sample)
        );
    }
    EXPECT_GT(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(6, 5, -7)), 0.0f);
    EXPECT_LT(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(6, 5, -7)), 1.0f)
        << "the probe must sit on the curve, not at a plateau";
}

// Before the first publication a gated source reveals nothing while an
// ungated one still reveals.
TEST(FogRevealEvalTest, UnpublishedFieldRevealsNothingThroughAGatedSource) {
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    observers.visionCircles_[1] = IRMath::vec4(20.0f, 0.0f, 5.0f, 0.0f);
    observers.visionCircleCount_ = 2;
    observers.losSourceMask_ = 1;
    const FogLineOfSightField unpublished{};
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, unpublished, IRMath::vec3(1, 1, 0)),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, unpublished, IRMath::vec3(20, 1, 0)),
        1.0f
    );
}

// The system evaluates the published sources with the published field: a live
// set re-authored after the build never pairs with the old horizons.
TEST(FogRevealEvalTest, SnapshotPairsPublishedSourcesWithTheirField) {
    const std::vector<float> horizons = clearHorizons();
    const FogLineOfSightField published{horizons.data()};
    FrameDataFogObservers built = oneCircle(10.0f, 0.0f);
    built.losSourceMask_ = 1;
    FrameDataFogObservers live = built;
    live.visionCircles_[0] = IRMath::vec4(50.0f, 0.0f, 3.0f, 0.0f);

    FrameDataFogObservers observers{};
    FogLineOfSightField los{};
    IRPrefab::Fog::selectRevealSnapshot(live, built, published, observers, los);
    EXPECT_EQ(observers.visionCircles_[0], built.visionCircles_[0]);
    EXPECT_EQ(los.horizons_, horizons.data());

    IRPrefab::Fog::selectRevealSnapshot(live, built, FogLineOfSightField{}, observers, los);
    EXPECT_EQ(observers.visionCircles_[0], live.visionCircles_[0]);
    EXPECT_FALSE(los.published()) << "before the first build, gated sources read closed";

    live.losSourceMask_ = 0;
    IRPrefab::Fog::selectRevealSnapshot(live, built, published, observers, los);
    EXPECT_EQ(observers.visionCircles_[0], live.visionCircles_[0]);
    EXPECT_FALSE(los.published()) << "an ungated live set never reads the field";
}

// Two gated sources on opposite sides of a wall: a body behind the wall from A
// and inside only A's disc hides; one inside B's disc shows. Drives the live
// tick, so a tick that fell back to the cost-only overload fails here.
TEST(FogRevealEvalTest, SourcesOccludeIndependently) {
    C_VoxelPool pool{IRMath::ivec3(1)};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pendingByWorker_.resize(1);
    system.fogAttached_ = true;
    system.activeCanvas_ = IREntity::kNullEntity;
    system.activePool_ = &pool;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;
    system.settings_.staggerPeriod_ = 1;

    FrameDataFogObservers observers{};
    observers.visionCircles_[0] = IRMath::vec4(-6.0f, -6.0f, 10.0f, 0.0f);
    observers.visionCircles_[1] = IRMath::vec4(7.0f, 6.0f, 10.0f, 0.0f);
    observers.visionCircleCount_ = 2;
    observers.losSourceMask_ = 0b11;
    std::vector<float> horizons = clearHorizons();
    setHorizonColumnsFromX(horizons, 0, 2, -5.0f);
    for (int y = -20; y <= 20; ++y) {
        for (int x = -20; x <= -1; ++x) {
            setHorizon(horizons, 1, x, y, -5.0f);
        }
    }
    system.observers_ = observers;
    system.los_ = FogLineOfSightField{horizons.data()};

    const auto verdict = [&](IRMath::vec3 position) {
        IREntity::EntityId entity = 1;
        C_FogRevealed revealed{};
        C_WorldTransform transform{};
        C_VoxelSetNew voxelSet{};
        transform.translation_ = position;
        system.tick(entity, revealed, transform, voxelSet);
        return revealed;
    };
    const C_FogRevealed hiddenFromA = verdict(IRMath::vec3(3, -6, 0));
    EXPECT_FLOAT_EQ(hiddenFromA.revealFactor_, 0.0f);
    EXPECT_FALSE(hiddenFromA.shown_) << "a body behind the wall from its only source showed";
    EXPECT_TRUE(verdict(IRMath::vec3(3, 6, 0)).shown_);
    EXPECT_TRUE(verdict(IRMath::vec3(-3, -6, 0)).shown_);
    EXPECT_FALSE(verdict(IRMath::vec3(-3, 6, 0)).shown_) << "hidden from B, and outside A's disc";
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

// The fog pass reads unexploredColor as the std140 member after
// vec4 visionCircles[8] + ivec4 tail + vec4 visionCircleHeights[8]; the default
// is opaque black, the anchor the pass hard-coded before it became a parameter.
TEST(FogRevealEvalTest, UnexploredColorDefaultsToBlackAtItsStd140Offset) {
    EXPECT_EQ(offsetof(FrameDataFogObservers, unexploredColor_), 272u);
    const FrameDataFogObservers observers{};
    EXPECT_EQ(observers.unexploredColor_, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f));
    EXPECT_EQ(IRMath::colorToVec4(IRMath::IRColors::kBlack), observers.unexploredColor_);
}

// The write the active-canvas setter performs, on a payload: a non-black value
// lands normalized in unexploredColor_ and touches no other member.
TEST(FogRevealEvalTest, SetUnexploredColorWritesTheNormalizedAnchor) {
    FrameDataFogObservers observers = oneCircle(10.0f, 2.0f, 3.0f, 0.5f, 0.25f, 1.0f);
    const FrameDataFogObservers before = observers;

    IRPrefab::Fog::setUnexploredColor(observers, IRMath::Color{255, 0, 255, 128});

    EXPECT_EQ(observers.unexploredColor_, IRMath::vec4(1.0f, 0.0f, 1.0f, 128.0f / 255.0f));
    EXPECT_EQ(observers.visionCircleCount_, before.visionCircleCount_);
    EXPECT_EQ(observers.visionCircles_[0], before.visionCircles_[0]);
    EXPECT_EQ(observers.visionCircleHeights_[0], before.visionCircleHeights_[0]);

    IRPrefab::Fog::setUnexploredColor(observers, IRMath::IRColors::kBlack);
    EXPECT_EQ(observers.unexploredColor_, before.unexploredColor_);
}

// Headless (no RenderManager, so no active canvas) the creation-facing setter
// is the documented silent no-op rather than an assert.
TEST(FogRevealEvalTest, SetUnexploredColorWithoutAnActiveCanvasIsANoOp) {
    IRPrefab::Fog::setUnexploredColor(IRMath::Color{255, 0, 255, 255});
    EXPECT_EQ(IRPrefab::Fog::evalActiveVisionReveal(IRMath::vec3(0.0f)), 1.0f);
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
