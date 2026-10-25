#include <gtest/gtest.h>

#include <irreden/command/command_manager.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/systems/system_widget_apply_checkbox.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_apply_radio.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/widget_hotkeys.hpp>
#include <irreden/render/widgets.hpp>

#include <cstddef>
#include <optional>
#include <string>

namespace {

IRMath::vec2 offscreenMouse() {
    return IRMath::vec2(-1000.0f);
}

class WidgetKeyboardTest : public testing::Test {
  protected:
    IREntity::EntityManager entityManager_;
    IRCommand::CommandManager commandManager_;
};

class WidgetKeyboardPipelineTest : public testing::Test {
  protected:
    void SetUp() override {
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(windowSentinel_);
        inputManager_.emplace();
        IRInput::beginSyntheticInput();
        commandManager_.emplace();
        const IRSystem::SystemId input = IRSystem::createSystem<IRSystem::WIDGET_INPUT>();
        auto *params =
            systemManager_.getSystemParams<IRSystem::System<IRSystem::WIDGET_INPUT>>(input);
        ASSERT_NE(params, nullptr);
        params->mousePosition_ = &offscreenMouse;
        systemManager_.registerPipeline(
            IRTime::Events::INPUT,
            {
                input,
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_CHECKBOX>(),
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_RADIO>(),
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>(),
            }
        );
    }

    void TearDown() override {
        commandManager_.reset();
        inputManager_.reset();
        IRWindow::g_irglfwWindow = nullptr;
    }

    void frame() {
        inputManager_->tick();
        inputManager_->advanceInputState(IRTime::Events::INPUT);
        systemManager_.executePipeline(IRTime::Events::INPUT);
        inputManager_->advanceInputState(IRTime::Events::UPDATE);
        commandManager_->executeUserKeyboardCommandsAll();
    }

    void press(IRInput::KeyMouseButtons key) {
        IRInput::injectButton(key, IRInput::PRESSED);
        frame();
    }

    void release(IRInput::KeyMouseButtons key) {
        IRInput::injectButton(key, IRInput::RELEASED);
        frame();
    }

    IREntity::EntityManager entityManager_;
    IRSystem::SystemManager systemManager_;
    std::optional<IRInput::InputManager> inputManager_;
    std::optional<IRCommand::CommandManager> commandManager_;
    alignas(std::max_align_t) char windowSentinel_[1];
};

TEST_F(WidgetKeyboardPipelineTest, TabTraversesWidgetsInReadingOrderAndWraps) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(40, 50), IRMath::ivec2(30, 12), "BUTTON");
    const auto checkbox = IRPrefab::Widget::makeCheckbox(
        IRMath::ivec2(40, 10),
        IRMath::ivec2(30, 12),
        "CHECK",
        false
    );
    const auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(40, 30),
        IRMath::ivec2(30, 12),
        "SLIDER",
        0.0f,
        10.0f,
        5.0f
    );
    IRPrefab::Widget::makeTextInput(IRMath::ivec2(0), IRMath::ivec2(30, 12), "");
    IRPrefab::Widget::makeHitRegion(IRMath::ivec2(0, 5), IRMath::ivec2(30, 12));

    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), checkbox);
    release(IRInput::kKeyButtonTab);
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), slider);
    release(IRInput::kKeyButtonTab);
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), button);
    release(IRInput::kKeyButtonTab);
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), checkbox);
}

TEST_F(WidgetKeyboardPipelineTest, ShiftTabReversesAndTabYieldsToACommand) {
    const auto first =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0, 10), IRMath::ivec2(30, 12), "A", false);
    const auto last =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0, 30), IRMath::ivec2(30, 12), "B");

    press(IRInput::kKeyButtonTab);
    ASSERT_EQ(IRPrefab::Widget::focusedWidget(), first);
    release(IRInput::kKeyButtonTab);
    press(IRInput::kKeyButtonLeftShift);
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), last);
    release(IRInput::kKeyButtonTab);
    release(IRInput::kKeyButtonLeftShift);

    int commandFires = 0;
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonTab,
        [&] { ++commandFires; }
    );
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(commandFires, 1);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), last);
}

