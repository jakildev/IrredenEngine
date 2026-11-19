#include <gtest/gtest.h>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/per_axis_surface_lighting.hpp>

// The per-axis surface lighting switch lives on the C_PerAxisSurfaceLighting
// singleton: unset until set, readable back, and kept across a scene
// transition like every singleton row. Unset, it admits the surface routes on
// every pipeline; an explicit value stands. On a fog pipeline
// `surfaceFogRoute` then decides whether the composite fogs the surface routes
// or fog keeps lighting in compute.

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_PerAxisSurfaceLighting;
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

class PerAxisSurfaceLightingTest : public testing::Test {
  protected:
    IREntity::EntityManager m_entity_manager;
};

TEST_F(PerAxisSurfaceLightingTest, UnsetUntilSetAndReadsBack) {
    // No system manager, so no fog pass is registered.
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
    const C_PerAxisSurfaceLighting *lighting =
        IREntity::singletonOrNull<C_PerAxisSurfaceLighting>();
    ASSERT_NE(lighting, nullptr);
    EXPECT_FALSE(lighting->enabled_.has_value());

    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());

    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

TEST_F(PerAxisSurfaceLightingTest, SurvivesSceneTransition) {
    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);

    IREntity::resetGameplay();

    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

// A pipeline that registers FOG_TO_TRIXEL. Each case resolves the fog route
// from the switch as the case left it, the way `resolveRoute` does.
class PerAxisSurfaceLightingFogPassTest : public testing::Test {
  protected:
    PerAxisSurfaceLightingFogPassTest() {
        m_system_manager.recordEngineSystemId(
            IRSystem::FOG_TO_TRIXEL,
            IRSystem::createSystem<C_PerAxisSurfaceLighting>(
                "FogPassStandIn",
                [](C_PerAxisSurfaceLighting &) {}
            )
        );
    }

    static SurfaceFogRoute
    route(const FrameDataFogObservers *mainCanvasObservers, bool glitchPresentationActive) {
        return surfaceFogRoute(
            IRSystem::findSystem(IRSystem::FOG_TO_TRIXEL) != IRSystem::kNullSystemId,
            mainCanvasObservers,
            IRPrefab::PerAxisSurfaceLighting::isEnabled(),
            glitchPresentationActive
        );
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PerAxisSurfaceLightingFogPassTest, UnsetSwitchIsOn) {
    ASSERT_NE(IRSystem::findSystem(IRSystem::FOG_TO_TRIXEL), IRSystem::kNullSystemId);
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::state().enabled_.has_value());
}

TEST_F(PerAxisSurfaceLightingFogPassTest, ExplicitValueStands) {
    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);
    EXPECT_FALSE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
    IRPrefab::PerAxisSurfaceLighting::setEnabled(true);
    EXPECT_TRUE(IRPrefab::PerAxisSurfaceLighting::isEnabled());
}

TEST_F(PerAxisSurfaceLightingFogPassTest, UnsetComposesAFoggedMainCanvas) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(route(&observers, false), SurfaceFogRoute::COMPOSE);
}

TEST_F(PerAxisSurfaceLightingFogPassTest, UnsetLeavesAFoglessMainCanvasUnfogged) {
    EXPECT_EQ(route(nullptr, false), SurfaceFogRoute::NONE);
}

TEST_F(PerAxisSurfaceLightingFogPassTest, UnsetFallsBackForALineOfSightSource) {
    FrameDataFogObservers observers = observersWithSources(2);
    ASSERT_EQ(route(&observers, false), SurfaceFogRoute::COMPOSE);
    C_CanvasFogOfWar::setVisionCircleLineOfSight(observers, 1, 1.5f);
    EXPECT_EQ(route(&observers, false), SurfaceFogRoute::FALLBACK);
}

TEST_F(PerAxisSurfaceLightingFogPassTest, UnsetFallsBackForAnActiveGlitchSubject) {
    const FrameDataFogObservers observers = observersWithSources(1);
    ASSERT_EQ(route(&observers, false), SurfaceFogRoute::COMPOSE);
    EXPECT_EQ(route(&observers, true), SurfaceFogRoute::FALLBACK);
}

TEST_F(PerAxisSurfaceLightingFogPassTest, ExplicitOffKeepsCompute) {
    const FrameDataFogObservers observers = observersWithSources(1);
    IRPrefab::PerAxisSurfaceLighting::setEnabled(false);
    EXPECT_EQ(route(&observers, false), SurfaceFogRoute::FALLBACK);
    EXPECT_EQ(route(nullptr, false), SurfaceFogRoute::FALLBACK);
}

TEST(SurfaceFogRoute, PipelineWithoutAFogPassRunsUnfogged) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(surfaceFogRoute(false, nullptr, false, false), SurfaceFogRoute::NONE);
    EXPECT_EQ(surfaceFogRoute(false, &observers, true, true), SurfaceFogRoute::NONE);
}

TEST(SurfaceFogRoute, FogPipelineKeepsComputeWhileSwitchedOff) {
    const FrameDataFogObservers observers = observersWithSources(1);
    EXPECT_EQ(surfaceFogRoute(true, &observers, false, false), SurfaceFogRoute::FALLBACK);
    // A main canvas without fog on a fog pipeline still follows the switch.
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
