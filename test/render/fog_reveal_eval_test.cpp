#include <gtest/gtest.h>

#include "common/allocation_counter.hpp"

#include <irreden/ir_job.hpp>
#include <irreden/job/job_manager.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_shape.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <set>
#include <thread>
#include <tuple>
#include <vector>

namespace {

using IRComponents::C_FogRevealed;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FogLosColumnField;
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

// A column field with no occluder, and a wall stamp: a tall box whose top
// plane sits at `top`.
std::vector<float> emptyField() {
    return std::vector<float>(
        IRComponents::kFogLosFieldFloatCount,
        IRComponents::kFogLosColumnEmpty
    );
}

// The field corner a window centred on the world origin anchors.
const IRMath::ivec2 kWorldFieldMin(-IRComponents::kFogLosFieldHalfExtent);

void stampWall(
    std::vector<float> &field,
    IRMath::vec2 minXY,
    IRMath::vec2 maxXY,
    float top,
    IRMath::ivec2 fieldMin = kWorldFieldMin
) {
    IRPrefab::Fog::stampLosBox(field, fieldMin, minXY, maxXY, top);
    IRPrefab::Fog::buildLosPyramid(field);
}

// Gates source 0 with its eye one unit above its observerZ.
FrameDataFogObservers gated(FrameDataFogObservers observers) {
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 1.0f);
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

// The BODY verdict kernel: a VISIBLE grid cell reveals fully on its own, an
// EXPLORED cell contributes nothing, and an UNEXPLORED cell inside a circle
// reads the circle term unchanged.
TEST(FogRevealEvalTest, GridAwareVerdictTakesTheMaxOfVisibleCellAndCircleTerm) {
    const FogLosColumnField noLos{};
    const FrameDataFogObservers none{};
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            none,
            noLos,
            IRComponents::kFogStateVisible,
            IRMath::vec3(3, 4, 0)
        ),
        1.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            none,
            noLos,
            IRComponents::kFogStateExplored,
            IRMath::vec3(3, 4, 0)
        ),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            none,
            noLos,
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(3, 4, 0)
        ),
        0.0f
    );

    const FrameDataFogObservers soft = oneCircle(10.0f, 2.0f);
    const float circleTerm = IRPrefab::Fog::evalVisionReveal(soft, IRMath::vec3(10, 0, 0));
    ASSERT_NEAR(circleTerm, 0.5f, 1e-6f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            soft,
            noLos,
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(10, 0, 0)
        ),
        circleTerm
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            soft,
            noLos,
            IRComponents::kFogStateExplored,
            IRMath::vec3(10, 0, 0)
        ),
        circleTerm
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            soft,
            noLos,
            IRComponents::kFogStateVisible,
            IRMath::vec3(10, 0, 0)
        ),
        1.0f
    );
}

// The grid term reads the world field at any column, with no window test: a
// far unexplored column reveals nothing, and a VISIBLE cell there reveals.
TEST(FogRevealEvalTest, GridTermReadsTheWorldFieldAtAFarColumn) {
    IRComponents::C_CanvasFogOfWar fog{IRComponents::C_CanvasFogOfWar::HeadlessInit{}};
    const FogLosColumnField noLos{};
    const FrameDataFogObservers none{};
    const IRMath::vec3 far(5000.0f, -3000.0f, 0.0f);
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalReveal(fog, none, noLos, far), 0.0f)
        << "a far unexplored column read as revealed";
    fog.setCell(5000, -3000, IRComponents::kFogStateVisible);
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalReveal(fog, none, noLos, far), 1.0f);
}

TEST(FogRevealEvalTest, QuantizedFactorRoundsHalfUpAndPinsTheEnds) {
    EXPECT_EQ(IRPrefab::Fog::quantizeRevealFactor(0.0f), 0);
    EXPECT_EQ(IRPrefab::Fog::quantizeRevealFactor(1.0f), 255);
    EXPECT_EQ(IRPrefab::Fog::quantizeRevealFactor(0.5f), 128);
    EXPECT_EQ(IRPrefab::Fog::quantizeRevealFactor(1.5f), 255);
    EXPECT_EQ(IRPrefab::Fog::quantizeRevealFactor(-0.5f), 0);
}

TEST(FogRevealEvalTest, RejectsOutsideBoundingRadiusBeforeExactCurve) {
    FrameDataFogObservers observers = oneCircle(4.0f, 1.0f, 1000.0f, 1000.0f, 1000.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(5.01f, 0.0f, 1000.0f)),
        0.0f
    );
}

// An occluded sample is unrevealed with no cost at all; a sample the segment
// clears above the wall keeps its reveal, and clearing only the source's mask
// bit restores the full reveal.
TEST(FogRevealEvalTest, OccludedSampleIsUnrevealedRegardlessOfCost) {
    std::vector<float> columns = emptyField();
    stampWall(columns, IRMath::vec2(1.0f, 1.0f), IRMath::vec2(2.0f, 3.0f), -5.0f);
    const FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers = gated(oneCircle(10.0f, 0.0f));
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, 0)), 0.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, -20)),
        1.0f
    ) << "a sample the segment clears above the wall is visible";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3.3f, 0.2f, 0.0f)),
        1.0f
    ) << "a sample the segment passes beside the wall is visible";

    observers.losSourceMask_ = 0;
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(3, 4, 0)), 1.0f);
}

// The window re-anchor: a gated source and a wall several field half extents
// from the world origin, inside the field anchored on a window centred near
// them, hide the sample behind the wall. Control: the world-centred field
// cannot hold the wall, so there the sample keeps its full reveal.
TEST(FogRevealEvalTest, OffOriginOccluderHidesInTheWindowAnchoredField) {
    constexpr int kWindowEdge = 1152;
    const IRMath::vec2 offset(600.0f, -400.0f);
    const IRMath::ivec2 fieldMin = FogLosColumnField::fieldMinForWindow(
        IRPrefab::Fog::detail::windowOriginForCentre(offset, kWindowEdge),
        kWindowEdge
    );
    FrameDataFogObservers observers = gated(oneCircle(10.0f, 0.0f));
    observers.visionCircles_[0] = IRMath::vec4(offset.x, offset.y, 10.0f, 0.0f);
    const IRMath::vec3 behind(offset.x + 3.0f, offset.y + 4.0f, 0.0f);

    std::vector<float> anchored = emptyField();
    stampWall(
        anchored,
        offset + IRMath::vec2(1.0f),
        offset + IRMath::vec2(2.0f, 3.0f),
        -5.0f,
        fieldMin
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(
            observers,
            FogLosColumnField{anchored.data(), fieldMin},
            behind
        ),
        0.0f
    ) << "the off-origin wall hides nothing in the field anchored on the window";

    std::vector<float> worldCentred = emptyField();
    stampWall(worldCentred, offset + IRMath::vec2(1.0f), offset + IRMath::vec2(2.0f, 3.0f), -5.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, FogLosColumnField{worldCentred.data()}, behind),
        1.0f
    ) << "control: a world-centred field must not hold the off-origin wall";
}

