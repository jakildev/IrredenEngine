#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/input/components/component_mouse_scroll.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/systems/system_widget_apply_number_field.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/time/ir_time_types.hpp>

#include <cstddef>
#include <optional>
#include <stdexcept>

namespace {

using IRComponents::C_GuiPosition;
using IRComponents::C_HitBox2DGui;
using IRComponents::C_Widget;
using IRComponents::C_WidgetNumberField;
using IRComponents::C_WidgetSlider;
using IRComponents::C_WidgetState;
using IRComponents::WidgetKind;

class WidgetSliderGestureTest : public testing::Test {
  protected:
    WidgetSliderGestureTest()
        : entityManager_{}
        , systemManager_{} {}

    void SetUp() override {
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(windowSentinel_);
        inputManager_.emplace();
        IRInput::beginSyntheticInput();
        apply_.theme_ = IRPrefab::Widget::defaultTheme();
    }

    void TearDown() override {
        inputManager_.reset();
        IRWindow::g_irglfwWindow = nullptr;
    }

    void press(float x, bool shift = false, bool control = false) {
        input_.mouseGuiTrixel_ = IRMath::vec2(x, 20.0f);
        input_.mouseLeftPressedThisFrame_ = true;
        input_.mouseLeftReleasedThisFrame_ = false;
        input_.mouseLeftDown_ = true;
        input_.shiftHeld_ = shift;
        input_.controlHeld_ = control;
        input_.topHoveredId_ = sliderId_;
        input_.tick(sliderId_, widget_, state_, hitbox_, position_);
        apply_.tick(widget_, state_, slider_, position_);
        input_.mouseLeftPressedThisFrame_ = false;
    }

    void move(float x) {
        input_.mouseGuiTrixel_ = IRMath::vec2(x, 20.0f);
        input_.mouseLeftDown_ = true;
        input_.mouseLeftReleasedThisFrame_ = false;
        input_.tick(sliderId_, widget_, state_, hitbox_, position_);
        apply_.tick(widget_, state_, slider_, position_);
    }

    void release() {
        input_.mouseLeftDown_ = false;
        input_.mouseLeftReleasedThisFrame_ = true;
        input_.tick(sliderId_, widget_, state_, hitbox_, position_);
        apply_.tick(widget_, state_, slider_, position_);
        input_.mouseLeftReleasedThisFrame_ = false;
    }

    void resetGesture(float value = 50.0f) {
        slider_.currentValue_ = value;
        state_ = {};
        input_.capturedWidgetId_ = IREntity::kNullEntity;
        input_.focusedWidgetId_ = IREntity::kNullEntity;
    }

    IREntity::EntityManager entityManager_;
    IRSystem::SystemManager systemManager_;
    std::optional<IRInput::InputManager> inputManager_;
    alignas(std::max_align_t) char windowSentinel_[1];
    IRSystem::System<IRSystem::WIDGET_INPUT> input_{};
    IRSystem::System<IRSystem::WIDGET_APPLY_SLIDER> apply_{};
    const IREntity::EntityId sliderId_ = 700u;
    C_Widget widget_{WidgetKind::SLIDER, IRMath::ivec2(100, 30)};
    C_WidgetState state_{};
    C_WidgetSlider slider_{"VALUE", 0.0f, 100.0f, 50.0f, 25.0f, 10.0f};
    C_HitBox2DGui hitbox_{100, 30};
    C_GuiPosition position_{IRMath::ivec2(0, 0)};
};

TEST_F(WidgetSliderGestureTest, DragModesUsePressTimeAnchorsAndModifiers) {
    hitbox_.hovered_ = true;

    press(20.0f);
    move(80.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 80.0f);
    release();
    move(10.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 80.0f) << "release must not replay the drag";

    resetGesture();
    press(20.0f, true);
    move(80.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 56.0f);
    input_.shiftHeld_ = false;
    move(90.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 57.0f) << "modifier mode is fixed at press";

    resetGesture();
    press(26.0f, false, true);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 30.0f);
    move(74.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 70.0f);

    resetGesture(53.0f);
    press(20.0f, true, true);
    move(80.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 60.0f)
        << "fine mapping is applied before min-anchored snapping";
}

TEST_F(WidgetSliderGestureTest, WheelAccumulatesBySignAndHonoursShift) {
    state_.hovered_ = true;
    IREntity::createEntity(IRComponents::C_MouseScroll{0.0, 0.2});
    IREntity::createEntity(IRComponents::C_MouseScroll{0.0, 9.0});
    IREntity::createEntity(IRComponents::C_MouseScroll{0.0, -3.0});
    apply_.beginTick();
    apply_.tick(widget_, state_, slider_, position_);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 60.0f);

    slider_.currentValue_ = 50.0f;
    IRInput::injectButton(
        IRInput::KeyMouseButtons::kKeyButtonLeftShift,
        IRInput::ButtonStatuses::PRESSED
    );
    inputManager_->tick();
    inputManager_->advanceInputState(IRTime::Events::INPUT);
    apply_.beginTick();
    apply_.tick(widget_, state_, slider_, position_);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 100.0f) << "Shift multiplies the net nudge by ten";
}

TEST_F(WidgetSliderGestureTest, DisabledAndSliderKeyboardInputAreNoOps) {
    hitbox_.hovered_ = true;
    widget_.disabled_ = true;
    press(90.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 50.0f);
    apply_.wheelSteps_ = 1;
    apply_.tick(widget_, state_, slider_, position_);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 50.0f);

    widget_.disabled_ = false;
    state_.hovered_ = true;
    input_.focusedWidgetId_ = sliderId_;
    IRInput::injectButton(
        IRInput::KeyMouseButtons::kKeyButtonBackspace,
        IRInput::ButtonStatuses::PRESSED
    );
    IRInput::injectButton(
        IRInput::KeyMouseButtons::kKeyButtonDelete,
        IRInput::ButtonStatuses::PRESSED
    );
    inputManager_->tick();
    inputManager_->advanceInputState(IRTime::Events::INPUT);
    apply_.wheelSteps_ = 0;
    apply_.tick(widget_, state_, slider_, position_);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 50.0f);
}

