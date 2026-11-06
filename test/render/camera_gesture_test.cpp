#include <gtest/gtest.h>

#include <irreden/render/camera_gesture.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_window.hpp>
#include <cstdint>
#include <optional>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/system/system_manager.hpp>
#include <irreden/render/systems/system_camera_key_drag_rotate.hpp>

namespace {
using namespace IRPrefab::Camera;
constexpr ButtonActivity kPress{true, true, false};
constexpr ButtonActivity kHeld{false, true, false};
constexpr ButtonActivity kRelease{false, false, true};
constexpr GestureControls kAllControls{true, true, true, true};

TEST(CameraGesture, MissingControlsLeaveModifierClicksAvailable) {
    GestureOwnership owner;
    GestureInput input{kPress, kPress, true, true, true};
    EXPECT_FALSE(owner.update(input, {}));
    input.left_ = kHeld;
    input.middle_ = kRelease;
    EXPECT_FALSE(owner.update(input, {}));
}

TEST(CameraGesture, ModifierFirstReleaseKeepsLeftGestureOwnedThroughRelease) {
    GestureOwnership owner;
    GestureInput input{kPress, {}, true};
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.alt_ = false;
    input.left_ = kHeld;
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.left_ = kRelease;
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.left_ = {};
    EXPECT_FALSE(owner.update(input, kAllControls));
    input.left_ = kPress;
    EXPECT_FALSE(owner.update(input, kAllControls));
}

TEST(CameraGesture, CombinedClickIsOwnedWithoutStickingIntoNextInputTick) {
    GestureOwnership owner;
    GestureInput input{{true, false, true}, {}, false, false, true};
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.space_ = false;
    // A distinct click can arrive immediately, without an intervening neutral tick.
    EXPECT_FALSE(owner.update(input, kAllControls));
    EXPECT_FALSE(owner.update({}, kAllControls));
}

TEST(CameraGesture, NavigationInterruptsAnEditUntilLeftReturnsToNeutral) {
    GestureOwnership owner;
    GestureInput input{kPress};
    EXPECT_FALSE(owner.update(input, kAllControls));
    input.left_ = kHeld;
    input.middle_ = kPress;
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.middle_ = kRelease;
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.middle_ = {};
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.left_ = kRelease;
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_FALSE(owner.update({}, kAllControls));
}

TEST(CameraGesture, NavigationModifierInterruptsHeldEditThroughModifierFirstRelease) {
    GestureOwnership owner;
    GestureInput input{kPress};
    EXPECT_FALSE(owner.update(input, kAllControls));
    input.left_ = kHeld;
    input.alt_ = true;
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.alt_ = false;
    EXPECT_TRUE(owner.update(input, kAllControls));
    input.left_ = kRelease;
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_FALSE(owner.update({}, kAllControls));
    input.left_ = kPress;
    EXPECT_TRUE(owner.update(input, {}, true));
    input.left_ = kHeld;
    EXPECT_TRUE(owner.update(input, {}, false));
    EXPECT_FALSE(owner.update({}, {}));
}

TEST(CameraGesture, RegisteredNavigationModifierBlocksHoverBeforeClick) {
    GestureOwnership owner;
    GestureInput input;
    input.alt_ = true;
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_FALSE(owner.update(input, {}));
    input.alt_ = false;
    input.space_ = true;
    EXPECT_TRUE(owner.update(input, kAllControls));
    EXPECT_FALSE(owner.update(input, {}));
    EXPECT_FALSE(owner.update({}, kAllControls));
}

TEST(CameraGesture, ChordsMatchRegisteredCameraControlPrecedence) {
    GestureOwnership owner;
    GestureInput altSpace{kPress, {}, true, false, true};
    EXPECT_FALSE(owner.update(altSpace, {false, true, false, false}));
    EXPECT_TRUE(owner.update(altSpace, {true, false, false, false}));
    owner.update({}, {});
    GestureInput controlMiddle{{}, kPress, false, true};
    EXPECT_FALSE(owner.update(controlMiddle, {false, false, false, true}));
    EXPECT_TRUE(owner.update(controlMiddle, {false, false, true, false}));
}

class CameraGestureInputTest : public testing::Test {
  protected:
    void SetUp() override {
        // Same headless window sentinel as the widget input pipeline fixtures:
        // joystick discovery does not access window storage.
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(std::uintptr_t{1});
        input_.emplace();
        input_->beginSyntheticInput();
    }
    void TearDown() override {
        input_.reset();
        IRWindow::g_irglfwWindow = nullptr;
    }
    IREntity::EntityManager entities_;
    std::optional<IRInput::InputManager> input_;
};

TEST_F(CameraGestureInputTest, CombinedModifierAndPointerEventsReachOwnershipSampler) {
    for (const auto key :
         {IRInput::kKeyButtonLeftAlt,
          IRInput::kKeyButtonRightAlt,
          IRInput::kKeyButtonLeftControl,
          IRInput::kKeyButtonRightControl,
          IRInput::kKeyButtonSpace}) {
        const bool control =
            key == IRInput::kKeyButtonLeftControl || key == IRInput::kKeyButtonRightControl;
        const auto pointer = control ? IRInput::kMouseButtonMiddle : IRInput::kMouseButtonLeft;
        input_->injectButton(key, IRInput::PRESSED);
        input_->injectButton(pointer, IRInput::PRESSED);
        input_->injectButton(pointer, IRInput::RELEASED);
        input_->injectButton(key, IRInput::RELEASED);
        input_->tick();
        input_->advanceInputState(IRTime::Events::INPUT);
        const auto snapshot = gestureInput();
        EXPECT_TRUE(snapshot.left_.pressed_ || snapshot.middle_.pressed_);
        EXPECT_TRUE(snapshot.left_.released_ || snapshot.middle_.released_);
        EXPECT_TRUE(snapshot.alt_ || snapshot.control_ || snapshot.space_);
        GestureOwnership owner;
        EXPECT_TRUE(owner.update(snapshot, kAllControls));
        input_->advanceInputState(IRTime::Events::INPUT);
        EXPECT_FALSE(owner.update(gestureInput(), kAllControls));
    }
}

TEST(CameraGesture, LiveRegistrationReportsOnlyInstalledControls) {
    IREntity::EntityManager entities;
    IRSystem::SystemManager systems;
    const auto absent = registeredGestureControls();
    EXPECT_FALSE(absent.leftRotate_);
    EXPECT_FALSE(absent.leftPan_);
    EXPECT_FALSE(absent.middleRotate_);
    EXPECT_FALSE(absent.middlePan_);
    IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>::create();
    const auto present = registeredGestureControls();
    EXPECT_TRUE(present.leftRotate_);
    EXPECT_FALSE(present.leftPan_);
    EXPECT_FALSE(present.middleRotate_);
    EXPECT_FALSE(present.middlePan_);
}

} // namespace