// An unoccluded sample takes the unchanged cost curve: over an empty field
// the gated reveal equals the cost-only overload, height penalty included.
TEST(FogRevealEvalTest, UnoccludedSampleTakesTheCostCurve) {
    const std::vector<float> columns = emptyField();
    const FogLosColumnField field{columns.data()};
    const FrameDataFogObservers observers = gated(oneCircle(10.0f, 2.0f, 0.0f, 0.3f, 0.1f, 1.0f));
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

// A softened source scales its reveal by the band factor behind a low wall.
TEST(FogRevealEvalTest, SoftenedSourceScalesItsReveal) {
    std::vector<float> columns = emptyField();
    stampWall(columns, IRMath::vec2(-20.0f, -20.0f), IRMath::vec2(20.0f, 20.0f), 0.0f);
    stampWall(columns, IRMath::vec2(1.0f, -5.0f), IRMath::vec2(2.0f, 5.0f), -1.0f);
    const FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 3.0f, 2.0f);
    const float behind = IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(5, 0, 0));
    EXPECT_GT(behind, 0.0f);
    EXPECT_LT(behind, 1.0f) << "the band scales the reveal behind the wall";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, field, IRMath::vec3(-5, 0, 0)),
        1.0f
    );
}

// Before the first publication a gated source reveals nothing while an
// ungated one still reveals.
TEST(FogRevealEvalTest, UnpublishedFieldRevealsNothingThroughAGatedSource) {
    FrameDataFogObservers observers = gated(oneCircle(10.0f, 0.0f));
    observers.visionCircles_[1] = IRMath::vec4(20.0f, 0.0f, 5.0f, 0.0f);
    observers.visionCircleCount_ = 2;
    const FogLosColumnField unpublished{};
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
// set re-authored after the build never pairs with the old columns.
TEST(FogRevealEvalTest, SnapshotPairsPublishedSourcesWithTheirField) {
    const std::vector<float> columns = emptyField();
    const FogLosColumnField published{columns.data(), IRMath::ivec2(1024, -1152)};
    FrameDataFogObservers built = gated(oneCircle(10.0f, 0.0f));
    FrameDataFogObservers live = built;
    live.visionCircles_[0] = IRMath::vec4(50.0f, 0.0f, 3.0f, 0.0f);

    FrameDataFogObservers observers{};
    FogLosColumnField los{};
    IRPrefab::Fog::selectRevealSnapshot(live, built, published, observers, los);
    EXPECT_EQ(observers.visionCircles_[0], built.visionCircles_[0]);
    EXPECT_EQ(los.tops_, columns.data());
    EXPECT_EQ(los.fieldMin_, published.fieldMin_) << "the field travels with its anchor";

    IRPrefab::Fog::selectRevealSnapshot(live, built, FogLosColumnField{}, observers, los);
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
    system.pending_.reset(4);
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
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 1.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 1.0f);
    std::vector<float> columns = emptyField();
    stampWall(columns, IRMath::vec2(1.0f, -20.0f), IRMath::vec2(2.0f, 20.0f), -5.0f);
    system.observers_ = observers;
    system.los_ = FogLosColumnField{columns.data()};

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

    // Bound as beginTick binds it, the tick reads its hard routes from the
    // system's cache and keeps every verdict.
    system.losRoutes_.begin(system.observers_, system.los_);
    EXPECT_FLOAT_EQ(verdict(IRMath::vec3(3, -6, 0)).revealFactor_, 0.0f);
    EXPECT_TRUE(verdict(IRMath::vec3(3, 6, 0)).shown_);
    EXPECT_TRUE(verdict(IRMath::vec3(-3, -6, 0)).shown_);
    EXPECT_FALSE(verdict(IRMath::vec3(-3, 6, 0)).shown_);
    EXPECT_GT(system.losRoutes_.stats().builds_, 0u);
}

// The shape BODY evaluator owns its own route cache: once bound, its tick
// reads hard routes from it, keeps every verdict of the unbound (scalar)
// tick, and still drops sources off the body's channels.
TEST(FogRevealEvalTest, ShapeEvaluatorReadsItsOwnRouteCache) {
    IRComponents::C_CanvasFogOfWar fog{IRComponents::C_CanvasFogOfWar::HeadlessInit{}};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE> system;
    system.fog_ = &fog;
    system.settings_.showThreshold_ = 0.6f;
    system.settings_.hideThreshold_ = 0.3f;
    system.settings_.staggerPeriod_ = 1;

    FrameDataFogObservers observers{};
    observers.visionCircles_[0] = IRMath::vec4(-6.0f, -6.0f, 10.0f, 0.0f);
    observers.visionCircles_[1] = IRMath::vec4(7.0f, 6.0f, 10.0f, 0.0f);
    observers.visionCircleCount_ = 2;
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 1.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 1.0f);
    std::vector<float> columns = emptyField();
    stampWall(columns, IRMath::vec2(1.0f, -20.0f), IRMath::vec2(2.0f, 20.0f), -5.0f);
    system.observers_ = observers;
    system.los_ = FogLosColumnField{columns.data()};

    const auto verdict = [&](IRMath::vec3 position, std::uint32_t channels) {
        IREntity::EntityId entity = 1;
        C_FogRevealed revealed{0.0f, false, IRComponents::FogOverride::NONE, channels};
        C_WorldTransform transform{};
        IRComponents::C_ShapeDescriptor shape{};
        transform.translation_ = position;
        system.tick(entity, revealed, transform, shape);
        return revealed.revealFactor_;
    };
    const IRMath::vec3 bodies[] = {
        IRMath::vec3(3, -6, 0),
        IRMath::vec3(3, 6, 0),
        IRMath::vec3(-3, -6, 0),
        IRMath::vec3(-3, 6, 0),
    };
    float scalar[4] = {};
    for (int i = 0; i < 4; ++i) {
        scalar[i] = verdict(bodies[i], IRComponents::kFogChannelDefault);
    }
    EXPECT_FLOAT_EQ(scalar[0], 0.0f) << "a body behind the wall from its only source revealed";
    EXPECT_FLOAT_EQ(scalar[1], 1.0f);

    system.losRoutes_.begin(system.observers_, system.los_);
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(verdict(bodies[i], IRComponents::kFogChannelDefault), scalar[i]) << "body " << i;
    }
    EXPECT_GT(system.losRoutes_.stats().builds_, 0u);
    EXPECT_FLOAT_EQ(verdict(bodies[1], 0b10u), 0.0f) << "a source off the body's channels";
}

