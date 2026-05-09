// Two-canvas verification for T-116 / #363: per-canvas C_LightSource
// scope via `IREntity::setParent`.
//
// One rendered canvas (the main one) plus a sentinel "canvas B" entity
// that owns its own C_CanvasLightVolume but is not in the rendering
// chain — its volume is computed and ignored, present only as a scope
// target for one of the lights below. Three lights at the same world
// origin demonstrate the scoping rules:
//
//   - Light A (red emissive, parented to main canvas):
//     contributes only to main → red component on the rendered scene.
//   - Light B (blue emissive, parented to canvas B):
//     does NOT contribute to main → no blue mixed into the render.
//   - Light C (green emissive, no parent — world-scope):
//     contributes to every canvas → green component mixes onto main.
//
// Expected output: scene voxels are lit yellow / orange (red + green),
// not white-ish. If T-116 scoping regresses, light B leaks into the
// main canvas and the colors shift toward white.

#include "common/lighting_demo_main.hpp"

#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>

namespace {

void perCanvasScopeScene() {
    using namespace IREntity;
    using namespace IRComponents;
    using IRMath::Color;
    using IRMath::vec3;

    IRLightingDemo::detail::createGeometry();

    EntityId mainCanvas = IRRender::getActiveCanvasEntity();

    EntityId canvasB = createEntity(C_CanvasLightVolume{}, C_TrixelCanvasRenderBehavior{});
    setName(canvasB, "scope_canvas_b");

    constexpr vec3 lightOriginA{24.0f, 6.0f, -2.0f};
    constexpr vec3 lightOriginB{34.0f, -7.0f, -1.0f};
    constexpr vec3 lightOriginC{10.0f, -10.0f, -2.0f};
    constexpr std::uint8_t kRadius = 28;
    constexpr float kIntensity = 2.0f;

    EntityId lightA = createEntity(
        C_Position3D{lightOriginA},
        C_LightSource{LightType::EMISSIVE, Color{220, 60, 30, 255}, kIntensity, kRadius}
    );
    setParent(lightA, mainCanvas);

    EntityId lightB = createEntity(
        C_Position3D{lightOriginB},
        C_LightSource{LightType::EMISSIVE, Color{30, 60, 220, 255}, kIntensity, kRadius}
    );
    setParent(lightB, canvasB);

    createEntity(
        C_Position3D{lightOriginC},
        C_LightSource{LightType::EMISSIVE, Color{40, 200, 80, 255}, kIntensity, kRadius}
    );
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_per_canvas_scope",
        .geometryFn_ = perCanvasScopeScene,
    }
)
