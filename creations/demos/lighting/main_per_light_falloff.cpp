#include "common/lighting_demo_main.hpp"

namespace {

using namespace IRComponents;
using namespace IREntity;
using namespace IRMath;

constexpr IRVideo::AutoScreenshotShot kShots[] = {
    {4.0f, vec2(0.0f), 0.0f, "per_light_falloff"},
};

void buildGeometry() {
    namespace LD = IRLightingDemo::detail;
    const vec4 floorParams{88.0f, 48.0f, 2.0f, 0.0f};
    const EntityId floor = LD::createSdfShape(
        vec3(0.0f, 0.0f, 5.0f),
        IRRender::ShapeType::BOX,
        floorParams,
        Color{112, 112, 120, 255}
    );
    IREntity::setComponent(floor, C_LightBlocker{false, false, 0.0f});
}

void buildLights() {
    if (IREngine::args().getFlag("--no-local-lights")) {
        return;
    }
    IREntity::createEntity(
        C_LocalTransform{vec3(-24.0f, 0.0f, 3.0f)},
        C_LightSource{LightType::POINT, Color{255, 80, 60, 255}, 2.5f, static_cast<std::uint8_t>(8)}
    );
    if (!IREngine::args().getFlag("--no-wide-light")) {
        IREntity::createEntity(
            C_LocalTransform{vec3(24.0f, 0.0f, 3.0f)},
            C_LightSource{
                LightType::POINT,
                Color{60, 120, 255, 255},
                2.5f,
                static_cast<std::uint8_t>(32)
            }
        );
    }
}

} // namespace

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_per_light_falloff",
        .sunIntensity_ = 0.0f,
        .sunAmbient_ = 0.15f,
        .sunShadowsEnabled_ = false,
        .aoEnabled_ = false,
        .shots_ = kShots,
        .numShots_ = static_cast<int>(sizeof(kShots) / sizeof(kShots[0])),
        .geometryFn_ = buildGeometry,
        .localLightsFn_ = buildLights,
    }
)