// The BODY oracle's circle term is line-of-sight gated: an anchor behind a
// ridge from a ground-level gated source evaluates to 0, and the same anchor
// with line of sight off takes the circle term. The grid term is not gated. The
// live tick with a fog component attached reads the same snapshot, so a verdict
// that dropped the field fails here.
TEST(FogRevealEvalTest, BodyVerdictBehindARidgeFromAGatedSourceIsZero) {
    IRComponents::C_CanvasFogOfWar fog{IRComponents::C_CanvasFogOfWar::HeadlessInit{}};
    FrameDataFogObservers observers = gated(oneCircle(12.0f, 0.0f));
    std::vector<float> columns = emptyField();
    // A ridge across the source's +X view, topped well above its eye.
    stampWall(columns, IRMath::vec2(4.0f, -20.0f), IRMath::vec2(5.0f, 20.0f), -5.0f);
    const FogLosColumnField field{columns.data()};
    const IRMath::vec3 anchor(8.0f, 0.0f, 0.0f);

    const float circleTerm = IRPrefab::Fog::evalVisionReveal(observers, anchor);
    ASSERT_FLOAT_EQ(circleTerm, 1.0f);
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalReveal(fog, observers, field, anchor), 0.0f);
    FrameDataFogObservers losOff = observers;
    losOff.losSourceMask_ = 0;
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalReveal(fog, losOff, field, anchor), circleTerm);

    fog.setCell(8, 0, IRComponents::kFogStateVisible);
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalReveal(fog, observers, field, anchor), 1.0f)
        << "a VISIBLE cell reveals whatever the line of sight";
    fog.setCell(8, 0, IRComponents::kFogStateUnexplored);

    C_VoxelPool pool{IRMath::ivec3(1)};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pending_.reset(4);
    system.fogAttached_ = true;
    system.fog_ = &fog;
    system.activeCanvas_ = IREntity::kNullEntity;
    system.activePool_ = &pool;
    system.settings_.staggerPeriod_ = 1;
    system.observers_ = observers;
    system.los_ = field;
    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_VoxelSetNew voxelSet{};
    transform.translation_ = anchor;
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, 0.0f);
    system.observers_ = losOff;
    system.tick(entity, revealed, transform, voxelSet);
    EXPECT_FLOAT_EQ(revealed.revealFactor_, circleTerm);
}

TEST(FogRevealEvalTest, HysteresisAndStaggerControlEntityVerdict) {
    C_VoxelPool pool{IRMath::ivec3(1)};
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pending_.reset(4);
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
    system.pending_.reset(4);
    system.fogAttached_ = false;
    system.activeCanvas_ = IREntity::kNullEntity;

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    C_VoxelSetNew voxelSet{};
    system.tick(entity, revealed, transform, voxelSet);

    EXPECT_FLOAT_EQ(revealed.revealFactor_, 1.0f);
    EXPECT_FALSE(revealed.shown_);
    EXPECT_EQ(system.pending_.size(), 0u);
}

// The fog pass reads unexploredColor as the std140 member after
// vec4 visionCircles[8] + ivec4 tail + vec4 visionCircleHeights[8]; the default
// is opaque black, the anchor the pass hard-coded before it became a parameter.
TEST(FogRevealEvalTest, UnexploredColorDefaultsToBlackAtItsStd140Offset) {
    EXPECT_EQ(offsetof(FrameDataFogObservers, unexploredColor_), 272u);
    EXPECT_EQ(offsetof(FrameDataFogObservers, visionCircleChannels_), 416u);
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

// The ceiling is a function of the height above the observer alone: a hard
// plane at 3 hides the centre column and a rim column at the same dzUp,
// keeps the plane itself, and never cuts a sample below the observer. The
// mutation control is the additive up-cost: spelled as a cost, the hiding
// height slopes from 10 at the centre to 1 at the rim.
TEST(FogRevealEvalTest, HardCeilingHidesCentreAndRimAtTheSameHeight) {
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 3.0f);
    for (const IRMath::vec2 column :
         {IRMath::vec2(0.0f, 0.0f), IRMath::vec2(9.0f, 0.0f), IRMath::vec2(6.0f, 6.0f)}) {
        SCOPED_TRACE(testing::Message() << "column (" << column.x << ", " << column.y << ")");
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -3.0f)),
            1.0f
        ) << "the plane itself is visible";
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -3.001f)),
            0.0f
        ) << "the first sample above the plane is hidden";
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, 5.0f)),
            1.0f
        ) << "a sample below the observer is never cut";
    }

    const FrameDataFogObservers coupled = oneCircle(10.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(coupled, IRMath::vec3(0.0f, 0.0f, -9.0f)),
        1.0f
    );
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(coupled, IRMath::vec3(9.0f, 0.0f, -9.0f)), 0.0f)
        << "an additive cost hides the rim lower than the centre: not a ceiling";
}

TEST(FogRevealEvalTest, CeilingFadeIsOneAtThePlaneAndZeroExactlyAtItsEnd) {
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 2.0f, 4.0f);
    for (const IRMath::vec2 column : {IRMath::vec2(0.0f, 0.0f), IRMath::vec2(9.0f, 0.0f)}) {
        SCOPED_TRACE(testing::Message() << "column (" << column.x << ", " << column.y << ")");
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -1.0f)),
            1.0f
        );
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -2.0f)),
            1.0f
        );
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -4.0f)),
            0.5f
        );
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -6.0f)),
            0.0f
        ) << "the fade reaches zero exactly at ceiling + fade";
        EXPECT_FLOAT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(column, -6.5f)),
            0.0f
        );
    }
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::detail::ceilingVisibility(IRMath::vec4(2.0f, 4.0f, 0.0f, 0.0f), 3.0f),
        1.0f - IRMath::smoothstep(2.0f, 6.0f, 3.0f)
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::detail::ceilingVisibility(IRMath::vec4(-1.0f, 4.0f, 0.0f, 0.0f), 100.0f),
        1.0f
    ) << "a negative height is off whatever the fade";
}

// The additive height cost and the ceiling are independent terms of one
// source: the cost keeps its curve with the ceiling off, and both apply as a
// product where both are partial.
TEST(FogRevealEvalTest, CeilingMultipliesTheAdditiveHeightCostCurve) {
    const FrameDataFogObservers costOnly = oneCircle(10.0f, 2.0f, 0.0f, 0.5f);
    const float costReveal =
        IRPrefab::Fog::evalVisionReveal(costOnly, IRMath::vec3(0.0f, 0.0f, -18.0f));
    EXPECT_NEAR(costReveal, 0.84375f, 1e-6f);

    FrameDataFogObservers both = costOnly;
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(both, 0, 10.0f, 16.0f);
    EXPECT_NEAR(
        IRPrefab::Fog::evalVisionReveal(both, IRMath::vec3(0.0f, 0.0f, -18.0f)),
        costReveal * 0.5f,
        1e-6f
    );
    EXPECT_FLOAT_EQ(IRPrefab::Fog::evalVisionReveal(both, IRMath::vec3(0.0f, 0.0f, -8.0f)), 1.0f)
        << "under the ceiling the cost curve alone decides";
}

