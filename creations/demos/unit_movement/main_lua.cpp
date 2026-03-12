#include <irreden/ir_engine.hpp>
#include <irreden/ir_input.hpp>
#include "lua_bindings.hpp"

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/update/components/component_nav_cell.hpp>
#include <irreden/update/components/component_nav_agent.hpp>
#include <irreden/update/components/component_move_order.hpp>
#include <irreden/update/components/component_facing_2d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <irreden/update/systems/system_grid_bake.hpp>
#include <irreden/update/systems/system_flow_field_build.hpp>
#include <irreden/update/systems/system_grid_pathfind.hpp>
#include <irreden/update/systems/system_flow_field_movement.hpp>
#include <irreden/update/systems/system_grid_movement.hpp>
#include <irreden/update/systems/system_smooth_movement.hpp>
#include <irreden/update/systems/system_turn_to_move.hpp>
#include <irreden/update/systems/system_unit_collision_resolve.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/input/systems/system_unit_selection.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>

#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>

#include <irreden/update/systems/system_debug_draw_nav.hpp>

#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>
#include <irreden/render/commands/command_toggle_gui.hpp>
#include <irreden/render/commands/command_gui_zoom.hpp>
#include <irreden/video/commands/command_take_screenshot.hpp>
#include <irreden/video/commands/command_take_screenshot_canvas.hpp>

void initSystems();
void initCommands();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: unit_movement");

    UnitMovement::registerLuaBindings();
    IREngine::init(argv[0]);
    initSystems();
    initCommands();
    IREngine::runScript("main.lua");
    IREngine::gameLoop();

    return 0;
}

void initSystems() {
    IRSystem::registerPipeline(
        IRTime::Events::UPDATE,
        {IRSystem::createSystem<IRSystem::GRID_BAKE>(),
         IRSystem::createSystem<IRSystem::FLOW_FIELD_BUILD>(),
         IRSystem::createSystem<IRSystem::GRID_PATHFIND>(),
         IRSystem::createSystem<IRSystem::TURN_TO_MOVE>(),
         IRSystem::createSystem<IRSystem::FLOW_FIELD_MOVEMENT>(),
         IRSystem::createSystem<IRSystem::SMOOTH_MOVEMENT>(),
         IRSystem::createSystem<IRSystem::GRID_MOVEMENT>(),
         IRSystem::createSystem<IRSystem::UNIT_COLLISION_RESOLVE>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::INPUT_GAMEPAD>(),
         IRSystem::createSystem<IRSystem::UNIT_SELECTION>(),
         IRSystem::createSystem<IRSystem::ENTITY_HOVER_DETECT>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
         IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
         IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>(),
         IRSystem::createSystem<IRSystem::DEBUG_DRAW_NAV>(),
         IRSystem::createSystem<IRSystem::UNIT_SELECTION_RENDER>(),
         IRSystem::createSystem<IRSystem::DEBUG_OVERLAY>()}
    );
}

void initCommands() {
    using namespace IRInput;
    IRCommand::createCommand<IRCommand::CLOSE_WINDOW>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEscape
    );
    IRCommand::createCommand<IRCommand::ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual
    );
    IRCommand::createCommand<IRCommand::ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_START>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonS
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_START>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonW
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_START>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonD
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_START>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonA
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_DOWN_END>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonS
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_UP_END>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonW
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_RIGHT_END>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonD
    );
    IRCommand::createCommand<IRCommand::MOVE_CAMERA_LEFT_END>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::RELEASED,
        KeyMouseButtons::kKeyButtonA
    );
    IRCommand::createCommand<IRCommand::TOGGLE_GUI>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonGraveAccent
    );
    IRCommand::createCommand<IRCommand::GUI_ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual,
        kModifierControl
    );
    IRCommand::createCommand<IRCommand::GUI_ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus,
        kModifierControl
    );
    IRCommand::createCommand<IRCommand::SCREENSHOT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF8
    );
    IRCommand::createCommand<IRCommand::SCREENSHOT_CANVAS>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF7
    );
}
