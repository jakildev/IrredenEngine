#include <irreden/ir_engine.hpp>

#include <irreden/ir_render.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/render/voxel_pool_config.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>

namespace IREngine::detail {

void applyPreInitLuaConfig(const char *configFile) {
    IRScript::LuaScript script{configFile};
    sol::table config = script.getTable("config");
    if (!config.valid()) {
        return;
    }

    sol::object edge = config["voxel_pool_edge"];
    if (edge.valid() && edge.is<int>()) {
        const int parsed = edge.as<int>();
        if (parsed > 0) {
            IRRender::VoxelPoolConfig::setSize(parsed);
            IRE_LOG_INFO("voxel_pool_edge from config.lua: {}", parsed);
        } else {
            IRE_LOG_WARN(
                "config.voxel_pool_edge must be a positive integer; got {} (using default {})",
                parsed,
                IRRender::VoxelPoolConfig::kDefaultEdge
            );
        }
    }
}

void clearEntityEventHandlers() {
    IRSystem::getEntityEventHandlers().clear();
}

void warnIfAutoScreenshotNeverArmed() {
    if (args().wasProvided("--auto-screenshot") && !IRVideo::isAutoCaptureActive()) {
        IRE_LOG_WARN(
            "--auto-screenshot was provided but no capture system is registered; "
            "this creation will render indefinitely and never exit. The creation "
            "must call IRVideo::createAutoScreenshotSystem (or createGuiTestSystem "
            "for the GUI-test path) before gameLoop() (see engine/video/CLAUDE.md)."
        );
    }
}

} // namespace IREngine::detail