TEST_F(WidgetKeyboardPipelineTest, ToggleRadioButtonAndDropdownKeysApplyThroughPipeline) {
    const auto checkbox =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0), IRMath::ivec2(40, 14), "CHECK", false);
    const auto radio =
        IRPrefab::Widget::makeRadio(IRMath::ivec2(0, 20), IRMath::ivec2(40, 14), "RADIO", 1, 1);
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0, 40), IRMath::ivec2(40, 14), "BUTTON");
    const auto dropdown = IRPrefab::Widget::makeDropdown(
        IRMath::ivec2(0, 60),
        IRMath::ivec2(60, 14),
        {"A", "B", "C"},
        0
    );

    IRPrefab::Widget::focus(checkbox);
    frame();
    press(IRInput::kKeyButtonSpace);
    EXPECT_TRUE(IRPrefab::Widget::checkboxState(checkbox));
    release(IRInput::kKeyButtonSpace);

    IRPrefab::Widget::focus(radio);
    frame();
    press(IRInput::kKeyButtonSpace);
    EXPECT_TRUE(IRPrefab::Widget::radioSelected(radio));
    release(IRInput::kKeyButtonSpace);

    IRPrefab::Widget::focus(button);
    frame();
    press(IRInput::kKeyButtonEnter);
    EXPECT_TRUE(IRPrefab::Widget::wasClicked(button));
    release(IRInput::kKeyButtonEnter);

    IRPrefab::Widget::focus(dropdown);
    frame();
    press(IRInput::kKeyButtonSpace);
    EXPECT_TRUE(IRPrefab::Widget::isDropdownOpen(dropdown));
    release(IRInput::kKeyButtonSpace);
    press(IRInput::kKeyButtonDown);
    release(IRInput::kKeyButtonDown);
    press(IRInput::kKeyButtonDown);
    release(IRInput::kKeyButtonDown);
    press(IRInput::kKeyButtonEnter);
    EXPECT_EQ(IRPrefab::Widget::dropdownSelectedIndex(dropdown), 2);
    EXPECT_FALSE(IRPrefab::Widget::isDropdownOpen(dropdown));
}

TEST_F(WidgetKeyboardPipelineTest, HotkeysAndFocusedSliderKeysApplyThroughThePipeline) {
    const auto checkbox =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0), IRMath::ivec2(40, 14), "CHECK", false);
    const auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(0, 20),
        IRMath::ivec2(60, 14),
        "SLIDER",
        0.0f,
        10.0f,
        5.0f,
        IRRender::TextSize::LARGE,
        IRPrefab::Widget::SliderOptions{5.0f, 1.0f, IREntity::kNullEntity}
    );
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(checkbox, IRInput::kKeyButtonC, IRInput::kModifierAlt));
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(slider, IRInput::kKeyButtonS, IRInput::kModifierAlt));

    press(IRInput::kKeyButtonLeftAlt);
    press(IRInput::kKeyButtonC);
    EXPECT_TRUE(IRPrefab::Widget::checkboxState(checkbox));
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), checkbox);
    release(IRInput::kKeyButtonC);
    press(IRInput::kKeyButtonS);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), slider);
    release(IRInput::kKeyButtonS);
    release(IRInput::kKeyButtonLeftAlt);
    press(IRInput::kKeyButtonUp);
    EXPECT_FLOAT_EQ(IRPrefab::Widget::sliderValue(slider), 6.0f);
    release(IRInput::kKeyButtonUp);
    press(IRInput::kKeyButtonDelete);
    EXPECT_FLOAT_EQ(IRPrefab::Widget::sliderValue(slider), 5.0f);
}

