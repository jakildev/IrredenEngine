#include "common/lighting_demo_main.hpp"

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_sun_shading",
        .addDirectional_ = true,
        .sunShadowsEnabled_ = false,
    }
)
