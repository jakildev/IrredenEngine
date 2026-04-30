#include "common/lighting_demo_main.hpp"

IR_LIGHTING_DEMO_MAIN(
    IRLightingDemo::DemoConfig{
        .name_ = "lighting_debug_overlays",
        .overlay_ = IRRender::DebugOverlayMode::LIGHT_LEVEL,
        .addEmissive_ = true,
        .addPoint_ = true,
    }
)