TEST_F(WidgetSliderGestureTest, ThumbDoubleClickResetsToDefault) {
    hitbox_.hovered_ = true;
    const auto thumb =
        IRPrefab::Widget::sliderThumbRect(widget_, slider_, position_, apply_.theme_);
    const float thumbCenter = static_cast<float>(thumb.pos_.x + thumb.size_.x / 2);
    press(thumbCenter);
    release();
    slider_.currentValue_ = 70.0f;
    press(thumbCenter);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 25.0f);
    EXPECT_FALSE(state_.pressed_);
}

TEST_F(WidgetSliderGestureTest, TrackDoubleClickRestoresValueAndFocusesPairedFieldNextInput) {
    hitbox_.hovered_ = true;
    const IREntity::EntityId fieldId = IRPrefab::Widget::makeNumberField(
        IRMath::ivec2(0),
        IRMath::ivec2(60, 18),
        50.0f,
        0.0f,
        100.0f,
        1.0f
    );
    slider_.fieldId_ = fieldId;
    IRPrefab::Widget::ensureFocusRequestSingleton();

    press(90.0f);
    ASSERT_FLOAT_EQ(slider_.currentValue_, 90.0f);
    release();
    press(90.0f);
    EXPECT_FLOAT_EQ(slider_.currentValue_, 50.0f);
    apply_.endTick();
    const IREntity::EntityId requested = IRPrefab::Widget::takeFocusRequest();
    ASSERT_EQ(requested, fieldId);
    // WIDGET_INPUT::beginTick consumes this same queue before ticking fields.
    // Seed the consumed result directly so this unit test does not require a
    // render manager merely to sample an otherwise irrelevant mouse position.
    input_.focusedWidgetId_ = requested;
    C_WidgetState fieldState{};
    const C_Widget &fieldWidget = IREntity::getComponent<C_Widget>(fieldId);
    const C_HitBox2DGui &fieldHitbox = IREntity::getComponent<C_HitBox2DGui>(fieldId);
    const C_GuiPosition &fieldPosition = IREntity::getComponent<C_GuiPosition>(fieldId);
    input_.tick(fieldId, fieldWidget, fieldState, fieldHitbox, fieldPosition);
    EXPECT_TRUE(fieldState.focused_);
}

TEST_F(WidgetSliderGestureTest, EmptyNumberFieldBackspaceRestoresSnappedDefault) {
    C_WidgetNumberField field{};
    field.minValue_ = 0.0f;
    field.maxValue_ = 1.0f;
    field.step_ = 0.1f;
    field.setValue(0.06f);
    field.defaultValue_ = field.value_;
    field.setValue(0.8f);
    field.editing_ = true;
    field.editBuffer_.clear();
    C_WidgetState focused{};
    focused.focused_ = true;
    IRSystem::System<IRSystem::WIDGET_APPLY_NUMBER_FIELD> applyField{};
    applyField.backspace_ = true;
    applyField.tick(C_Widget{WidgetKind::NUMBER_FIELD, IRMath::ivec2(60, 18)}, focused, field);
    EXPECT_FLOAT_EQ(field.value_, 0.1f);
    EXPECT_EQ(field.editBuffer_, "0.1");
    EXPECT_TRUE(field.editing_);
}

TEST_F(WidgetSliderGestureTest, ResetGameplayClearsSystemOwnedWidgetReferences) {
    const IRSystem::SystemId systemId = IRSystem::createSystem<IRSystem::WIDGET_INPUT>();
    auto *params =
        systemManager_.getSystemParams<IRSystem::System<IRSystem::WIDGET_INPUT>>(systemId);
    ASSERT_NE(params, nullptr);
    const IREntity::EntityId doomed = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(0),
        IRMath::ivec2(100, 30),
        "VALUE",
        0.0f,
        100.0f,
        50.0f,
        IRRender::TextSize::LARGE,
        IRPrefab::Widget::SliderOptions{25.0f, 10.0f, IREntity::kNullEntity}
    );
    params->capturedWidgetId_ = doomed;
    params->focusedWidgetId_ = doomed;
    params->topHoveredId_ = doomed;
    params->focusableWidgets_ = {doomed};
    IRPrefab::Widget::focus(doomed);

    IREntity::resetGameplay();

    EXPECT_EQ(params->capturedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(params->focusedWidgetId_, IREntity::kNullEntity);
    EXPECT_EQ(params->topHoveredId_, IREntity::kNullEntity);
    EXPECT_TRUE(params->focusableWidgets_.empty());
    EXPECT_EQ(IRPrefab::Widget::takeFocusRequest(), IREntity::kNullEntity);
}

#ifndef IR_RELEASE
TEST_F(WidgetSliderGestureTest, CppBuilderRejectsAnInvalidStepBeforeCreation) {
    const std::size_t before = entityManager_.getLiveEntityCount();
    EXPECT_THROW(
        IRPrefab::Widget::makeSlider(
            IRMath::ivec2(0),
            IRMath::ivec2(100, 30),
            "VALUE",
            0.0f,
            1.0f,
            0.5f,
            IRRender::TextSize::LARGE,
            IRPrefab::Widget::SliderOptions{std::nullopt, 0.0f, IREntity::kNullEntity}
        ),
        std::runtime_error
    );
    EXPECT_EQ(entityManager_.getLiveEntityCount(), before);
}
#endif

} // namespace