// Each ceiling scales its own source before the maximum: a source cut by its
// ceiling never lowers what another source or a VISIBLE cell reveals. The
// mutation control is a post-maximum ceiling, which would cut both.
TEST(FogRevealEvalTest, CeilingScalesItsOwnSourceBeforeTheMaximum) {
    FrameDataFogObservers observers{};
    IRComponents::C_CanvasFogOfWar::addVisionCircle(
        observers,
        0.0f,
        0.0f,
        10.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    );
    IRComponents::C_CanvasFogOfWar::addVisionCircle(
        observers,
        0.0f,
        0.0f,
        10.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    );
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 1.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(0.0f, 0.0f, -5.0f)),
        1.0f
    ) << "the unceilinged source still reveals above the other's ceiling";
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 1, 3.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(0.0f, 0.0f, -5.0f)),
        0.0f
    );
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, IRMath::vec3(0.0f, 0.0f, -2.0f)),
        1.0f
    );
    const FogLosColumnField noLos{};
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            noLos,
            IRComponents::kFogStateVisible,
            IRMath::vec3(0.0f, 0.0f, -50.0f)
        ),
        1.0f
    ) << "no ceiling scales the grid term";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            noLos,
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(0.0f, 0.0f, -5.0f)
        ),
        0.0f
    ) << "the BODY verdict takes the ceiling at the anchor";
}

TEST(FogRevealEvalTest, LineOfSightAndCeilingFactorsMultiplyPerSource) {
    FrameDataFogObservers observers = gated(oneCircle(10.0f, 0.0f));
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 2.0f, 4.0f);
    const IRMath::vec3 sample(0.0f, 0.0f, -4.0f);
    const auto halfVisible = [](int) { return 0.5f; };
    EXPECT_NEAR(
        IRPrefab::Fog::detail::evalGatedVisionReveal(
            observers,
            sample,
            IRComponents::kFogChannelDefault,
            halfVisible
        ),
        0.25f,
        1e-6f
    ) << "ceiling 0.5 times line of sight 0.5";
    EXPECT_NEAR(
        IRPrefab::Fog::detail::evalGatedVisionReveal(
            observers,
            sample,
            IRComponents::kFogChannelDefault,
            [](int) { return 1.0f; }
        ),
        0.5f,
        1e-6f
    );
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(
        observers,
        0,
        IRComponents::kFogVisionCeilingOff
    );
    EXPECT_NEAR(
        IRPrefab::Fog::detail::evalGatedVisionReveal(
            observers,
            sample,
            IRComponents::kFogChannelDefault,
            halfVisible
        ),
        0.5f,
        1e-6f
    );
}

// The FIELD treatment styles a sample only when a partial contribution wins:
// a VISIBLE cell, an explored cell above the contribution, or a fully
// visible competing source suppresses it; density 0 retains every voxel at
// the band's full weight; density 1 dissolves about half the voxels at a
// surface factor of one half, and a dissolved voxel falls back to the grid
// term rather than to a lower partial state.
TEST(FogRevealEvalTest, TreatmentStylesOnlyARetainedPartialWinner) {
    using IRPrefab::Fog::RevealSurfaceSample;
    const FogLosColumnField noLos{};
    FrameDataFogObservers observers = oneCircle(10.0f, 0.0f);
    IRComponents::C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 2.0f, 4.0f);
    const IRMath::vec3 sample(0.2f, 0.3f, -4.0f);

    RevealSurfaceSample off = IRPrefab::Fog::evalRevealSurface(
        observers,
        noLos,
        IRComponents::kFogStateUnexplored,
        sample
    );
    EXPECT_FLOAT_EQ(off.state_, 0.5f);
    EXPECT_FLOAT_EQ(off.styledBand_, 0.0f) << "treatment off styles nothing";

    IRComponents::C_CanvasFogOfWar::setRevealSurfaceTreatment(observers, 0.0f);
    RevealSurfaceSample retained = IRPrefab::Fog::evalRevealSurface(
        observers,
        noLos,
        IRComponents::kFogStateUnexplored,
        sample
    );
    EXPECT_FLOAT_EQ(retained.state_, 0.5f);
    EXPECT_FLOAT_EQ(retained.styledBand_, 1.0f) << "the band peaks at the midpoint";
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalRevealSurface(observers, noLos, IRComponents::kFogStateVisible, sample)
            .styledBand_,
        0.0f
    ) << "a VISIBLE cell suppresses the treatment";
    const RevealSurfaceSample explored =
        IRPrefab::Fog::evalRevealSurface(observers, noLos, IRComponents::kFogStateExplored, sample);
    EXPECT_FLOAT_EQ(explored.state_, 128.0f / 255.0f) << "the explored term wins";
    EXPECT_FLOAT_EQ(explored.styledBand_, 0.0f);
    EXPECT_FLOAT_EQ(
        IRPrefab::Fog::evalRevealSurface(
            observers,
            noLos,
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(0.2f, 0.3f, 1.0f)
        )
            .styledBand_,
        0.0f
    ) << "a sample below the observer is fully visible, not a band";

    FrameDataFogObservers overlapped = observers;
    IRComponents::C_CanvasFogOfWar::addVisionCircle(
        overlapped,
        0.0f,
        0.0f,
        10.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        0.0f
    );
    const RevealSurfaceSample covered = IRPrefab::Fog::evalRevealSurface(
        overlapped,
        noLos,
        IRComponents::kFogStateUnexplored,
        sample
    );
    EXPECT_FLOAT_EQ(covered.state_, 1.0f);
    EXPECT_FLOAT_EQ(covered.styledBand_, 0.0f) << "a fully visible competitor suppresses it";

    IRComponents::C_CanvasFogOfWar::setRevealSurfaceTreatment(observers, 1.0f);
    int retainedCount = 0;
    int dissolvedCount = 0;
    for (int y = -8; y < 8; ++y) {
        for (int x = -8; x < 8; ++x) {
            const RevealSurfaceSample voxel = IRPrefab::Fog::evalRevealSurface(
                observers,
                noLos,
                IRComponents::kFogStateUnexplored,
                IRMath::vec3(static_cast<float>(x) + 0.25f, static_cast<float>(y) - 0.25f, -4.0f)
            );
            if (voxel.styledBand_ > 0.0f) {
                EXPECT_FLOAT_EQ(voxel.state_, 0.5f);
                ++retainedCount;
            } else {
                EXPECT_FLOAT_EQ(voxel.state_, 0.0f) << "a dissolved voxel falls back to the grid";
                ++dissolvedCount;
            }
        }
    }
    EXPECT_GT(retainedCount, 256 * 3 / 10);
    EXPECT_GT(dissolvedCount, 256 * 3 / 10);
}

TEST(FogRevealEvalTest, RevealSurfaceHashIsVoxelStableAndUniform) {
    using IRPrefab::Fog::detail::revealSurfaceHash01;
    EXPECT_EQ(
        revealSurfaceHash01(IRMath::ivec3(3, -7, 12)),
        revealSurfaceHash01(IRMath::ivec3(3, -7, 12))
    );
    double sum = 0.0;
    int distinctFromOrigin = 0;
    const float origin = revealSurfaceHash01(IRMath::ivec3(0));
    for (int z = -4; z < 4; ++z) {
        for (int y = -16; y < 16; ++y) {
            for (int x = -16; x < 16; ++x) {
                const float hash = revealSurfaceHash01(IRMath::ivec3(x, y, z));
                EXPECT_GE(hash, 0.0f);
                EXPECT_LT(hash, 1.0f);
                sum += hash;
                distinctFromOrigin += hash != origin ? 1 : 0;
            }
        }
    }
    EXPECT_NEAR(sum / (8.0 * 32.0 * 32.0), 0.5, 0.03);
    EXPECT_GT(distinctFromOrigin, 8 * 32 * 32 - 16);
}

