#ifndef CAMERA_GESTURE_H
#define CAMERA_GESTURE_H

#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>

namespace IRPrefab::Camera {

struct ButtonActivity {
    bool pressed_ = false;
    bool held_ = false;
    bool released_ = false;

    bool active() const {
        return pressed_ || held_ || released_;
    }
};

struct GestureInput {
    ButtonActivity left_;
    ButtonActivity middle_;
    bool alt_ = false;
    bool control_ = false;
    bool space_ = false;
};

struct GestureControls {
    bool leftRotate_ = false;
    bool leftPan_ = false;
    bool middleRotate_ = false;
    bool middlePan_ = false;
};

// Snapshot before camera controls run in RENDER. PRESSED and RELEASED queries
// both include a combined press/release event, while HELD does not.
inline GestureInput gestureInput() {
    const auto button = [](IRInput::KeyMouseButtons key) {
        return ButtonActivity{
            IRInput::checkKeyMouseButton(key, IRInput::PRESSED),
            IRInput::checkKeyMouseButton(key, IRInput::HELD),
            IRInput::checkKeyMouseButton(key, IRInput::RELEASED)
        };
    };
    const auto pressedOrHeld = [&](IRInput::KeyMouseButtons key) {
        const auto state = button(key);
        return state.pressed_ || state.held_;
    };
    return {
        button(IRInput::kMouseButtonLeft),
        button(IRInput::kMouseButtonMiddle),
        pressedOrHeld(IRInput::kKeyButtonLeftAlt) || pressedOrHeld(IRInput::kKeyButtonRightAlt),
        pressedOrHeld(IRInput::kKeyButtonLeftControl) ||
            pressedOrHeld(IRInput::kKeyButtonRightControl),
        pressedOrHeld(IRInput::kKeyButtonSpace)
    };
}

inline GestureControls registeredGestureControls() {
    const auto registered = [](IRSystem::SystemName name) {
        return IRSystem::findSystem(name) != IRSystem::kNullSystemId;
    };
    return {
        registered(IRSystem::CAMERA_KEY_DRAG_ROTATE),
        registered(IRSystem::CAMERA_KEY_DRAG_PAN),
        registered(IRSystem::CAMERA_MOUSE_ROTATE),
        registered(IRSystem::CAMERA_MOUSE_PAN)
    };
}

// Owned by each raw-input tool/system and sampled every INPUT tick, including
// while that tool is disabled. A camera press owns the release too, even if its
// modifier released first. A registered navigation modifier suppresses previews
// even before clicking. If camera navigation interrupts a left edit, the
// rest of that left gesture remains unavailable to the tool. The caller settles
// any active edit when update returns true; it must not start another one until
// false. Custom navigation bindings are supplied explicitly, never guessed.
class GestureOwnership {
  public:
    bool update(
        const GestureInput &input, const GestureControls &controls, bool extraCameraInput = false
    ) {
        if (!input.left_.active()) {
            m_leftCamera = false;
            m_leftSuppressed = false;
        } else if (input.left_.pressed_) {
            m_leftSuppressed = false;
            m_leftCamera = (controls.leftRotate_ && input.alt_) ||
                           (controls.leftPan_ && input.space_ && !input.alt_);
        }
        if (!input.middle_.active()) {
            m_middleCamera = false;
        } else if (input.middle_.pressed_) {
            m_middleCamera = input.control_ ? controls.middleRotate_ : controls.middlePan_;
        }
        const bool navigationModifier = (controls.leftRotate_ && input.alt_) ||
                                        (controls.leftPan_ && input.space_ && !input.alt_);
        const bool camera =
            m_leftCamera || m_middleCamera || navigationModifier || extraCameraInput;
        if (camera && input.left_.active())
            m_leftSuppressed = true;
        return camera || m_leftSuppressed;
    }

    bool poll(bool extraCameraInput = false) {
        return update(gestureInput(), registeredGestureControls(), extraCameraInput);
    }

  private:
    bool m_leftCamera = false;
    bool m_middleCamera = false;
    bool m_leftSuppressed = false;
};

} // namespace IRPrefab::Camera

#endif /* CAMERA_GESTURE_H */