TEST_F(WidgetKeyboardPipelineTest, DirectlyWrittenAltTabHotkeyNeverActivates) {
    const auto checkbox =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0), IRMath::ivec2(40, 14), "CHECK", false);
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0, 30), IRMath::ivec2(40, 14), "BUTTON");
    IREntity::setComponent(
        button,
        IRComponents::C_WidgetHotkey{IRInput::kKeyButtonTab, IRInput::kModifierAlt, "TAB"}
    );

    press(IRInput::kKeyButtonLeftAlt);
    press(IRInput::kKeyButtonTab);

    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), checkbox);
    EXPECT_FALSE(IREntity::getComponent<IRComponents::C_WidgetState>(button).fireAction_);
}

TEST_F(WidgetKeyboardPipelineTest, PointerFocusDoesNotCaptureKeysOrSuppressCommands) {
    const auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(0),
        IRMath::ivec2(60, 14),
        "SLIDER",
        0.0f,
        10.0f,
        5.0f,
        IRRender::TextSize::LARGE,
        IRPrefab::Widget::SliderOptions{5.0f, 1.0f, IREntity::kNullEntity}
    );
    IREntity::getComponent<IRComponents::C_HitBox2DGui>(slider).hovered_ = true;
    press(IRInput::kMouseButtonLeft);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), slider);
    EXPECT_FALSE(IRPrefab::Widget::focusVisible());
    release(IRInput::kMouseButtonLeft);
    IRPrefab::Widget::setSliderValue(slider, 5.0f);

    int commandFires = 0;
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonUp,
        [&] { ++commandFires; }
    );
    press(IRInput::kKeyButtonUp);
    EXPECT_FLOAT_EQ(IRPrefab::Widget::sliderValue(slider), 5.0f);
    EXPECT_FALSE(commandManager_->isButtonCaptured(IRInput::kKeyButtonUp));
    EXPECT_EQ(commandFires, 1);
}

TEST_F(WidgetKeyboardPipelineTest, EditorHotkeyLandsNextFrameAndLeavesPressCommandLive) {
    const auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(0),
        IRMath::ivec2(60, 14),
        "SLIDER",
        0.0f,
        10.0f,
        5.0f
    );
    const auto editor =
        IRPrefab::Widget::makeTextInput(IRMath::ivec2(0, 20), IRMath::ivec2(60, 14), "");
    ASSERT_TRUE(
        IRPrefab::Widget::setHotkey(editor, IRInput::kKeyButtonT, IRInput::kModifierAlt, "T")
    );
    int commandFires = 0;
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonT,
        [&] { ++commandFires; },
        IRInput::kModifierNone,
        IRInput::kModifierShift
    );
    IRPrefab::Widget::focus(slider);
    frame();

    press(IRInput::kKeyButtonLeftAlt);
    press(IRInput::kKeyButtonT);
    EXPECT_EQ(commandFires, 1);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), slider);
    EXPECT_FALSE(commandManager_->isKeyboardCaptured());
    frame();
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), editor);
    EXPECT_TRUE(IRPrefab::Widget::focusVisible());
    EXPECT_TRUE(commandManager_->isKeyboardCaptured());
    EXPECT_TRUE(IRPrefab::Widget::textInputValue(editor).empty());
}

