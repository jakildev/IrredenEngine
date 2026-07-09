// Per-canvas light scope (T-116, issue #363).
//
// Verifies the gather logic in `IRSystem::detail::gatherLightSources`
// scopes a `C_LightSource` to a specific canvas when the light is
// CHILD_OF that canvas. Lights with no parent are world-scope and
// appear in every canvas's gather (the back-compat default).
//
// Runs without a RenderManager: the gather function only touches the
// EntityManager, so the test fixture installs a stack-local manager
// and calls the gather directly. No GPU resources are bound.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>

#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace {

class PerCanvasLightScopeTest : public testing::Test {
  protected:
    PerCanvasLightScopeTest()
        : m_entityManager{} {}

    IREntity::EntityManager m_entityManager;
};

// A canvas entity for scoping is just any entity ID — the gather only
// reads the light's CHILD_OF parent, not the parent's components.
IREntity::EntityId makeCanvasSentinel() {
    return IREntity::createEntity();
}

IREntity::EntityId makeLight(IRMath::vec3 position, IRMath::Color color) {
    // Seed C_WorldTransform directly because this test exercises the
    // gather in isolation — no PROPAGATE_TRANSFORM system is running to
    // resolve a local transform into the world slot.
    return IREntity::createEntity(
        IRComponents::C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_LightSource{LightType::EMISSIVE, color, 1.0f, static_cast<std::uint8_t>(8)}
    );
}

IREntity::EntityId
makeLightWithRadius(IRMath::vec3 position, IRMath::Color color, std::uint8_t radius) {
    return IREntity::createEntity(
        IRComponents::C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_LightSource{LightType::EMISSIVE, color, 1.0f, radius}
    );
}

IREntity::EntityId makeSpotLight(
    IRMath::vec3 position, IRMath::vec3 direction, std::uint8_t radius, float coneAngleDeg
) {
    return IREntity::createEntity(
        IRComponents::C_WorldTransform{position, vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_LightSource{
            LightType::SPOT,
            Color{255, 255, 255, 255},
            1.0f,
            radius,
            direction,
            coneAngleDeg
        }
    );
}

} // namespace

TEST_F(PerCanvasLightScopeTest, WorldScopeLightAppearsInEveryCanvasGather) {
    const IREntity::EntityId canvasA = makeCanvasSentinel();
    const IREntity::EntityId canvasB = makeCanvasSentinel();

    // No setParent — this light is world-scope.
    makeLight(vec3(0.0f, 0.0f, 0.0f), Color{255, 0, 0, 255});

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    const std::uint32_t countA = IRSystem::detail::gatherLightSources(
        out,
        canvasA,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countA, 1u);

    const std::uint32_t countB = IRSystem::detail::gatherLightSources(
        out,
        canvasB,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countB, 1u);
}

TEST_F(PerCanvasLightScopeTest, ChildOfCanvasLightOnlyAppearsForOwnCanvas) {
    const IREntity::EntityId canvasA = makeCanvasSentinel();
    const IREntity::EntityId canvasB = makeCanvasSentinel();

    const IREntity::EntityId lightA = makeLight(vec3(0.0f), Color{255, 0, 0, 255});
    IREntity::setParent(lightA, canvasA);

    const IREntity::EntityId lightB = makeLight(vec3(1.0f, 0.0f, 0.0f), Color{0, 0, 255, 255});
    IREntity::setParent(lightB, canvasB);

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    const std::uint32_t countA = IRSystem::detail::gatherLightSources(
        out,
        canvasA,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countA, 1u);

    const std::uint32_t countB = IRSystem::detail::gatherLightSources(
        out,
        canvasB,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countB, 1u);
}

TEST_F(PerCanvasLightScopeTest, MixedScopesEachCanvasSeesOwnPlusWorldScope) {
    const IREntity::EntityId canvasA = makeCanvasSentinel();
    const IREntity::EntityId canvasB = makeCanvasSentinel();

    const IREntity::EntityId lightAonly = makeLight(vec3(0.0f), Color{255, 0, 0, 255});
    IREntity::setParent(lightAonly, canvasA);

    const IREntity::EntityId lightBonly = makeLight(vec3(1.0f, 0.0f, 0.0f), Color{0, 0, 255, 255});
    IREntity::setParent(lightBonly, canvasB);

    makeLight(vec3(2.0f, 0.0f, 0.0f), Color{0, 255, 0, 255}); // world-scope

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    // Canvas A: own light + world-scope light = 2.
    const std::uint32_t countA = IRSystem::detail::gatherLightSources(
        out,
        canvasA,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countA, 2u);

    // Canvas B: own light + world-scope light = 2.
    const std::uint32_t countB = IRSystem::detail::gatherLightSources(
        out,
        canvasB,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countB, 2u);
}

