#include <irreden/ir_engine.hpp>
#include "lua_bindings.hpp"

// COMPONENTS
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>
#include <irreden/update/components/component_acceleration_3d.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/update/components/component_goto_easing_3d.hpp>
#include <irreden/audio/components/component_midi_note.hpp>
#include <irreden/audio/components/component_midi_sequence.hpp>
#include <irreden/update/components/component_particle_burst.hpp>

// SYSTEMS -- Update
#include <irreden/update/systems/system_velocity.hpp>
#include <irreden/update/systems/system_acceleration.hpp>
#include <irreden/update/systems/system_gravity.hpp>
#include <irreden/update/systems/system_velocity_drag.hpp>
#include <irreden/update/systems/system_goto_3d.hpp>
#include <irreden/update/systems/system_update_positions_global.hpp>
#include <irreden/voxel/systems/system_voxel_squash_stretch.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/update/systems/system_lifetime.hpp>
#include <irreden/update/systems/system_periodic_idle.hpp>
#include <irreden/update/systems/system_periodic_idle_position_offset.hpp>
#include <irreden/update/systems/system_apply_position_offset.hpp>
#include <irreden/update/systems/system_collision_event_clear.hpp>
#include <irreden/update/systems/system_collision_note_platform.hpp>
#include <irreden/update/systems/system_reactive_return_3d.hpp>
#include <irreden/update/systems/system_rhythmic_launch.hpp>
#include <irreden/audio/systems/system_contact_midi_trigger.hpp>
#include <irreden/update/systems/system_contact_note_burst.hpp>
#include <irreden/update/systems/system_contact_trigger_glow.hpp>
#include <irreden/update/systems/system_spawn_glow.hpp>
#include <irreden/update/systems/system_action_animation.hpp>
#include <irreden/update/systems/system_animation_color.hpp>
#include <irreden/update/systems/system_anim_motion_color_shift.hpp>
#include <irreden/update/systems/system_spring_platform.hpp>
#include <irreden/update/systems/system_spring_color.hpp>
#include <irreden/audio/systems/system_midi_delay_process.hpp>
#include <irreden/audio/systems/system_midi_sequence_out.hpp>
#include <irreden/audio/systems/system_audio_midi_message_out.hpp>

// SYSTEMS -- Input
#include <irreden/input/systems/system_input_key_mouse.hpp>
#include <irreden/input/systems/system_input_gamepad.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>

// SYSTEMS -- Render
#include <irreden/render/systems/system_render_velocity_2d_iso.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_trixel.hpp>
#include <irreden/render/systems/system_trixel_to_framebuffer.hpp>
#include <irreden/render/systems/system_text_to_trixel.hpp>
#include <irreden/render/systems/system_framebuffer_to_screen.hpp>

// COMMANDS
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/render/commands/command_background_zoom_in.hpp>
#include <irreden/render/commands/command_background_zoom_out.hpp>
#include <irreden/render/commands/command_move_camera.hpp>
#include <irreden/render/commands/command_toggle_gui.hpp>
#include <irreden/render/commands/command_gui_zoom.hpp>
#include <irreden/video/commands/command_take_screenshot.hpp>
#include <irreden/video/commands/command_toggle_recording.hpp>
#include <irreden/update/commands/command_toggle_periodic_idle_pause.hpp>

void initSystems();
void initCommands();

int main(int argc, char **argv) {
    IR_LOG_INFO("Starting creation: midi_polyrhythm");

    MidiPolyrhythm::registerLuaBindings();
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
        {IRSystem::createSystem<IRSystem::REACTIVE_RETURN_3D>(),
         IRSystem::createSystem<IRSystem::GRAVITY_3D>(),
         IRSystem::createSystem<IRSystem::ACCELERATION_3D>(),
         IRSystem::createSystem<IRSystem::VELOCITY_DRAG>(),
         IRSystem::createSystem<IRSystem::VELOCITY_3D>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE>(),
         IRSystem::createSystem<IRSystem::PERIODIC_IDLE_POSITION_OFFSET>(),
         IRSystem::createSystem<IRSystem::GOTO_3D>(),
         IRSystem::createSystem<IRSystem::GLOBAL_POSITION_3D>(),
         IRSystem::createSystem<IRSystem::APPLY_POSITION_OFFSET>(),
         IRSystem::createSystem<IRSystem::COLLISION_EVENT_CLEAR>(),
         IRSystem::createSystem<IRSystem::COLLISION_NOTE_PLATFORM>(),
         IRSystem::createSystem<IRSystem::SPRING_PLATFORM>(),
         IRSystem::createSystem<IRSystem::ACTION_ANIMATION>(),
         IRSystem::createSystem<IRSystem::RHYTHMIC_LAUNCH>(),
         IRSystem::createSystem<IRSystem::CONTACT_MIDI_TRIGGER>(),
         IRSystem::createSystem<IRSystem::CONTACT_NOTE_BURST>(),
         IRSystem::createSystem<IRSystem::CONTACT_TRIGGER_GLOW>(),
         IRSystem::createSystem<IRSystem::SPAWN_GLOW>(),
         IRSystem::createSystem<IRSystem::ANIMATION_COLOR>(),
         IRSystem::createSystem<IRSystem::ANIMATION_MOTION_COLOR_SHIFT>(),
         IRSystem::createSystem<IRSystem::SPRING_COLOR>(),
         IRSystem::createSystem<IRSystem::MIDI_SEQUENCE_OUT>(),
         IRSystem::createSystem<IRSystem::MIDI_DELAY_PROCESS>(),
         IRSystem::createSystem<IRSystem::OUTPUT_MIDI_MESSAGE_OUT>(),
         IRSystem::createSystem<IRSystem::VOXEL_SQUASH_STRETCH>(),
         IRSystem::createSystem<IRSystem::UPDATE_VOXEL_SET_CHILDREN>(),
         IRSystem::createSystem<IRSystem::LIFETIME>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::INPUT,
        {IRSystem::createSystem<IRSystem::INPUT_KEY_MOUSE>(),
         IRSystem::createSystem<IRSystem::INPUT_GAMEPAD>(),
         IRSystem::createSystem<IRSystem::ENTITY_HOVER_DETECT>()}
    );

    IRSystem::registerPipeline(
        IRTime::Events::RENDER,
        {IRSystem::createSystem<IRSystem::RENDERING_VELOCITY_2D_ISO>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_1>(),
         IRSystem::createSystem<IRSystem::VOXEL_TO_TRIXEL_STAGE_2>(),
         IRSystem::createSystem<IRSystem::TEXT_TO_TRIXEL>(),
         IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>(),
         IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>()}
    );
}

void initCommands() {
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
    IRCommand::createCommand<IRCommand::ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonKPAdd
    );
    IRCommand::createCommand<IRCommand::ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonKPSubtract
    );
    IRCommand::createCommand<IRCommand::BACKGROUND_ZOOM_IN>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonEqual,
        kModifierShift
    );
    IRCommand::createCommand<IRCommand::BACKGROUND_ZOOM_OUT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonMinus,
        kModifierShift
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
    IRCommand::createCommand<IRCommand::SCREENSHOT>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF8
    );
    IRCommand::createCommand<IRCommand::RECORD_TOGGLE>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonF9
    );
    IRCommand::createCommand<IRCommand::TOGGLE_PERIODIC_IDLE_PAUSE>(
        InputTypes::KEY_MOUSE,
        ButtonStatuses::PRESSED,
        KeyMouseButtons::kKeyButtonP
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
}