// The appended lanes sit after the channel masks (448 and 576 of 592 bytes)
// so no earlier offset moves, and every default is the disabled state.
TEST(FogRevealEvalTest, CeilingAndTreatmentLanesAppendAfterTheChannelMasks) {
    EXPECT_EQ(offsetof(FrameDataFogObservers, visionCircleCeilings_), 448u);
    EXPECT_EQ(offsetof(FrameDataFogObservers, revealSurfaceTreatment_), 576u);
    EXPECT_EQ(sizeof(FrameDataFogObservers), 592u);
    const FrameDataFogObservers observers{};
    for (int source = 0; source < IRComponents::kMaxFogVisionCircles; ++source) {
        EXPECT_EQ(
            observers.visionCircleCeilings_[source],
            IRMath::vec4(IRComponents::kFogVisionCeilingOff, 0.0f, 0.0f, 0.0f)
        );
        EXPECT_FALSE(observers.ceilingEnabled(source));
    }
    EXPECT_EQ(
        observers.revealSurfaceTreatment_,
        IRMath::vec4(0.0f, 0.0f, IRComponents::kFogCutTone, 0.0f)
    );
    EXPECT_FALSE(observers.revealSurfaceTreatmentEnabled());
}

TEST(FogRevealEvalTest, CeilingSlotsResetWithTheSourceAndRefuseUnregisteredSlots) {
    using IRComponents::C_CanvasFogOfWar;
    FrameDataFogObservers observers{};
    ASSERT_EQ(
        C_CanvasFogOfWar::
            addVisionCircle(observers, 0.0f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        0
    );
    C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 3.0f, -2.0f);
    EXPECT_EQ(observers.visionCircleCeilings_[0], IRMath::vec4(3.0f, 0.0f, 0.0f, 0.0f))
        << "a negative fade clamps to the hard plane";
    C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, -5.0f, 2.0f);
    EXPECT_EQ(
        observers.visionCircleCeilings_[0],
        IRMath::vec4(IRComponents::kFogVisionCeilingOff, 0.0f, 0.0f, 0.0f)
    ) << "any negative height disables the slot";
    C_CanvasFogOfWar::setVisionCircleCeiling(observers, 0, 2.5f, 1.5f);
    const IRComponents::FogVisionCeiling stored =
        C_CanvasFogOfWar::visionCircleCeiling(observers, 0);
    EXPECT_TRUE(stored.enabled());
    EXPECT_FLOAT_EQ(stored.ceilingHeight_, 2.5f);
    EXPECT_FLOAT_EQ(stored.fadeHeight_, 1.5f);

    ASSERT_EQ(
        C_CanvasFogOfWar::
            addVisionCircle(observers, 1.0f, 0.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f),
        1
    );
    EXPECT_FALSE(observers.ceilingEnabled(1)) << "a new slot starts disabled";
    EXPECT_THROW(C_CanvasFogOfWar::setVisionCircleCeiling(observers, 2, 1.0f), std::runtime_error);
    EXPECT_THROW(C_CanvasFogOfWar::setVisionCircleCeiling(observers, -1, 1.0f), std::runtime_error);
    EXPECT_THROW(C_CanvasFogOfWar::visionCircleCeiling(observers, 2), std::runtime_error);
    EXPECT_TRUE(observers.ceilingEnabled(0));
    EXPECT_FALSE(observers.ceilingEnabled(2)) << "a refused slot is untouched";

    C_CanvasFogOfWar::clearVisionCircles(observers);
    EXPECT_FALSE(observers.ceilingEnabled(0)) << "clearing the sources resets every ceiling";

    C_CanvasFogOfWar::setRevealSurfaceTreatment(observers, 1.5f, -1.0f);
    EXPECT_EQ(observers.revealSurfaceTreatment_, IRMath::vec4(1.0f, 1.0f, 0.0f, 0.0f))
        << "the style is clamped to [0, 1]";
    C_CanvasFogOfWar::clearRevealSurfaceTreatment(observers);
    EXPECT_FALSE(observers.revealSurfaceTreatmentEnabled());
    EXPECT_FLOAT_EQ(observers.dissolveDensity(), 1.0f) << "clearing keeps the stored style";
    C_CanvasFogOfWar::setRevealSurfaceTreatment(observers, 0.25f);
    const IRComponents::FogRevealSurfaceTreatment treatment =
        C_CanvasFogOfWar::revealSurfaceTreatment(observers);
    EXPECT_TRUE(treatment.enabled_);
    EXPECT_FLOAT_EQ(treatment.dissolveDensity_, 0.25f);
    EXPECT_FLOAT_EQ(treatment.capTone_, IRComponents::kFogCutTone);
}

// The CPU colour twins of the fog pass: the state curve and the shared cap
// blend, which the demo probes compare readbacks against.
TEST(FogRevealEvalTest, RevealColourMirrorsFollowTheStateCurveAndTheCapBlend) {
    using IRPrefab::Fog::detail::cutCapBlend;
    using IRPrefab::Fog::detail::revealStateColor;
    const IRMath::vec3 source(1.0f, 0.5f, 0.0f);
    const IRMath::vec3 unexplored(1.0f, 0.0f, 1.0f);
    EXPECT_EQ(revealStateColor(0.0f, source, unexplored), unexplored);
    EXPECT_EQ(revealStateColor(1.0f, source, unexplored), source);
    const float luminance = 0.299f * 1.0f + 0.587f * 0.5f;
    const IRMath::vec3 explored = IRMath::vec3(luminance) * 0.4f;
    EXPECT_NEAR(revealStateColor(128.0f / 255.0f, source, unexplored).x, explored.x, 1e-6f);
    EXPECT_EQ(cutCapBlend(unexplored, source, 0.5f, 0.0f, 0.0f), unexplored)
        << "weight 0 leaves the colour";
    EXPECT_EQ(cutCapBlend(unexplored, source, 0.5f, 0.0f, 1.0f), source * 0.5f)
        << "weight 1 at state 0 is the toned source";
    EXPECT_EQ(cutCapBlend(unexplored, source, 0.5f, 1.0f, 1.0f), source) << "state 1 is untoned";
}