TEST_F(WidgetKeyboardPipelineTest, HotkeysMatchExactlyIgnoreDisabledAndPublishHints) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(40, 14), "BUTTON");
    const auto disabled =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0, 20), IRMath::ivec2(40, 14), "OFF", false);
    IREntity::getComponent<IRComponents::C_Widget>(disabled).disabled_ = true;
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(button, IRInput::kKeyButtonM));
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(disabled, IRInput::kKeyButtonN, IRInput::kModifierAlt));

    press(IRInput::kKeyButtonLeftControl);
    press(IRInput::kKeyButtonM);
    EXPECT_FALSE(IRPrefab::Widget::wasClicked(button));
    release(IRInput::kKeyButtonM);
    release(IRInput::kKeyButtonLeftControl);
    press(IRInput::kKeyButtonM);
    EXPECT_TRUE(IRPrefab::Widget::wasClicked(button));
    release(IRInput::kKeyButtonM);

    press(IRInput::kKeyButtonLeftAlt);
    EXPECT_TRUE(IRPrefab::Widget::hintsVisible());
    press(IRInput::kKeyButtonN);
    EXPECT_FALSE(IRPrefab::Widget::checkboxState(disabled));
    EXPECT_NE(IRPrefab::Widget::focusedWidget(), disabled);
    release(IRInput::kKeyButtonN);
    release(IRInput::kKeyButtonLeftAlt);
    EXPECT_FALSE(IRPrefab::Widget::hintsVisible());

    IRPrefab::Widget::defaultTheme().hintModifier_ = IRInput::kModifierNone;
    press(IRInput::kKeyButtonLeftAlt);
    EXPECT_FALSE(IRPrefab::Widget::hintsVisible());
}

TEST_F(WidgetKeyboardPipelineTest, DestroyingFocusedWidgetClearsPublishedFocus) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(40, 14), "BUTTON");
    IRPrefab::Widget::focus(button);
    frame();
    ASSERT_EQ(IRPrefab::Widget::focusedWidget(), button);
    IREntity::destroyEntity(button);
    entityManager_.destroyMarkedEntities();
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), IREntity::kNullEntity);
    EXPECT_FALSE(IRPrefab::Widget::focusVisible());
}

TEST_F(WidgetKeyboardTest, HotkeyValidationRejectsSentinelsMouseAndFocusKeys) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(40, 20), "BUTTON");
    EXPECT_FALSE(IRPrefab::Widget::setHotkey(button, IRInput::kNullButton));
    EXPECT_FALSE(IRPrefab::Widget::setHotkey(button, IRInput::kMouseButtonLeft));
    EXPECT_FALSE(
        IRPrefab::Widget::setHotkey(button, IRInput::kKeyButtonTab, IRInput::kModifierAlt)
    );
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(button));

    EXPECT_TRUE(
        IRPrefab::Widget::setHotkey(button, IRInput::kKeyButtonB, IRInput::kModifierAlt, "B")
    );
    const auto &hotkey = IREntity::getComponent<IRComponents::C_WidgetHotkey>(button);
    EXPECT_EQ(hotkey.key_, IRInput::kKeyButtonB);
    EXPECT_EQ(hotkey.modifiers_, IRInput::kModifierAlt);
    EXPECT_EQ(hotkey.hint_, "B");

    const auto incomplete = IREntity::createEntity(
        IRComponents::C_WidgetState{},
        IRComponents::C_HitBox2DGui{IRMath::ivec2(10)}
    );
    EXPECT_FALSE(IRPrefab::Widget::setHotkey(incomplete, IRInput::kKeyButtonM));
}

TEST_F(WidgetKeyboardTest, EveryCapturedKeyIsReservedAndFocusKeysRejectEveryModifierMask) {
    const auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(0),
        IRMath::ivec2(40, 14),
        "S",
        0.0f,
        1.0f,
        0.5f
    );
    const auto checkbox =
        IRPrefab::Widget::makeCheckbox(IRMath::ivec2(0), IRMath::ivec2(40, 14), "C", false);
    const auto button = IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(40, 14), "B");
    const auto dropdown =
        IRPrefab::Widget::makeDropdown(IRMath::ivec2(0), IRMath::ivec2(40, 14), {"A"});
    for (const auto key : IRPrefab::Widget::kFocusKeys) {
        for (unsigned raw = 0; raw <= static_cast<unsigned>(IRInput::kModifierAll); ++raw) {
            const auto mods = static_cast<IRInput::KeyModifierMask>(raw);
            EXPECT_FALSE(IRPrefab::Widget::setHotkey(slider, key, mods));
            EXPECT_FALSE(IRPrefab::Widget::setHotkey(checkbox, key, mods));
            EXPECT_FALSE(IRPrefab::Widget::setHotkey(button, key, mods));
            EXPECT_FALSE(IRPrefab::Widget::setHotkey(dropdown, key, mods));
        }
    }
    const auto check = [](const auto &keys) {
        for (const auto key : keys) {
            EXPECT_TRUE(IRPrefab::Widget::isFocusKey(key));
        }
    };
    check(IRSystem::System<IRSystem::WIDGET_INPUT>::kSliderCaptured);
    check(IRSystem::System<IRSystem::WIDGET_INPUT>::kToggleCaptured);
    check(IRSystem::System<IRSystem::WIDGET_INPUT>::kPressCaptured);
    check(IRSystem::System<IRSystem::WIDGET_INPUT>::kClosedDropdownCaptured);
    check(IRSystem::System<IRSystem::WIDGET_INPUT>::kOpenDropdownCaptured);
}

