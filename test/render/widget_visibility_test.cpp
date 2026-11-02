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
#include <irreden/render/gui_layout_audit.hpp>
#include <irreden/render/systems/system_widget_render_label.hpp>
#include <irreden/render/widgets.hpp>

#include "widget_input_test_utils.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace {

IRMath::vec2 offscreenMouse() {
    return IRMath::vec2(-1000.0f);
}

class WidgetVisibilityTest : public testing::Test {
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
        WidgetInputTestUtils::runFrame(*inputManager_, systemManager_, *commandManager_);
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

TEST_F(WidgetVisibilityTest, HiddenWidgetsCannotHoverTabFocusOrActivateHotkeys) {
    const auto hidden =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(60, 20), "HIDDEN");
    const auto shown =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0, 30), IRMath::ivec2(60, 20), "SHOWN");
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(hidden, IRInput::kKeyButtonM));
    IRPrefab::Widget::setVisible(hidden, false);
    // Hitbox tests can still publish geometric overlap: routing excludes it.
    IREntity::getComponent<IRComponents::C_HitBox2DGui>(hidden).hovered_ = true;
    EXPECT_EQ(IRPrefab::Widget::topmostHoveredWidget(), IREntity::kNullEntity);
    IRPrefab::Widget::focus(hidden);
    frame();
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), IREntity::kNullEntity);
    press(IRInput::kKeyButtonTab);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), shown);
    release(IRInput::kKeyButtonTab);
    press(IRInput::kKeyButtonM);
    EXPECT_FALSE(IRPrefab::Widget::wasClicked(hidden));
    EXPECT_FALSE(IRPrefab::Widget::isHovered(hidden));
}

TEST_F(WidgetVisibilityTest, HideShowCancelsKeyboardFocusWithoutLosingAuthoredText) {
    const auto editor =
        IRPrefab::Widget::makeTextInput(IRMath::ivec2(10, 30), IRMath::ivec2(60, 20), "TREE");
    IRPrefab::Widget::focus(editor);
    frame();
    ASSERT_TRUE(commandManager_->isKeyboardCaptured());
    IRPrefab::Widget::setVisible(editor, false);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), IREntity::kNullEntity);
    // Showing before the next input tick must not resurrect the old focus.
    IRPrefab::Widget::setVisible(editor, true);
    press(IRInput::kKeyButtonA);
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), IREntity::kNullEntity);
    EXPECT_FALSE(commandManager_->isKeyboardCaptured());
    EXPECT_EQ(IRPrefab::Widget::textInputValue(editor), "TREE");
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_GuiPosition>(editor).pos_,
        IRMath::ivec2(10, 30)
    );
}

TEST_F(WidgetVisibilityTest, HideShowCancelsMouseCaptureAndQueuedClick) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(0), IRMath::ivec2(60, 20), "BUTTON");
    IREntity::getComponent<IRComponents::C_HitBox2DGui>(button).hovered_ = true;
    press(IRInput::kMouseButtonLeft);
    ASSERT_TRUE(IRPrefab::Widget::isPressed(button));
    IRPrefab::Widget::setVisible(button, false);
    IRPrefab::Widget::setVisible(button, true);
    release(IRInput::kMouseButtonLeft);
    EXPECT_FALSE(IRPrefab::Widget::wasClicked(button));
    EXPECT_FALSE(IRPrefab::Widget::isPressed(button));
    EXPECT_FALSE(IRPrefab::Widget::isHovered(button));
}

TEST_F(WidgetVisibilityTest, HideDropdownClosesStripPreservingSelectionLayoutAndDisabledState) {
    const auto dropdown = IRPrefab::Widget::makeDropdown(
        IRMath::ivec2(10, 30),
        IRMath::ivec2(60, 20),
        {"A", "B"},
        1,
        20
    );
    auto &data = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdown);
    auto &widget = IREntity::getComponent<IRComponents::C_Widget>(dropdown);
    auto &hitbox = IREntity::getComponent<IRComponents::C_HitBox2DGui>(dropdown);
    auto &state = IREntity::getComponent<IRComponents::C_WidgetState>(dropdown);
    data.isOpen_ = true;
    data.typeahead_ = "B";
    widget.zOrder_ = 3 + IRComponents::kWidgetDropdownOpenZBias;
    hitbox.offset_ = IRMath::ivec2(0, -40);
    hitbox.size_ = IRMath::ivec2(60, 60);
    state.fireAction_ = true;
    state.focused_ = true;
    IRPrefab::Widget::setDisabled(dropdown, true);
    IRPrefab::Widget::setVisible(dropdown, false);
    EXPECT_FALSE(data.isOpen_);
    EXPECT_TRUE(data.typeahead_.empty());
    EXPECT_FALSE(state.fireAction_);
    EXPECT_FALSE(state.focused_);
    EXPECT_EQ(hitbox.offset_, IRMath::ivec2(0));
    EXPECT_EQ(hitbox.size_, widget.size_);
    EXPECT_EQ(widget.zOrder_, 3);
    IRPrefab::Widget::setVisible(dropdown, true);
    EXPECT_EQ(data.selectedIndex_, 1);
    EXPECT_TRUE(widget.disabled_);
    EXPECT_TRUE(IRPrefab::Widget::isVisible(dropdown));
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_GuiPosition>(dropdown).pos_,
        IRMath::ivec2(10, 30)
    );
}

TEST_F(WidgetVisibilityTest, HiddenGeometryIsAbsentFromLayoutAuditUntilShown) {
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(-20, -20), IRMath::ivec2(60, 20), "BUTTON");
    ASSERT_FALSE(IRPrefab::GuiAudit::report(IRMath::ivec2(400, 300)).empty());
    IRPrefab::Widget::setVisible(button, false);
    EXPECT_TRUE(IRPrefab::GuiAudit::report(IRMath::ivec2(400, 300)).empty());
    const auto layout = IRPrefab::GuiAudit::detail::Collector{IRMath::ivec2(400, 300)}.collect();
    EXPECT_TRUE(layout.elements_.empty());
    IRPrefab::Widget::setVisible(button, true);
    EXPECT_FALSE(IRPrefab::GuiAudit::report(IRMath::ivec2(400, 300)).empty());
}

TEST_F(WidgetVisibilityTest, HiddenLabelDoesNotQueueGlyphsAndShowsAgain) {
    const auto label = IRPrefab::Widget::makeLabel(IRMath::ivec2(10, 30), "LABEL");
    IRComponents::C_TriangleCanvasTextures canvas;
    canvas.size_ = IRMath::ivec2(400, 300);
    IRSystem::System<IRSystem::WIDGET_RENDER_LABEL> render;
    render.canvas_ = &canvas;
    const auto &widget = IREntity::getComponent<IRComponents::C_Widget>(label);
    const auto &data = IREntity::getComponent<IRComponents::C_WidgetLabel>(label);
    const auto &position = IREntity::getComponent<IRComponents::C_GuiPosition>(label);
    render.tick(widget, data, position);
    ASSERT_FALSE(render.textCmds_.empty());
    render.textCmds_.clear();
    IRPrefab::Widget::setVisible(label, false);
    render.tick(widget, data, position);
    EXPECT_TRUE(render.textCmds_.empty());
    IRPrefab::Widget::setVisible(label, true);
    render.tick(widget, data, position);
    EXPECT_FALSE(render.textCmds_.empty());
}

} // namespace
