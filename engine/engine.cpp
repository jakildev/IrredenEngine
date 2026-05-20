#include <irreden/ir_engine.hpp>

#include <irreden/ir_render.hpp>
#include <irreden/ir_script.hpp>
#include <irreden/render/voxel_pool_config.hpp>

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

} // namespace IREngine::detail