TEST_F(PerCanvasLightScopeTest, DirectionalLightStillSkippedRegardlessOfParent) {
    const IREntity::EntityId canvasA = makeCanvasSentinel();

    // Directional lights drive sun shading via FrameDataSun, never the
    // light volume. Their parent should not change that.
    const IREntity::EntityId sun = IREntity::createEntity(
        IRComponents::C_WorldTransform{vec3(0.0f), vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
        C_LightSource{
            LightType::DIRECTIONAL,
            IRColors::kWhite,
            1.0f,
            static_cast<std::uint8_t>(0),
            vec3(-0.3f, -0.2f, -0.93f),
            45.0f,
            0.4f
        }
    );
    IREntity::setParent(sun, canvasA);

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    const std::uint32_t countA = IRSystem::detail::gatherLightSources(
        out,
        canvasA,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(countA, 0u);
    // No gathered lights → max-radius stays at the gather's zero-init default.
    EXPECT_EQ(maxRadius, 0);
}

TEST_F(PerCanvasLightScopeTest, GatherMaxRadiusUncappedForSmallRadius) {
    const IREntity::EntityId canvas = makeCanvasSentinel();
    makeLightWithRadius(vec3(0.0f), Color{255, 0, 0, 255}, 8);

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    IRSystem::detail::gatherLightSources(out, canvas, ivec3{0, 0, 0}, maxRadius, eligible, hasSpot);
    // radius=8 is below kLightVolumePropagateIterations (32) — no cap applied.
    EXPECT_EQ(maxRadius, 8);
}

TEST_F(PerCanvasLightScopeTest, GatherMaxRadiusCappedAtPropagateIterations) {
    const IREntity::EntityId canvas = makeCanvasSentinel();
    // radius=255 exceeds kLightVolumePropagateIterations (32).
    makeLightWithRadius(vec3(0.0f), Color{255, 0, 0, 255}, 255);

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    IRSystem::detail::gatherLightSources(out, canvas, ivec3{0, 0, 0}, maxRadius, eligible, hasSpot);
    // radius=255 must be capped to kLightVolumePropagateIterations to prevent
    // the propagate loop from dispatching more iterations than the budget allows.
    EXPECT_EQ(maxRadius, IRRender::kLightVolumePropagateIterations);
}

// #2318: hasSpot gates the consumer's winning-light-ID read. It must stay
// false for a scene with only non-SPOT lights so those scenes render exactly
// as before (byte-identical).
TEST_F(PerCanvasLightScopeTest, GatherHasSpotFalseWithoutSpotLights) {
    const IREntity::EntityId canvas = makeCanvasSentinel();
    makeLight(vec3(0.0f), Color{255, 0, 0, 255}); // EMISSIVE, in-window

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    const std::uint32_t count = IRSystem::detail::gatherLightSources(
        out,
        canvas,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    EXPECT_EQ(count, 1u);
    EXPECT_FALSE(hasSpot);
}

// #2318: a seeded SPOT light flags hasSpot and carries its TRUE (unclamped)
// origin separately from the seed cell. An out-of-window spot seeds the
// clamped window-boundary cell, but the cone consumer must orient from the
// real apex — so trueOriginVoxel_ keeps the true position while
// originAndType_.xyz is the clamped seed cell.
TEST_F(PerCanvasLightScopeTest, GatherSpotFlagsHasSpotAndCarriesTrueOrigin) {
    const IREntity::EntityId canvas = makeCanvasSentinel();
    // Origin at world (70,0,0) with the ±64 window centered on (0,0,0): the
    // x axis clamps to halfExtent-1 = 63. radius 32 keeps the boundary-
    // discounted seed alpha positive so the spot is actually seeded.
    makeSpotLight(vec3(70.0f, 0.0f, 0.0f), vec3(-1.0f, 0.0f, 0.0f), 32, 40.0f);

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::uint32_t eligible = 0;
    int maxRadius = 0;
    bool hasSpot = false;

    const std::uint32_t count = IRSystem::detail::gatherLightSources(
        out,
        canvas,
        ivec3{0, 0, 0},
        maxRadius,
        eligible,
        hasSpot
    );
    ASSERT_EQ(count, 1u);
    EXPECT_TRUE(hasSpot);
    // Seed cell clamped to the window edge on x, true on y/z.
    EXPECT_EQ(static_cast<int>(out[0].originAndType_.x), IRComponents::kLightVolumeHalfExtent - 1);
    EXPECT_EQ(static_cast<int>(out[0].originAndType_.y), 0);
    // True apex preserved unclamped for the cone factor.
    EXPECT_EQ(static_cast<int>(out[0].trueOriginVoxel_.x), 70);
    EXPECT_EQ(static_cast<int>(out[0].trueOriginVoxel_.y), 0);
    EXPECT_EQ(static_cast<int>(out[0].trueOriginVoxel_.z), 0);
    // Type lane still encodes SPOT for the consumer's branch.
    EXPECT_EQ(static_cast<int>(out[0].originAndType_.w), static_cast<int>(LightType::SPOT));
}
