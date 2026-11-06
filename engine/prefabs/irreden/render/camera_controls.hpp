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
//                     or  middle-drag   (mouse only)
//   Orbit (yaw+pitch) Alt + left-drag  (mouse + trackpad)
//                     or  Ctrl + middle-drag (mouse only)
//   Zoom             scroll wheel / two-finger scroll  (register in INPUT)
//   Pan              WASD               (keyboard)
//   Zoom             +/-                (keyboard)
//   Close            Escape             (keyboard)
//
// Raw-input authoring tools poll Camera::GestureOwnership during INPUT so a
// camera drag cannot also edit the scene. GIZMO_DRAG adopts it automatically;
// custom tools own their edit cancellation/commit policy.

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
/// the middle-drag systems (middle-drag=pan, Ctrl+middle=rotate).
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
// Takes the same registration-time overrides — a creation that owns Escape
// itself passes `{.omit_ = {IRCommand::CLOSE_WINDOW}}` rather than dropping to
// hand-registering the rest of the suite. That is how a `IRPrefab::SettingsMenu`
// adopter keeps Escape free for the menu.
inline void registerStandardKeyboardCommands(const IRCommand::BindingOverrides &overrides) {
    IRCommand::registerCameraCommands(overrides);
}

inline void registerStandardKeyboardCommands() {
    IRCommand::registerCameraCommands();
}

/// Cursor anchoring defaults on for both orbit gestures. Shift reverses the
/// policy for one drag. Changes apply at the next press; no view is moved here.
/// Absent systems are ignored; the query returns false if neither exists.
inline void setCursorPivotByDefault(bool enabled) {
    const auto mouse = IRSystem::findSystem(IRSystem::CAMERA_MOUSE_ROTATE);
    if (mouse != IRSystem::kNullSystemId)
        IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(mouse)
            ->latchByDefault_ = enabled;
    const auto key = IRSystem::findSystem(IRSystem::CAMERA_KEY_DRAG_ROTATE);
    if (key != IRSystem::kNullSystemId)
        IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(key)
            ->latchByDefault_ = enabled;
}

inline bool cursorPivotByDefault() {
    const auto mouse = IRSystem::findSystem(IRSystem::CAMERA_MOUSE_ROTATE);
    if (mouse != IRSystem::kNullSystemId)
        return IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(mouse)
            ->latchByDefault_;
    const auto key = IRSystem::findSystem(IRSystem::CAMERA_KEY_DRAG_ROTATE);
    return key != IRSystem::kNullSystemId &&
           IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(key)
               ->latchByDefault_;
}

/// Configure both registered orbit gestures. A creation with custom rendered
/// geometry supplies a surface resolver matching its authoring/picking contract.
inline void setCursorPivotOptions(const CursorPivot::FocusOptions &options) {
    const auto mouse = IRSystem::findSystem(IRSystem::CAMERA_MOUSE_ROTATE);
    if (mouse != IRSystem::kNullSystemId)
        IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(mouse)
            ->pivotOptions_ = options;
    const auto key = IRSystem::findSystem(IRSystem::CAMERA_KEY_DRAG_ROTATE);
    if (key != IRSystem::kNullSystemId)
        IRSystem::getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(key)
            ->pivotOptions_ = options;
}

} // namespace IRPrefab::Camera

#endif /* IR_PREFAB_CAMERA_CONTROLS_H */
