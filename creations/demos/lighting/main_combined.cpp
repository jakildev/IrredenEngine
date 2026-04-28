#include "common/lighting_demo_main.hpp"

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_combined",
        .addEmissive_ = true,
        .addPoint_ = true,
        .addSpot_ = true,
        .addDirectional_ = true,
        .enableFog_ = true,
    }
)
