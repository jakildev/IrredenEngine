#include <irreden/ir_engine.hpp>
#include "lua_bindings.hpp"

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>

// SYSTEMS
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>

#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

// COMMANDS
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>

void initSystems();
void initEntities();
void initCommands();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: default");

    IRDefaultCreation::registerLuaBindings();
    IREngine::init("config.lua");
    initSystems();
    initCommands();
    IREngine::runScript("main.lua");
    IREngine::gameLoop();

    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(IRTime::Events::UPDATE,
                               {IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
                                IRSystem::createSystem<IRSystem::PERIODIC_IDLE>(),
                                IRSystem::createSystem<IRSystem::GOTO_3D>(),
                                IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
                                IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
                                IRSystem::createSystem<IRSystem::LIFETIME>()});

    IRSystem::registerPipeline(IRTime::Events::INPUT,
                               {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
                                IRSystem::createSystem<IRSystem::INPUT_GAMEPAD>()});

    IRSystem::registerPipeline(IRTime::Events::RENDER,
                               {IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
                                IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
                                IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
                                IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
                                IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()});
}

void initCommands() {
    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonEscape);
    IRCommand::createCommand<IRCommand::ZOOM_IN>(InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
                                                 KeyMouseButtons::kKeyButtonEqual);
    IRCommand::createCommand<IRCommand::ZOOM_OUT>(InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED,
                                                  KeyMouseButtons::kKeyButtonMinus);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonS);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonW);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonD);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_START>(
        InputTypes::KEY_MOUSE, ButtonStatuses::PRESSED, KeyMouseButtons::kKeyButtonA);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED, KeyMouseButtons::kKeyButtonS);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED, KeyMouseButtons::kKeyButtonW);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED, KeyMouseButtons::kKeyButtonD);
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_END>(
        InputTypes::KEY_MOUSE, ButtonStatuses::RELEASED, KeyMouseButtons::kKeyButtonA);
}