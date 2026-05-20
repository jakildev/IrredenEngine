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

#include <unordered_set>
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

} // namespace

TEST_F(PerCanvasLightScopeTest, WorldScopeLightAppearsInEveryCanvasGather) {
    const IREntity::EntityId canvasA = makeCanvasSentinel();
    const IREntity::EntityId canvasB = makeCanvasSentinel();

    // No setParent — this light is world-scope.
    makeLight(vec3(0.0f, 0.0f, 0.0f), Color{255, 0, 0, 255});

    std::vector<IRRender::GPULightSource> out;
    out.reserve(IRRender::kLightVolumeMaxSources);
    std::unordered_set<std::uint64_t> warned;

    const std::uint32_t countA =
        IRSystem::detail::gatherLightSources(out, canvasA, ivec3{0, 0, 0}, warned);
    EXPECT_EQ(countA, 1u);

    const std::uint32_t countB =
        IRSystem::detail::gatherLightSources(out, canvasB, ivec3{0, 0, 0}, warned);
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
    std::unordered_set<std::uint64_t> warned;

    const std::uint32_t countA =
        IRSystem::detail::gatherLightSources(out, canvasA, ivec3{0, 0, 0}, warned);
    EXPECT_EQ(countA, 1u);

    const std::uint32_t countB =
        IRSystem::detail::gatherLightSources(out, canvasB, ivec3{0, 0, 0}, warned);
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
    std::unordered_set<std::uint64_t> warned;

    // Canvas A: own light + world-scope light = 2.
    const std::uint32_t countA =
        IRSystem::detail::gatherLightSources(out, canvasA, ivec3{0, 0, 0}, warned);
    EXPECT_EQ(countA, 2u);

    // Canvas B: own light + world-scope light = 2.
    const std::uint32_t countB =
        IRSystem::detail::gatherLightSources(out, canvasB, ivec3{0, 0, 0}, warned);
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
    std::unordered_set<std::uint64_t> warned;

    const std::uint32_t countA =
        IRSystem::detail::gatherLightSources(out, canvasA, ivec3{0, 0, 0}, warned);
    EXPECT_EQ(countA, 0u);
}