TEST(FogRevealEvalTest, ActiveMaskHideAndRestoreAreAlphaPreserving) {
    C_VoxelPool pool{IRMath::ivec3(4, 1, 1)};
    auto allocation = pool.allocateVoxels(4);
    C_VoxelSetNew voxelSet{};
    voxelSet.voxelStartIdx_ = 0;
    voxelSet.numVoxels_ = 4;
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pending_.reset(4);
    allocation.voxels_[0].color_.alpha_ = 255;
    allocation.voxels_[1].color_.alpha_ = 0;
    allocation.voxels_[2].color_.alpha_ = 255;
    allocation.voxels_[3].color_.alpha_ = 255;
    pool.resyncActiveMaskFromColors(0, 4);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0xdu);

    system.pending_.push({&voxelSet, &pool, false});
    system.endTick();
    EXPECT_FALSE(voxelSet.visible_);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0u);
    EXPECT_EQ(allocation.voxels_[0].color_.alpha_, 255);
    EXPECT_EQ(allocation.voxels_[1].color_.alpha_, 0);

    system.pending_.reset(1);
    system.pending_.push({&voxelSet, &pool, true});
    system.endTick();
    EXPECT_TRUE(voxelSet.visible_);
    EXPECT_EQ(pool.getActiveMask()[0] & 0xfu, 0xdu);
}

// A shown body's carrier follows its verdict: the 8-bit factor is rewritten
// on every voxel only when it moves, and the frame's re-stamp count is the
// number of voxels rewritten.
TEST(FogRevealEvalTest, ShownBodyRestampsItsCarrierOnlyWhenTheFactorMoves) {
    using IRComponents::VoxelReserved::kFogBody;
    using IRComponents::VoxelReserved::kFogBodyFactorShift;
    using IRComponents::VoxelReserved::kFogCarrierMask;
    C_VoxelPool pool{IRMath::ivec3(4, 1, 1)};
    auto allocation = pool.allocateVoxels(4);
    C_VoxelSetNew voxelSet{};
    voxelSet.voxelStartIdx_ = 0;
    voxelSet.numVoxels_ = 4;
    voxelSet.voxels_ = allocation.voxels_;
    pool.resyncActiveMaskFromColors(0, 4);

    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.pending_.reset(4);
    system.restampedByWorker_.assign(1, 0u);
    system.fogAttached_ = true;
    system.activePool_ = &pool;
    system.observers_ = oneCircle(10.0f, 4.0f);

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    C_WorldTransform transform{};
    transform.translation_ = IRMath::vec3(0);
    system.tick(entity, revealed, transform, voxelSet);
    system.endTick();
    ASSERT_TRUE(revealed.shown_);
    for (const IRComponents::C_Voxel &voxel : voxelSet.voxels_) {
        EXPECT_EQ(voxel.reserved_ & kFogCarrierMask, kFogBody | (255u << kFogBodyFactorShift));
    }
    EXPECT_EQ(system.restampedVoxelsLastFrame_, 4u);

    system.restampedByWorker_.assign(1, 0u);
    system.tick(entity, revealed, transform, voxelSet);
    system.endTick();
    EXPECT_EQ(system.restampedVoxelsLastFrame_, 0u) << "an unchanged factor is not rewritten";

    transform.translation_ = IRMath::vec3(8.0f, 0.0f, 0.0f);
    system.restampedByWorker_.assign(1, 0u);
    system.tick(entity, revealed, transform, voxelSet);
    system.endTick();
    const std::uint32_t expected = IRPrefab::Fog::quantizeRevealFactor(revealed.revealFactor_);
    ASSERT_GT(revealed.revealFactor_, system.settings_.hideThreshold_);
    ASSERT_LT(expected, 255u);
    for (const IRComponents::C_Voxel &voxel : voxelSet.voxels_) {
        EXPECT_EQ(voxel.reserved_ & kFogCarrierMask, kFogBody | (expected << kFogBodyFactorShift));
    }
    EXPECT_EQ(system.restampedVoxelsLastFrame_, 4u);
}

// Eight sources over a wall, a tower and lowered ground: hard-gated sources
// (one on a lattice corner), a soft one with partial reveals, ungated ones,
// soft disc edges and a height cost. The prepared reveal must equal the scalar
// one bit for bit across hidden, partial and visible bodies.
FrameDataFogObservers mixedSources(IRMath::vec2 shift = IRMath::vec2(0.0f)) {
    FrameDataFogObservers observers{};
    const struct {
        IRMath::vec4 circle_;
        float zCostUp_;
        float eyeHeight_;
        float softness_;
    } sources[IRComponents::kMaxFogVisionCircles] = {
        {IRMath::vec4(-6.0f, -6.0f, 12.0f, 0.0f), 0.0f, 1.0f, 0.0f},
        {IRMath::vec4(7.0f, 6.0f, 12.0f, 1.5f), 0.0f, 1.0f, 0.0f},
        {IRMath::vec4(0.0f, 9.0f, 9.0f, 0.0f), 0.5f, 2.0f, 0.0f},
        {IRMath::vec4(-8.0f, 7.5f, 10.0f, 0.0f), 0.0f, 1.5f, 0.75f},
        {IRMath::vec4(14.0f, -12.0f, 4.0f, 2.0f), 0.0f, -1.0f, 0.0f},
        {IRMath::vec4(5.5f, -7.0f, 11.0f, 0.0f), 0.25f, 0.5f, 0.0f},
        {IRMath::vec4(-15.0f, 0.0f, 3.0f, 0.0f), 0.0f, -1.0f, 0.0f},
        {IRMath::vec4(-2.0f, -11.5f, 13.0f, 0.5f), 0.0f, 3.0f, 0.0f},
    };
    for (const auto &source : sources) {
        const int slot = IRComponents::C_CanvasFogOfWar::addVisionCircle(
            observers,
            source.circle_.x + shift.x,
            source.circle_.y + shift.y,
            source.circle_.z,
            source.circle_.w,
            0.0f,
            source.zCostUp_,
            IRComponents::kFogVisionZCostMirrorUp,
            0.0f
        );
        if (source.eyeHeight_ >= 0.0f) {
            IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(
                observers,
                slot,
                source.eyeHeight_,
                source.softness_
            );
        }
    }
    return observers;
}

std::vector<float> mixedField(float wallX) {
    std::vector<float> columns(IRComponents::kFogLosFieldFloatCount, 1.0f);
    IRPrefab::Fog::stampLosBox(
        columns,
        kWorldFieldMin,
        IRMath::vec2(wallX, -20.0f),
        IRMath::vec2(wallX + 1.0f, 20.0f),
        -5.0f
    );
    IRPrefab::Fog::stampLosBox(
        columns,
        kWorldFieldMin,
        IRMath::vec2(-4.0f, 2.0f),
        IRMath::vec2(-3.0f, 3.5f),
        -9.0f
    );
    IRPrefab::Fog::buildLosPyramid(columns);
    return columns;
}

// Bodies in Z-major order, the layout the perf fixture spawns: every XY of a
// `side`-wide lattice repeats once per layer, `layers` layers apart.
std::vector<IRMath::vec3> stackedBodies(int side, int layers) {
    std::vector<IRMath::vec3> bodies;
    for (int z = 0; z < layers; ++z) {
        for (int y = 0; y < side; ++y) {
            for (int x = 0; x < side; ++x) {
                bodies.emplace_back(
                    static_cast<float>(x - side / 2) + 0.5f,
                    static_cast<float>(y - side / 2) + 0.5f,
                    0.75f - 0.25f * static_cast<float>(z)
                );
            }
        }
    }
    return bodies;
}

