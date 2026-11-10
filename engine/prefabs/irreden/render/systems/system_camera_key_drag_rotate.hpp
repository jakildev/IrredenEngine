#ifndef SYSTEM_CAMERA_KEY_DRAG_ROTATE_H
#define SYSTEM_CAMERA_KEY_DRAG_ROTATE_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/camera.hpp>
#include <irreden/render/camera_gesture.hpp>
#include <irreden/render/components/component_camera.hpp>
#include <irreden/render/cursor_pivot.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Alt + left drag orbits the clicked surface; Shift inverts the pivot policy.
template <> struct System<CAMERA_KEY_DRAG_ROTATE> : IRPrefab::CursorPivot::OrbitAnchor {
    static constexpr float kPixelsPerRevolution = 1280.0f;
    bool dragging_ = false;
    bool (*canStartGesture_)() = nullptr;
    bool latchByDefault_ = true;
    IRPrefab::CursorPivot::FocusOptions pivotOptions_;
    vec2 dragStartMouse_ = vec2(0.0f);
    float dragStartYaw_ = 0.0f;
    float dragStartPitch_ = 0.0f;

    void tick(C_Camera &) {}

    void endTick() {
        const bool pressed =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::PRESSED);
        const bool down = IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::HELD);
        const bool released =
            IRInput::checkKeyMouseButton(IRInput::kMouseButtonLeft, IRInput::RELEASED);
        const bool modifier = IRPrefab::Camera::gestureInput().alt_;
        const bool shift = IRInput::checkKeyMouseModifiers(IRInput::kModifierShift);
        if (pressed && modifier && !dragging_ && (!canStartGesture_ || canStartGesture_())) {
            dragging_ = true;
            dragStartMouse_ = IRInput::getMousePositionScreen();
            dragStartYaw_ = IRPrefab::Camera::getYaw();
            dragStartPitch_ = IRPrefab::Camera::getPitch();
            begin(shift != latchByDefault_, pivotOptions_);
        }
        if (dragging_ && down) {
            const vec2 delta = IRInput::getMousePositionScreen() - dragStartMouse_;
            IRPrefab::Camera::setYawPitch(
                dragStartYaw_ + delta.x / kPixelsPerRevolution * IRMath::kTwoPi,
                dragStartPitch_ - delta.y / kPixelsPerRevolution * IRMath::kTwoPi
            );
            updateIndicator();
        }
        if (dragging_ && (released || (!pressed && !down))) {
            end();
            dragging_ = false;
        }
    }

    static SystemId create() {
        const SystemId id =
            registerSystem<CAMERA_KEY_DRAG_ROTATE, C_Camera, MainThread>("CameraKeyDragRotate");
        auto *params = getSystemParams<System<CAMERA_KEY_DRAG_ROTATE>>(id);
        IREntity::getEntityManager().registerPreDestroyHook([params](IREntity::EntityId destroyed) {
            if (params->pivotIndicator_ == destroyed)
                params->dragging_ = false;
            params->onEntityDestroyed(destroyed);
        });
        return id;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CAMERA_KEY_DRAG_ROTATE_H */
