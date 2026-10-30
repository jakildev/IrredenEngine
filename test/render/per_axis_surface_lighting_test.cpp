#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>

#include <irreden/render/per_axis_surface_lighting.hpp>

// The per-axis surface lighting switch lives on the C_PerAxisSurfaceLighting
// singleton: off until set, readable back, and kept across a scene
// transition like every singleton row. On a fog pipeline the same switch
// admits the surface routes, and `surfaceFogRoute` decides whether the
// composite fogs them or fog keeps lighting in compute.

namespace {

class PerAxisSurfaceLightingTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(PerAxisSurfaceLightingTest, OffUntilEnabledAndReadsBack) {
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
    ASSERT_NE(IREntity::singletonOrNull<IRComponents::C_PerAxisSurfaceLighting>(), nullptr);

    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());

    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

TEST_F(PerAxisSurfaceLightingTest, SurvivesSceneTransition) {
    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);

    IREntity::resetGameplay();

    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

using IRComponents::C_CanvasFogOfWar;
using IRComponents::FrameDataFogObservers;
using IRPrefab::PerAxisSurfaceLighting::SurfaceFogRoute;
using IRPrefab::PerAxisSurfaceLighting::surfaceFogRoute;

FrameDataFogObservers observersWithSources(int count) {
    FrameDataFogObservers observers;
    for (int source = 0; source < count; ++source) {
        C_CanvasFogOfWar::addVisionCircle(
            observers,
            static_cast<float>(source * 10),
            0.0f,
            6.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            0.0f
        );
    }
    return observers;
}

TEST(SurfaceFogRoute, PipelineWithoutAFogPassRunsUnfogged) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(surfaceFogRoute(false, nullptr, false, false), SurfaceFogRoute::NONE);
    EXPECT_EQ(surfaceFogRoute(false, &observers, true, true), SurfaceFogRoute::NONE);
}

TEST(SurfaceFogRoute, FogPipelineKeepsComputeUntilOptedIn) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(surfaceFogRoute(true, &observers, false, false), SurfaceFogRoute::FALLBACK);
    // A main canvas without fog on a fog pipeline is still a default-off frame.
    EXPECT_EQ(surfaceFogRoute(true, nullptr, false, false), SurfaceFogRoute::FALLBACK);
}

TEST(SurfaceFogRoute, MixedCanvasesLeaveAFoglessMainCanvasUnfogged) {
    // Fog lives only on other canvases: nothing touches the main canvas.
    EXPECT_EQ(surfaceFogRoute(true, nullptr, true, false), SurfaceFogRoute::NONE);
}

TEST(SurfaceFogRoute, UngatedSourcesCompose) {
    for (int count : {0, 1, IRComponents::kMaxFogVisionCircles}) {
        const FrameDataFogObservers observers = observersWithSources(count);
        EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::COMPOSE)
            << count << " sources";
    }
}

TEST(SurfaceFogRoute, AnyGatedSourceFallsBack) {
    FrameDataFogObservers observers = observersWithSources(IRComponents::kMaxFogVisionCircles);
    for (int source = 0; source < IRComponents::kMaxFogVisionCircles; ++source) {
        C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, source, 1.5f);
        EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::FALLBACK)
            << "source " << source;
        C_CanvasFogOfWar::setVisionCircleLineOfSight(
            observers,
            source,
            IRComponents::kFogVisionLosOff
        );
        EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::COMPOSE)
            << "source " << source;
    }
}

TEST(SurfaceFogRoute, ActiveGlitchPresentationFallsBack) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(surfaceFogRoute(true, &observers, true, true), SurfaceFogRoute::FALLBACK);
}

TEST(SurfaceFogRoute, RouteFollowsEachFramesObserversAcrossChangesAndTeardown) {
    FrameDataFogObservers observers = observersWithSources(2);
    // One decision per frame: compose, gate a source, ungate it, clear the
    // sources, then lose the main canvas's fog.
    EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::COMPOSE);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 2.0f);
    EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::FALLBACK);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, IRComponents::kFogVisionLosOff);
    EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::COMPOSE);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 0, 2.0f);
    C_CanvasFogOfWar::clearVisionCircles(observers);
    EXPECT_EQ(surfaceFogRoute(true, &observers, true, false), SurfaceFogRoute::COMPOSE);
    EXPECT_EQ(surfaceFogRoute(true, nullptr, true, false), SurfaceFogRoute::NONE);
}

} // namespace
