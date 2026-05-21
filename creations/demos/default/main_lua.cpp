#include <irreden/ir_engine.hpp>
#include "lua_bindings.hpp"

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/audio/components/component_midi_note.hpp>

// SYSTEMS
#include <irreden/common/systems/system_modifier_decay.hpp>
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_periodic_idle_position_offset.hpp>
#include <irreden/audio/systems/system_periodic_idle_midi_trigger.hpp>
#include <irreden/audio/systems/system_midi_sequence_out.hpp>
#include <irreden/audio/systems/system_audio_midi_message_out.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>

#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_shapes_to_trixel.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>
#include <irreden/render/systems/system_debug_culling_minimap.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>

// Input + prefab command bindings live in scripts/commands.lua via the
// IRCommand / IRInput Lua surface; see docs/design/lua-input-commands.md.

void initSystems();
void initEntities();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: default");

    IRDefaultCreation::registerLuaBindings();
    IREngine::init(argv[0]);
    initSystems();
    IREngine::runScript("commands.lua");
    IREngine::gameLoop();

    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE>(),
         IRSystem::createSystem<IRSystem::MODIFIER_DECAY>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE_POSITION_OFFSET>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE_MIDI_TRIGGER>(),
         IRSystem::createSystem<IRSystem::MIDI_SEQUENCE_OUT>(),
         IRSystem::createSystem<IRSystem::OUTPUT_MIDI_MESSAGE_OUT>(),
         IRSystem::createSystem<IRSystem::GOTO_3D>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::INPUT_GAMEPAD>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {IRSystem::createSystem<IRSystem::CAMERA_MOUSE_PAN>(),
         IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
         IRSystem::createSystem<IRSystem::SHAPES_TO_TRIXEL>(),
         IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>(),
         IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
         IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
         IRSystem::createSystem<IRSystem::DEBUG_CULLING_MINIMAP>(),
         IRSystem::createSystem<IRSystem::DEBUG_OVERLAY>(),
         IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
    );
}
