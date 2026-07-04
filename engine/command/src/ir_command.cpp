#include <irreden/ir_command.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_video.hpp>
#include <irreden/ir_window.hpp>

// `fireByName` dispatches by enum value to the matching `Command<NAME>::create()`
// specialization. Each spec lives in its own prefab header; pull them all in here
// so this TU is the single point of coupling between engine/command and the
// prefab command catalog. Other TUs that only need command registration (the
// vast majority — every system, every Lua binding) keep the lighter
// `ir_command.hpp`-only include.
#include <irreden/input/commands/command_close_window.hpp>
#include <irreden/render/commands/command_background_zoom_in.hpp>
#include <irreden/render/commands/command_background_zoom_out.hpp>
#include <irreden/render/commands/command_gui_zoom.hpp>
#include <irreden/render/commands/command_move_camera.hpp>
// command_set_trixel_color.hpp is WIP and includes non-existent ir_ecs.hpp —
// SET_TRIXEL_COLOR has no working Command<NAME> specialization today, so
// fireByName falls through to the unimplemented-log path for it.
#include <irreden/render/commands/command_toggle_culling_freeze.hpp>
#include <irreden/render/commands/command_toggle_gui.hpp>
#include <irreden/render/commands/command_zoom_in.hpp>
#include <irreden/render/commands/command_zoom_out.hpp>
#include <irreden/update/commands/command_toggle_periodic_idle_pause.hpp>
#include <irreden/video/commands/command_take_screenshot.hpp>
#include <irreden/video/commands/command_take_screenshot_canvas.hpp>
#include <irreden/video/commands/command_toggle_recording.hpp>
#include <irreden/voxel/commands/command_randomize_voxels.hpp>
#include <irreden/voxel/commands/command_spawn_particle_mouse_position.hpp>

