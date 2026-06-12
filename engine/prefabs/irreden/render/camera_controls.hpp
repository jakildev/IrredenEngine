#ifndef IR_PREFAB_CAMERA_CONTROLS_H
#define IR_PREFAB_CAMERA_CONTROLS_H

// Bundle entry points for camera-control systems and keyboard commands.
// A creation calls standardControlSystems() to splice the drag+pan+rotate
// set into its RENDER pipeline, registerStandardKeyboardCommands() for
// the matching WASD / +/- / Escape bindings, and registers
// CAMERA_SCROLL_ZOOM itself in the INPUT pipeline:
//
//   IRSystem::System<IRSystem::CAMERA_SCROLL_ZOOM>::create()
//
// Scroll zoom must live in INPUT so C_MouseScroll entities are consumed
// before LIFETIME destroys them in UPDATE. standardControlSystems() does
// not include it because RENDER placement silently drops scroll events in
// any creation that registers LIFETIME in UPDATE.
//
// Control scheme:
//   Pan              Space + left-drag  (mouse + trackpad)
//                     or  middle-drag   (mouse only, legacy)
//   Orbit (yaw+pitch) Alt + left-drag  (mouse + trackpad)
//                     or  Ctrl + middle-drag (mouse only, legacy)
//   Zoom             scroll wheel / two-finger scroll  (register in INPUT)
//   Pan              WASD               (keyboard)
//   Zoom             +/-                (keyboard)
//   Close            Escape             (keyboard)

#include <irreden/ir_system.hpp>
#include <irreden/ir_command.hpp>

#include <irreden/render/systems/system_camera_mouse_pan.hpp>
#include <irreden/render/systems/system_camera_mouse_rotate.hpp>
#include <irreden/render/systems/system_camera_key_drag_pan.hpp>
#include <irreden/render/systems/system_camera_key_drag_rotate.hpp>
#include <irreden/common/command_suite_camera.hpp>

#include <list>

namespace IRPrefab::Camera {

/// Mouse + trackpad drag systems for the RENDER pipeline.
/// Bundles the modifier+left-drag systems (Space=pan, Alt=rotate) and
/// the legacy middle-drag systems (middle-drag=pan, Ctrl+middle=rotate).
/// Callers must separately register CAMERA_SCROLL_ZOOM in INPUT — see
/// the file-level comment above for why and how.
inline std::list<IRSystem::SystemId> standardControlSystems() {
    return {
        IRSystem::System<IRSystem::CAMERA_MOUSE_PAN>::create(),
        IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>::create(),
        IRSystem::System<IRSystem::CAMERA_KEY_DRAG_PAN>::create(),
        IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>::create(),
    };
}

// Pairs with standardControlSystems(); wraps IRCommand::registerCameraCommands.
inline void registerStandardKeyboardCommands() {
    IRCommand::registerCameraCommands();
}

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_CONTROLS_H */