struct MarchCensus {
    std::size_t marches_ = 0;
    std::size_t routes_ = 0;
};

// What the scalar reveal marches through hard-gated sources over @p bodies:
// every such query and the distinct (source, exact XY) routes among them.
MarchCensus scalarMarches(
    const FrameDataFogObservers &observers,
    const FogLosColumnField &field,
    const std::vector<IRMath::vec3> &bodies
) {
    std::set<std::tuple<int, float, float>> routes;
    MarchCensus census;
    for (const IRMath::vec3 &body : bodies) {
        IRPrefab::Fog::detail::evalGatedVisionReveal(
            observers,
            body,
            IRComponents::kFogChannelDefault,
            [&](int source) {
                if (observers.losSoftness(source) <= IRComponents::kFogLosHardGate) {
                    ++census.marches_;
                    routes.emplace(source, body.x, body.y);
                }
                return IRPrefab::Fog::losVisibility(field, observers, source, body);
            }
        );
    }
    census.routes_ = routes.size();
    return census;
}

TEST(FogRevealEvalTest, PreparedRevealMatchesTheScalarComposition) {
    std::vector<float> columns = mixedField(1.0f);
    FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers = mixedSources();
    IRPrefab::Fog::LosHardRouteCache routes;
    routes.begin(observers, field);
    int hidden = 0;
    int partial = 0;
    int visible = 0;
    std::vector<float> first;
    for (float z = -6.0f; z <= 3.0f; z += 0.75f) {
        for (float y = -20.0f; y <= 20.0f; y += 0.5f) {
            for (float x = -20.0f; x <= 20.0f; x += 0.5f) {
                const IRMath::vec3 position(x + 0.25f, y, z);
                const float scalar = IRPrefab::Fog::evalVisionReveal(observers, field, position);
                ASSERT_EQ(
                    IRPrefab::Fog::evalVisionReveal(observers, field, routes, position),
                    scalar
                ) << "("
                  << position.x << ", " << y << ", " << z << ")";
                first.push_back(scalar);
                ++(scalar == 0.0f ? hidden : (scalar == 1.0f ? visible : partial));
            }
        }
    }
    EXPECT_GT(hidden, 100);
    EXPECT_GT(partial, 100);
    EXPECT_GT(visible, 100);
    EXPECT_GT(routes.stats().builds_, 0u);
    EXPECT_EQ(routes.laneCapacity(3), 0u) << "a soft source never takes a lane";
    EXPECT_EQ(routes.laneCapacity(4), 0u) << "an ungated source never takes a lane";

    // A new publication: the wall moved and the sources shifted. No route of
    // the last one may answer, so verdicts that changed follow the new pair.
    columns = mixedField(-2.5f);
    field = FogLosColumnField{columns.data()};
    observers = mixedSources(IRMath::vec2(0.5f, -0.25f));
    routes.begin(observers, field);
    EXPECT_EQ(routes.stats().builds_, 0u);
    int changed = 0;
    std::size_t index = 0;
    for (float z = -6.0f; z <= 3.0f; z += 0.75f) {
        for (float y = -20.0f; y <= 20.0f; y += 0.5f) {
            for (float x = -20.0f; x <= 20.0f; x += 0.5f) {
                const IRMath::vec3 position(x + 0.25f, y, z);
                const float scalar = IRPrefab::Fog::evalVisionReveal(observers, field, position);
                ASSERT_EQ(
                    IRPrefab::Fog::evalVisionReveal(observers, field, routes, position),
                    scalar
                ) << "after the new publication at ("
                  << position.x << ", " << y << ", " << z << ")";
                changed += scalar != first[index++] ? 1 : 0;
            }
        }
    }
    EXPECT_GT(changed, 100);
}

// Sources split across two channels: a body's mask drops the other channel's
// sources before any route is read, as the scalar reveal drops them, and the
// routed BODY kernel keeps the channel-blind grid term.
TEST(FogRevealEvalTest, PreparedRevealHonoursChannelMasks) {
    const std::vector<float> columns = mixedField(1.0f);
    const FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers = mixedSources();
    for (int source = 0; source < IRComponents::kMaxFogVisionCircles; ++source) {
        observers.visionCircleChannels_[source / 4][source % 4] = source % 2 == 0 ? 0b01u : 0b10u;
    }
    IRPrefab::Fog::LosHardRouteCache routes;
    routes.begin(observers, field);
    int masked = 0;
    for (const std::uint32_t channels : {0b01u, 0b10u, 0b11u}) {
        for (float y = -20.0f; y <= 20.0f; y += 0.5f) {
            for (float x = -20.0f; x <= 20.0f; x += 0.5f) {
                const IRMath::vec3 position(x + 0.25f, y, 0.0f);
                const float scalar =
                    IRPrefab::Fog::evalVisionReveal(observers, field, position, channels);
                ASSERT_EQ(
                    IRPrefab::Fog::evalVisionReveal(observers, field, routes, position, channels),
                    scalar
                ) << "channels "
                  << channels << " at (" << position.x << ", " << y << ")";
                masked +=
                    scalar != IRPrefab::Fog::evalVisionReveal(observers, field, position) ? 1 : 0;
            }
        }
    }
    EXPECT_GT(masked, 100);
    EXPECT_GT(routes.stats().builds_, 0u);

    const IRMath::vec3 farCell(19.25f, 19.0f, 0.0f);
    EXPECT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            field,
            routes,
            IRComponents::kFogStateVisible,
            farCell,
            0u
        ),
        1.0f
    );
    EXPECT_EQ(
        IRPrefab::Fog::evalReveal(
            observers,
            field,
            routes,
            IRComponents::kFogStateUnexplored,
            IRMath::vec3(-6.0f, -5.0f, 0.0f),
            0u
        ),
        0.0f
    );
}

// An unpublished field and an ungated set take no routes; a gated source
// reveals nothing through an unpublished field, as the scalar reveal reads.
TEST(FogRevealEvalTest, PreparedRevealTakesNoRoutesWithoutAHardGate) {
    const std::vector<float> columns = mixedField(1.0f);
    const FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers = mixedSources();
    IRPrefab::Fog::LosHardRouteCache routes;

    routes.begin(observers, FogLosColumnField{});
    const IRMath::vec3 nearSource(-6.0f, -5.0f, 0.0f);
    EXPECT_EQ(
        IRPrefab::Fog::evalVisionReveal(observers, FogLosColumnField{}, routes, nearSource),
        IRPrefab::Fog::evalVisionReveal(observers, FogLosColumnField{}, nearSource)
    );
    observers.losSourceMask_ = 0;
    routes.begin(observers, field);
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        const IRMath::vec3 position(x, 0.5f, 0.0f);
        EXPECT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, field, routes, position),
            IRPrefab::Fog::evalVisionReveal(observers, field, position)
        );
    }
    EXPECT_EQ(routes.stats().builds_, 0u);
    for (int source = 0; source < IRComponents::kMaxFogVisionCircles; ++source) {
        EXPECT_EQ(routes.laneCapacity(source), 0u) << "source " << source;
    }
}