TEST_F(WidgetKeyboardTest, CallbackRegistryDiesWithTheWorld) {
    int fires = 0;
    IRPrefab::Widget::registerHotkey("ACTION", IRInput::kModifierAlt, IRInput::kKeyButtonM, [&] {
        ++fires;
    });
    ASSERT_EQ(IRPrefab::Widget::getHotkeyRegistry().size(), 1u);
    entityManager_.destroyAllEntities();
    EXPECT_TRUE(IRPrefab::Widget::getHotkeyRegistry().empty());
    EXPECT_EQ(fires, 0);
}

TEST_F(WidgetKeyboardTest, NewestRegistrationDisplacesWidgetAndReportsCommand) {
    const auto first =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(40, 20), "FIRST");
    const auto second =
        IRPrefab::Widget::makeButton(IRMath::ivec2(50, 0), IRMath::ivec2(40, 20), "SECOND");
    commandManager_.createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonM,
        [] {},
        IRInput::kModifierNone,
        IRInput::kModifierShift
    );
    ASSERT_TRUE(
        IRPrefab::Widget::setHotkey(first, IRInput::kKeyButtonM, IRInput::kModifierAlt, "M")
    );
    const auto conflict =
        IRPrefab::Widget::findHotkeyConflict(IRInput::kKeyButtonM, IRInput::kModifierAlt, second);
    EXPECT_EQ(conflict.widget_, first);
    EXPECT_TRUE(conflict.command_);
    ASSERT_TRUE(
        IRPrefab::Widget::setHotkey(second, IRInput::kKeyButtonM, IRInput::kModifierAlt, "M")
    );
    EXPECT_FALSE(IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(first));
    EXPECT_TRUE(IREntity::getComponentOptional<IRComponents::C_WidgetHotkey>(second));
}

TEST_F(WidgetKeyboardTest, SliderKeyActionsUseSharedValueOperations) {
    IRSystem::System<IRSystem::WIDGET_APPLY_SLIDER> apply;
    IRComponents::C_Widget widget{IRComponents::WidgetKind::SLIDER, IRMath::ivec2(100, 20)};
    IRComponents::C_WidgetState state;
    IRComponents::C_WidgetSlider slider{"VALUE", 0.0f, 100.0f, 20.0f, 50.0f, 2.0f};
    const IRComponents::C_GuiPosition position{IRMath::ivec2(0)};

    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::MOVE_UP;
    state.keyShiftHeld_ = true;
    apply.tick(widget, state, slider, position);
    EXPECT_FLOAT_EQ(slider.currentValue_, 40.0f);
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::MAXIMUM;
    apply.tick(widget, state, slider, position);
    EXPECT_FLOAT_EQ(slider.currentValue_, 100.0f);
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::RESET;
    apply.tick(widget, state, slider, position);
    EXPECT_FLOAT_EQ(slider.currentValue_, 50.0f);
}

