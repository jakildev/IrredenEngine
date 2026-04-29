#include "common/lighting_demo_main.hpp"

// Demonstrates that a `C_LightSource{DIRECTIONAL}` entity overrides the
// global IRRender::setSunDirection. The global stays at the engine
// default (mostly overhead), and the C_LightSource swings the sun to a
// dramatic side-light along -X. The resulting lit faces and projected
// shadows differ visibly from main_sun_shadow.cpp, where no override
// entity is created.
IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_directional",
        .overlay_ = IRRender::DebugOverlayMode::SHADOW,
        .addDirectional_ = true,
        .directionalOverrideDirection_ = IRMath::vec3(-0.85f, 0.3f, -0.4f),
        .directionalOverrideIntensity_ = 1.1f,
        .directionalOverrideAmbient_ = 0.25f,
    }
)