// The perf fixture's shape under the real fan-out: a stacked 24 x 24 lattice,
// 64 layers deep in Z-major order, dispatched through IRJob::parallelFor at
// the production grain on a two-worker pool. Every distinct (source, exact
// XY) route is built exactly once across the workers, the rest of the
// queries hit a built route, and every reveal is the scalar one. A new
// begin forgets the routes and builds them again.
class FogRouteFanOutTest : public testing::Test {
  protected:
    void SetUp() override {
        m_jobs = std::make_unique<IRJob::JobManager>(2);
    }

    void TearDown() override {
        m_jobs.reset();
    }

    std::vector<float> revealAll(const std::vector<IRMath::vec3> &bodies) {
        std::vector<float> reveals(bodies.size());
        IRJob::parallelFor(
            0,
            static_cast<int>(bodies.size()),
            IRSystem::kDefaultGrainSize,
            [&](int begin, int end) {
                for (int i = begin; i < end; ++i) {
                    reveals[static_cast<std::size_t>(i)] = IRPrefab::Fog::evalVisionReveal(
                        m_observers,
                        m_field,
                        m_routes,
                        bodies[static_cast<std::size_t>(i)]
                    );
                }
            }
        );
        return reveals;
    }

    std::unique_ptr<IRJob::JobManager> m_jobs;
    std::vector<float> m_columns = mixedField(1.0f);
    FogLosColumnField m_field{m_columns.data()};
    FrameDataFogObservers m_observers = mixedSources();
    IRPrefab::Fog::LosHardRouteCache m_routes;
};

TEST_F(FogRouteFanOutTest, EachRouteIsBuiltOnceAcrossWorkers) {
    ASSERT_EQ(IRJob::workerCount(), 2);
    const std::vector<IRMath::vec3> bodies = stackedBodies(24, 64);
    const MarchCensus census = scalarMarches(m_observers, m_field, bodies);
    ASSERT_GT(census.routes_, 0u);

    for (int tick = 0; tick < 2; ++tick) {
        m_routes.begin(m_observers, m_field);
        const std::vector<float> reveals = revealAll(bodies);
        for (std::size_t i = 0; i < bodies.size(); ++i) {
            ASSERT_EQ(reveals[i], IRPrefab::Fog::evalVisionReveal(m_observers, m_field, bodies[i]))
                << "body " << i << " tick " << tick;
        }
        const IRPrefab::Fog::LosHardRouteCache::Stats stats = m_routes.stats();
        EXPECT_EQ(stats.capacityFallbacks_, 0u);
        EXPECT_LE(stats.bandFallbacks_, census.marches_ / 1000);
        EXPECT_EQ(stats.builds_, census.routes_) << "tick " << tick;
        const std::size_t hits = census.marches_ - stats.builds_ - stats.waits_;
        EXPECT_GT(hits, census.routes_ * 8) << "a 64-deep stack reads each route many times";
    }
}

// A reader that reaches a route while its builder still holds it open waits
// for it rather than building its own: the test seam parks the first builder
// until another worker has reported the wait, with a bounded timeout.
TEST_F(FogRouteFanOutTest, AReaderWaitsForTheElectedBuilder) {
    struct Hold {
        IRPrefab::Fog::LosHardRouteCache *routes_ = nullptr;
        std::atomic<bool> armed_{true};
        std::atomic<bool> released_{false};
    } hold;
    hold.routes_ = &m_routes;
    m_routes.setBuildHookForTesting(
        [](void *context) {
            Hold &hold = *static_cast<Hold *>(context);
            if (!hold.armed_.exchange(false)) {
                return;
            }
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
            while (hold.routes_->stats().waits_ == 0u &&
                   std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
            }
            hold.released_ = hold.routes_->stats().waits_ > 0u;
        },
        &hold
    );
    const std::vector<IRMath::vec3> bodies = stackedBodies(24, 64);
    m_routes.begin(m_observers, m_field);
    const std::vector<float> reveals = revealAll(bodies);
    m_routes.setBuildHookForTesting(nullptr, nullptr);

    EXPECT_TRUE(hold.released_) << "no worker reached the held route within the timeout";
    EXPECT_GT(m_routes.stats().waits_, 0u);
    for (std::size_t i = 0; i < bodies.size(); ++i) {
        ASSERT_EQ(reveals[i], IRPrefab::Fog::evalVisionReveal(m_observers, m_field, bodies[i]));
    }
}

// More distinct routes in one lane than its probe can place: the overflow
// marches, exactly, and the next begin grows the lane to the demand. A pass
// at the grown capacity then allocates nothing.
TEST(FogRevealEvalTest, LanePressureMarchesThenGrowsAndStaysAllocationFree) {
    const std::vector<float> columns = mixedField(1.0f);
    const FogLosColumnField field{columns.data()};
    FrameDataFogObservers observers{};
    observers.visionCircles_[0] = IRMath::vec4(0.0f, 0.0f, 60.0f, 0.0f);
    observers.visionCircleCount_ = 1;
    IRComponents::C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 1.0f);
    std::vector<IRMath::vec3> bodies;
    for (int y = -30; y < 30; ++y) {
        for (int x = -30; x < 30; ++x) {
            bodies.emplace_back(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, 0.0f);
        }
    }
    IRPrefab::Fog::LosHardRouteCache routes;
    routes.begin(observers, field);
    const std::uint32_t initial = routes.laneCapacity(0);
    ASSERT_LT(initial, bodies.size());
    for (const IRMath::vec3 &body : bodies) {
        ASSERT_EQ(
            IRPrefab::Fog::evalVisionReveal(observers, field, routes, body),
            IRPrefab::Fog::evalVisionReveal(observers, field, body)
        );
    }
    EXPECT_GT(routes.stats().capacityFallbacks_, 0u);

    routes.begin(observers, field);
    EXPECT_GT(routes.laneCapacity(0), initial);
    for (const IRMath::vec3 &body : bodies) {
        IRPrefab::Fog::evalVisionReveal(observers, field, routes, body);
    }
    EXPECT_EQ(routes.stats().capacityFallbacks_, 0u);
    EXPECT_EQ(routes.stats().builds_, bodies.size());

    const std::uint32_t grown = routes.laneCapacity(0);
    float sum = 0.0f;
    const IRTest::AllocationCounter counter;
    routes.begin(observers, field);
    for (const IRMath::vec3 &body : bodies) {
        sum += IRPrefab::Fog::evalVisionReveal(observers, field, routes, body);
    }
    EXPECT_EQ(counter.allocations(), 0u);
    EXPECT_EQ(routes.laneCapacity(0), grown);
    EXPECT_GT(sum, 0.0f);
}

} // namespace