TEST_F(WidgetKeyboardTest, DropdownKeyboardCursorTracksLiveItemsAndPointerReopen) {
    IRSystem::System<IRSystem::WIDGET_APPLY_DROPDOWN> apply;
    apply.guiCanvasHeight_ = 720;
    IRComponents::C_Widget widget{IRComponents::WidgetKind::DROPDOWN, IRMath::ivec2(100, 20)};
    IRComponents::C_WidgetState state;
    IRComponents::C_WidgetDropdown dropdown;
    dropdown.items_ = {"A", "B", "C", "D", "E"};
    dropdown.selectedIndex_ = 1;
    IRComponents::C_HitBox2DGui hitbox{widget.size_};
    const IRComponents::C_GuiPosition position{IRMath::ivec2(0)};
    const auto tick = [&] {
        apply.tick(1, widget, state, dropdown, hitbox, position);
        state.keyAction_ = IRComponents::C_WidgetState::KeyAction::NONE;
        state.fireAction_ = false;
    };

    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::OPEN_OR_CLOSE;
    tick();
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::MOVE_DOWN;
    tick();
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::SELECT;
    tick();
    EXPECT_EQ(dropdown.selectedIndex_, 2);
    EXPECT_FALSE(dropdown.isOpen_);

    state.fireAction_ = true;
    tick();
    EXPECT_TRUE(dropdown.isOpen_);
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::MOVE_DOWN;
    tick();
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::SELECT;
    tick();
    EXPECT_EQ(dropdown.selectedIndex_, 3);
    EXPECT_FALSE(dropdown.isOpen_);

    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::OPEN_OR_CLOSE;
    tick();
    dropdown.items_ = {"A", "B", "C"};
    dropdown.selectedIndex_ = 1;
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::SELECT;
    tick();
    EXPECT_EQ(dropdown.selectedIndex_, 1);
    EXPECT_FALSE(dropdown.isOpen_);

    dropdown.items_ = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
    dropdown.selectedIndex_ = 7;
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::OPEN_OR_CLOSE;
    tick();
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::MOVE_DOWN;
    tick();
    EXPECT_EQ(apply.keyboardRow_, 8);
    EXPECT_EQ(
        dropdown.topIndex(dropdown.stripLayout(0, widget.size_.y, apply.guiCanvasHeight_)),
        1
    );
}

// A 40-item dropdown whose header sits low on a short canvas: neither side has
// room for the eight requested rows, so the strip opens upward clipped to five.
// The keyboard row walks past that window, the window follows it, Enter picks
// the highlighted absolute row, and the pointer resolves the rows painted.
TEST_F(WidgetKeyboardTest, DropdownKeyboardRowScrollsAClippedUpwardWindow) {
    using Action = IRComponents::C_WidgetState::KeyAction;
    IRSystem::System<IRSystem::WIDGET_APPLY_DROPDOWN> apply;
    apply.guiCanvasHeight_ = 100;
    apply.mouseGuiTrixel_ = offscreenMouse();
    IRComponents::C_Widget widget{IRComponents::WidgetKind::DROPDOWN, IRMath::ivec2(100, 20)};
    IRComponents::C_WidgetState state;
    IRComponents::C_WidgetDropdown dropdown;
    dropdown.itemHeight_ = 10;
    for (int i = 0; i < 40; ++i) {
        dropdown.items_.push_back("ITEM " + std::to_string(i));
    }
    dropdown.selectedIndex_ = 0;
    IRComponents::C_HitBox2DGui hitbox{widget.size_};
    const IRComponents::C_GuiPosition position{IRMath::ivec2(0, 55)};
    const auto tick = [&](Action action) {
        state.keyAction_ = action;
        apply.tick(1, widget, state, dropdown, hitbox, position);
        state.keyAction_ = Action::NONE;
        state.fireAction_ = false;
    };
    const IRComponents::C_WidgetDropdown::StripLayout layout =
        dropdown.stripLayout(position.pos_.y, widget.size_.y, apply.guiCanvasHeight_);
    ASSERT_EQ(layout.rows_, 5);
    ASSERT_EQ(layout.offsetY_, -50);

    tick(Action::OPEN_OR_CLOSE);
    ASSERT_TRUE(dropdown.isOpen_);
    EXPECT_EQ(hitbox.offset_, IRMath::ivec2(0, -50)) << "the hitbox grows up over the strip";
    EXPECT_EQ(hitbox.size_, IRMath::ivec2(100, 70));

    for (int press = 0; press < 9; ++press) {
        tick(Action::MOVE_DOWN);
    }
    EXPECT_EQ(apply.keyboardRow_, 9);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 9) << "the highlight is the absolute row";
    EXPECT_EQ(dropdown.topIndex(layout), 5) << "row 9 sits on the window's last row";

    // The pointer over the strip's first and last painted rows names items 5 and 9.
    apply.mouseGuiTrixel_ = IRMath::vec2(50.0f, 55.0f + dropdown.itemCenterOffsetY(layout, 5));
    tick(Action::NONE);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 5);
    apply.mouseGuiTrixel_ = IRMath::vec2(50.0f, 55.0f + dropdown.itemCenterOffsetY(layout, 9));
    tick(Action::NONE);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 9);
    apply.mouseGuiTrixel_ = offscreenMouse();

    tick(Action::SELECT);
    EXPECT_EQ(dropdown.selectedIndex_, 9);
    EXPECT_FALSE(dropdown.isOpen_);
    EXPECT_EQ(hitbox.offset_, IRMath::ivec2(0)) << "closing restores the header hitbox";
    EXPECT_EQ(hitbox.size_, widget.size_);
}

