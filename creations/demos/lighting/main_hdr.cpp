#include "common/lighting_demo_main.hpp"

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_hdr",
        .addEmissive_ = true,
        .addPoint_ = true,
        .addSpot_ = true,
        .addDirectional_ = true,
        .hdrEnabled_ = true,
        .exposure_ = 1.0f,
        .skyIntensity_ = 0.15f,
        .skyColor_ = IRMath::vec3(0.5f, 0.7f, 1.0f),
    }
)