namespace IRCommand {

CommandManager *g_commandManager = nullptr;
CommandManager &getCommandManager() {
    IR_ASSERT(g_commandManager != nullptr, "CommandManager not initialized");
    return *g_commandManager;
}

void fire(CommandId id) {
    getCommandManager().fireUserCommand(id);
}

CommandId bindPrefabCommand(
    CommandNames name,
    IRInput::InputTypes inputType,
    IRInput::ButtonStatuses triggerStatus,
    int button,
    IRInput::KeyModifierMask requiredModifiers,
    IRInput::KeyModifierMask blockedModifiers
) {
    switch (name) {
    case ZOOM_IN:
        return createCommand<ZOOM_IN>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case ZOOM_OUT:
        return createCommand<ZOOM_OUT>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case BACKGROUND_ZOOM_IN:
        return createCommand<BACKGROUND_ZOOM_IN>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case BACKGROUND_ZOOM_OUT:
        return createCommand<BACKGROUND_ZOOM_OUT>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case CLOSE_WINDOW:
        return createCommand<CLOSE_WINDOW>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_LEFT_START:
        return createCommand<MOVE_CAMERA_LEFT_START>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_RIGHT_START:
        return createCommand<MOVE_CAMERA_RIGHT_START>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_UP_START:
        return createCommand<MOVE_CAMERA_UP_START>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_DOWN_START:
        return createCommand<MOVE_CAMERA_DOWN_START>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_LEFT_END:
        return createCommand<MOVE_CAMERA_LEFT_END>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_RIGHT_END:
        return createCommand<MOVE_CAMERA_RIGHT_END>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_UP_END:
        return createCommand<MOVE_CAMERA_UP_END>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case MOVE_CAMERA_DOWN_END:
        return createCommand<MOVE_CAMERA_DOWN_END>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case SCREENSHOT:
        return createCommand<SCREENSHOT>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case SCREENSHOT_CANVAS:
        return createCommand<SCREENSHOT_CANVAS>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case RECORD_TOGGLE:
        return createCommand<RECORD_TOGGLE>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case SPAWN_PARTICLE_MOUSE_POSITION:
        return createCommand<SPAWN_PARTICLE_MOUSE_POSITION>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case TOGGLE_PERIODIC_IDLE_PAUSE:
        return createCommand<TOGGLE_PERIODIC_IDLE_PAUSE>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case TOGGLE_GUI:
        return createCommand<TOGGLE_GUI>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case GUI_ZOOM_IN:
        return createCommand<GUI_ZOOM_IN>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case GUI_ZOOM_OUT:
        return createCommand<GUI_ZOOM_OUT>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case TOGGLE_CULLING_FREEZE:
        return createCommand<TOGGLE_CULLING_FREEZE>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case RANDOMIZE_VOXELS:
        return createCommand<RANDOMIZE_VOXELS>(
            inputType,
            triggerStatus,
            button,
            requiredModifiers,
            blockedModifiers
        );
    case NULL_COMMAND:
    case EXAMPLE:
    case RECORD_START:
    case RECORD_STOP:
    case STOP_VELOCITY:
    case RESHAPE_SPHERE:
    case RESHAPE_RECTANGULAR_PRISM:
    case LOCK_VOXEL_SCALE:
    case UNLOCK_VOXEL_SCALE:
    case SET_TRIXEL_COLOR:
        break;
    }
    IRE_LOG_ERROR(
        "IRCommand::bindPrefabCommand: no Command<{}> specialization registered "
        "(CommandNames value {})",
        commandNameToString(name),
        static_cast<int>(name)
    );
    return kInvalidCommandId;
}

void fireByName(CommandNames name) {
    switch (name) {
    case ZOOM_IN:
        Command<ZOOM_IN>::create()();
        return;
    case ZOOM_OUT:
        Command<ZOOM_OUT>::create()();
        return;
    case BACKGROUND_ZOOM_IN:
        Command<BACKGROUND_ZOOM_IN>::create()();
        return;
    case BACKGROUND_ZOOM_OUT:
        Command<BACKGROUND_ZOOM_OUT>::create()();
        return;
    case CLOSE_WINDOW:
        Command<CLOSE_WINDOW>::create()();
        return;
    case MOVE_CAMERA_LEFT_START:
        Command<MOVE_CAMERA_LEFT_START>::create()();
        return;
    case MOVE_CAMERA_RIGHT_START:
        Command<MOVE_CAMERA_RIGHT_START>::create()();
        return;
    case MOVE_CAMERA_UP_START:
        Command<MOVE_CAMERA_UP_START>::create()();
        return;
    case MOVE_CAMERA_DOWN_START:
        Command<MOVE_CAMERA_DOWN_START>::create()();
        return;
    case MOVE_CAMERA_LEFT_END:
        Command<MOVE_CAMERA_LEFT_END>::create()();
        return;
    case MOVE_CAMERA_RIGHT_END:
        Command<MOVE_CAMERA_RIGHT_END>::create()();
        return;
    case MOVE_CAMERA_UP_END:
        Command<MOVE_CAMERA_UP_END>::create()();
        return;
    case MOVE_CAMERA_DOWN_END:
        Command<MOVE_CAMERA_DOWN_END>::create()();
        return;
    case SCREENSHOT:
        Command<SCREENSHOT>::create()();
        return;
    case SCREENSHOT_CANVAS:
        Command<SCREENSHOT_CANVAS>::create()();
        return;
    case RECORD_TOGGLE:
        Command<RECORD_TOGGLE>::create()();
        return;
    case SPAWN_PARTICLE_MOUSE_POSITION:
        Command<SPAWN_PARTICLE_MOUSE_POSITION>::create()();
        return;
    case TOGGLE_PERIODIC_IDLE_PAUSE:
        Command<TOGGLE_PERIODIC_IDLE_PAUSE>::create()();
        return;
    case TOGGLE_GUI:
        Command<TOGGLE_GUI>::create()();
        return;
    case GUI_ZOOM_IN:
        Command<GUI_ZOOM_IN>::create()();
        return;
    case GUI_ZOOM_OUT:
        Command<GUI_ZOOM_OUT>::create()();
        return;
    case TOGGLE_CULLING_FREEZE:
        Command<TOGGLE_CULLING_FREEZE>::create()();
        return;
    case RANDOMIZE_VOXELS:
        Command<RANDOMIZE_VOXELS>::create()();
        return;
    // The remaining CommandNames values are declared in the enum but have
    // no Command<NAME>::create() specialization. They exist as forward
    // declarations for systems that may someday land them; firing them
    // today is a script bug. Fall through to the log path below so the
    // caller can find the gap.
    case NULL_COMMAND:
    case EXAMPLE:
    case RECORD_START:
    case RECORD_STOP:
    case STOP_VELOCITY:
    case RESHAPE_SPHERE:
    case RESHAPE_RECTANGULAR_PRISM:
    case LOCK_VOXEL_SCALE:
    case UNLOCK_VOXEL_SCALE:
    case SET_TRIXEL_COLOR:
        break;
    }
    IRE_LOG_ERROR(
        "IRCommand::fireByName: no Command<{}> specialization registered (CommandNames value {})",
        commandNameToString(name),
        static_cast<int>(name)
    );
}

} // namespace IRCommand
