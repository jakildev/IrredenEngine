#ifndef SYSTEM_CAMERA_MOUSE_ROTATE_H
#define SYSTEM_CAMERA_MOUSE_ROTATE_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/cursor_pivot.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Ctrl + middle-mouse drag: orbit camera (yaw + pitch). Horizontal drag
// controls Z-yaw, vertical drag controls X-pitch.
//   - Ctrl + middle-drag         → Z-yaw about the EXACT screen center (the
//                                   CAMERA_CENTER default; no explicit focus).
//   - Ctrl + Shift + middle-drag → Z-yaw about the world point under the CURSOR
//                                   (pinned via IRRender::setRotationPivotFocus,
//                                   captured once at drag start; reverts to the
//                                   screen-center default when the drag ends).
// Middle drag without Ctrl falls through to CAMERA_MOUSE_PAN (unchanged).
//
// The cursor pivot latches the CLICKED SURFACE point at its true depth
// (#2548, via IRPrefab::CursorPivot) and shows a marker there for the duration
// of the drag. `latchByDefault_` swaps which chord gets the cursor latch, so a
// demo can compare screen-center vs cursor-latched rotation live without a
// restart; it is off by default, leaving the chords exactly as documented above.
template <> struct System<CAMERA_MOUSE_ROTATE> {
    static constexpr float kPixelsPerRevolution = 1280.0f;

    bool dragging_ = false;
    bool cursorPivot_ = false;
    // When set, a plain Ctrl+middle-drag latches the cursor pivot and
    // Ctrl+Shift+middle-drag takes the screen-center default (the chords swap).
    bool latchByDefault_ = false;
    // Spawned on the first cursor-pivot drag, then reused and re-hidden —
    // never respawned, so a drag costs no structural change after the first.
    IREntity::EntityId pivotIndicator_ = IREntity::kNullEntity;
    vec2 dragStartMouse_ = vec2(0.0f);
    float dragStartYaw_ = 0.0f;
    float dragStartPitch_ = 0.0f;

    void tick(C_Camera &) {}

    void endTick() {
        const bool middlePressed =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::PRESSED);
        const bool middleDown =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonMiddle, IRInput::HELD);
        const bool ctrlHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierControl);
        const bool shiftHeld = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);

        if (middlePressed && ctrlHeld && !dragging_) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartYaw_ = IRPrefab::Camera::getYaw();
            dragStartPitch_ = IRPrefab::Camera::getPitch();
            // Cursor-pivot mode: pin the SURFACE point under the cursor at its
            // true depth — captured once, on mouse-down — so yaw rotates about
            // the feature that was clicked. The other chord clears any focus so
            // yaw rotates about the screen-center default instead.
            cursorPivot_ = (shiftHeld != latchByDefault_);
            if (cursorPivot_) {
                const vec3 focusWorld = IRPrefab::CursorPivot::resolveFocusWorld(pivotIndicator_);
                IRRender::setRotationPivotFocus(focusWorld);
                if (pivotIndicator_ == IREntity::kNullEntity) {
                    pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
                }
                IRPrefab::CursorPivot::showIndicator(pivotIndicator_, focusWorld);
            } else {
                IRRender::clearRotationPivotFocus();
            }
        }

        if (dragging_ && middleDown) {
            const vec2 currentMouse = IRInput::getMousePositionScreen();
            const float deltaX = currentMouse.x - dragStartMouse_.x;
            const float deltaY = currentMouse.y - dragStartMouse_.y;
            const float yawDelta = (deltaX / kPixelsPerRevolution) * IRMath::kTwoPi;
            // negate: screen +Y is down; pitch up requires negative deltaY
            const float pitchDelta = -(deltaY / kPixelsPerRevolution) * IRMath::kTwoPi;
            IRPrefab::Camera::setYawPitch(dragStartYaw_ + yawDelta, dragStartPitch_ + pitchDelta);
        } else {
            if (dragging_ && cursorPivot_) {
                // Drag ended: revert to the screen-center default and take the
                // marker back down (hidden, not destroyed — the next drag
                // reuses the same entity).
                IRRender::clearRotationPivotFocus();
                IRPrefab::CursorPivot::hideIndicator(pivotIndicator_);
                cursorPivot_ = false;
            }
            dragging_ = false;
        }
    }

    static SystemId create() {
        // MainThread is load-bearing, not documentation: `endTick` spawns the
        // indicator with an eager `IREntity::createEntity` and immediately
        // `getComponent`s the id it got back. Both are main-thread-only (see
        // engine/entity/CLAUDE.md) — a multi-system pipeline group fans every
        // member out onto a worker via IRJob::parallelFor, `endTick` included,
        // regardless of the system's own Concurrency. The tag makes that a
        // boot-time FATAL in `validateAllPipelineGroups` instead of a
        // heisenbug. Not `Spawns`: T-225 lifted MUTATOR_IN_PARALLEL_GROUP, so
        // it would document the mutation without preventing it.
        return registerSystem<CAMERA_MOUSE_ROTATE, C_Camera, MainThread>("CameraMouseRotate");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_MOUSE_ROTATE_H */