TEST_F(WidgetKeyboardTest, EmptyDropdownKeyboardSelectionRemainsEmpty) {
    IRSystem::System<IRSystem::WIDGET_APPLY_DROPDOWN> apply;
    apply.guiCanvasHeight_ = 720;
    IRComponents::C_Widget widget{IRComponents::WidgetKind::DROPDOWN, IRMath::ivec2(100, 20)};
    IRComponents::C_WidgetState state;
    IRComponents::C_WidgetDropdown dropdown;
    IRComponents::C_HitBox2DGui hitbox{widget.size_};
    const IRComponents::C_GuiPosition position{IRMath::ivec2(0)};

    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::OPEN_OR_CLOSE;
    apply.tick(1, widget, state, dropdown, hitbox, position);
    state.keyAction_ = IRComponents::C_WidgetState::KeyAction::SELECT;
    apply.tick(1, widget, state, dropdown, hitbox, position);

    EXPECT_EQ(dropdown.selectedIndex_, -1);
    EXPECT_TRUE(dropdown.isOpen_);
}

TEST_F(WidgetKeyboardTest, PerButtonCaptureIsIndependentOfKeyboardCapture) {
    const std::array captured{IRInput::kKeyButtonUp, IRInput::kKeyButtonDelete};
    commandManager_.setCapturedButtons(captured);
    EXPECT_TRUE(commandManager_.isButtonCaptured(IRInput::kKeyButtonUp));
    EXPECT_TRUE(commandManager_.isButtonCaptured(IRInput::kKeyButtonDelete));
    EXPECT_FALSE(commandManager_.isButtonCaptured(IRInput::kKeyButtonDown));
    EXPECT_FALSE(commandManager_.isKeyboardCaptured());
    commandManager_.setCapturedButtons(std::span<const IRInput::KeyMouseButtons>{});
    EXPECT_FALSE(commandManager_.isButtonCaptured(IRInput::kKeyButtonUp));
}

TEST(WidgetKeyboardModifiersTest, RequiredBlockedAndExtraBitsMatch) {
    EXPECT_TRUE(
        IRInput::modifiersMatch(
            IRInput::kModifierControl,
            IRInput::kModifierShift,
            IRInput::kModifierControl | IRInput::kModifierAlt
        )
    );
    EXPECT_FALSE(
        IRInput::modifiersMatch(
            IRInput::kModifierControl,
            IRInput::kModifierShift,
            IRInput::kModifierControl | IRInput::kModifierShift
        )
    );
}

} // namespace
