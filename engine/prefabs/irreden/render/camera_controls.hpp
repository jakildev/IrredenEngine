#ifndef IR_PREFAB_CAMERA_CONTROLS_H
#define IR_PREFAB_CAMERA_CONTROLS_H

// Bundle entry points for camera-control systems and keyboard commands.
// A creation calls standardControlSystems() to splice the full mouse +
// trackpad gesture set into its RENDER pipeline, and
// registerStandardKeyboardCommands() for the matching WASD / +/- /
// Escape bindings.
//
// Control scheme:
//   Pan         Space + left-drag  (mouse + trackpad)
//                or  middle-drag   (mouse only, legacy)
//   Rotate yaw  Alt + left-drag    (mouse + trackpad)
//                or  Ctrl + middle-drag (mouse only, legacy)
//   Zoom        scroll wheel / two-finger scroll
//   Pan         WASD               (keyboard)
//   Zoom        +/-                (keyboard)
//   Close       Escape             (keyboard)

#include <irreden/ir_system.hpp>
#include <irreden/ir_command.hpp>

#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_camera_mouse_rotate.hpp>
#include <irreden/render/systems/system_camera_key_drag_pan.hpp>
#include <irreden/render/systems/system_camera_key_drag_rotate.hpp>
#include <irreden/render/systems/system_camera_scroll_zoom.hpp>
#include <irreden/common/command_suite_camera.hpp>

#include <list>

namespace IRPrefab::Camera {

/// Standard mouse + trackpad camera-control systems. Returns a list a
/// creation can splice into its RENDER pipeline (the systems all
/// belong on the render side except CAMERA_SCROLL_ZOOM, which is
/// included here for one-call ergonomics — placing it in RENDER works
/// because ephemeral C_MouseScroll entities live one tick and have
/// not yet been collected by the lifetime sweep at RENDER start).
///
/// Bundles the modifier+left-drag systems (Space=pan, Alt=rotate)
/// and the legacy middle-drag systems (middle-drag=pan, Ctrl+middle-
/// drag=rotate) so mouse-only and trackpad users both have a working
/// gesture set.
inline std::list<IRSystem::SystemId> standardControlSystems() {
    return {
        IRSystem::System<IRSystem::CAMERA_MOUSE_PAN>::create(),
        IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>::create(),
        IRSystem::System<IRSystem::CAMERA_KEY_DRAG_PAN>::create(),
        IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>::create(),
        IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create(),
    };
}

/// Standard keyboard bindings (WASD pan, +/- zoom, Escape close).
/// Thin wrapper around IRCommand::registerCameraCommands so demos can
/// pair the systems-side bundle above with a single matching command-
/// side call.
inline void registerStandardKeyboardCommands() {
    IRCommand::registerCameraCommands();
}

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_CONTROLS_H */
