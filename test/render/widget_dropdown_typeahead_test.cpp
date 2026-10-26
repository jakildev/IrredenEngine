#include <gtest/gtest.h>

#include <irreden/command/command_manager.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/systems/system_widget_apply_dropdown.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/widgets.hpp>
#include <irreden/script/lua_script.hpp>

#include "widget_input_test_utils.hpp"

#include <sol/sol.hpp>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace {

IRMath::vec2 g_typeaheadMousePosition = IRMath::vec2(-1000.0f);

IRMath::vec2 typeaheadMousePosition() {
    return g_typeaheadMousePosition;
}

IRMath::ivec2 typeaheadCanvas() {
    return IRMath::ivec2(640, 720);
}

class WidgetDropdownTypeahead : public testing::Test {
  protected:
    WidgetDropdownTypeahead() {
        lua_.bindLuaDrivenEcs();
        lua_.bindLuaCommands();
    }

    void SetUp() override {
        g_typeaheadMousePosition = IRMath::vec2(-1000.0f);
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(windowSentinel_);
        inputManager_.emplace();
        IRInput::beginSyntheticInput();
        commandManager_.emplace();
        const IRSystem::SystemId input = IRSystem::createSystem<IRSystem::WIDGET_INPUT>();
        auto *params =
            systemManager_.getSystemParams<IRSystem::System<IRSystem::WIDGET_INPUT>>(input);
        ASSERT_NE(params, nullptr);
        params->mousePosition_ = &typeaheadMousePosition;
        params->guiCanvasSize_ = &typeaheadCanvas;
        systemManager_.registerPipeline(
            IRTime::Events::INPUT,
            {input, IRSystem::createSystem<IRSystem::WIDGET_APPLY_DROPDOWN>()}
        );
        auto result = lua_.lua().safe_script(
            R"lua(
            local names = {
                "GAIN", "COLOR", "CUTOFF", "FLOW", "LOW PASS", "RESONANCE",
                "ATTACK", "DECAY", "SUSTAIN", "RELEASE", "DRIVE", "MIX",
                "PAN", "WIDTH", "RATE", "DEPTH", "PHASE", "FEEDBACK",
                "TONE", "LEVEL", "PITCH", "FINE", "COARSE", "GLIDE",
                "SYNC", "MODE", "SHAPE", "SLOPE", "RANGE", "OFFSET",
                "SPEED", "AMOUNT", "THRESHOLD", "RATIO", "KNEE", "MAKEUP",
                "INPUT", "OUTPUT", "BYPASS", "LINK"
            }
            dd = IRGui.makeDropdown(20, 690, 180, 20, names, 1, 20, IRText.Size.SMALL)
        )lua",
            sol::script_pass_on_error
        );
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
        dropdownId_ = lua_.lua()["dd"].get<IREntity::EntityId>();
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

    void tap(IRInput::KeyMouseButtons key) {
        press(key);
        release(key);
    }

    IRScript::LuaScript lua_;
    IREntity::EntityManager entityManager_;
    IRSystem::SystemManager systemManager_;
    std::optional<IRInput::InputManager> inputManager_;
    std::optional<IRCommand::CommandManager> commandManager_;
    IREntity::EntityId dropdownId_ = IREntity::kNullEntity;
    alignas(std::max_align_t) char windowSentinel_[1];
};

TEST_F(WidgetDropdownTypeahead, NarrowsHighlightsEditsAndClears) {
    IRPrefab::Widget::focus(dropdownId_);
    frame();
    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    ASSERT_GT(dropdown.stripLayout(690, 20, 720).rows_, 0);
    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonO);

    ASSERT_TRUE(dropdown.isOpen_);
    ASSERT_EQ(dropdown.typeahead_, "lo");
    const auto layout = dropdown.stripLayout(690, 20, 720);
    ASSERT_GT(layout.rows_, 0);
    EXPECT_LT(layout.offsetY_, 0);
    EXPECT_EQ(layout.rows_, dropdown.shownItemCount());

    std::vector<int> shown;
    for (int row = 0; row < layout.rows_; ++row) {
        shown.push_back(dropdown.shownItemAt(layout, row));
    }
    EXPECT_EQ(shown, (std::vector<int>{1, 3, 4, 27}));
    EXPECT_EQ(IRPrefab::WidgetText::dropdownHeaderLabel(dropdown), "lo");
    EXPECT_EQ(dropdown.matchSpan(dropdown.items_[1]).begin_, 2);
    EXPECT_EQ(dropdown.matchSpan(dropdown.items_[1]).length_, 2);

    std::vector<IRRender::GlyphDrawCommand> commands;
    const auto &theme = IRPrefab::Widget::defaultTheme();
    IRPrefab::WidgetText::queueDropdownStripRow(
        commands,
        dropdown,
        IRMath::ivec2(20, 690),
        layout,
        theme,
        shown.front(),
        typeaheadCanvas(),
        theme.textIdle_
    );
    auto unfiltered = dropdown;
    unfiltered.typeahead_.clear();
    unfiltered.scrollOffset_ = shown.front();
    std::vector<IRRender::GlyphDrawCommand> baseline;
    IRPrefab::WidgetText::queueDropdownStripRow(
        baseline,
        unfiltered,
        IRMath::ivec2(20, 690),
        layout,
        theme,
        shown.front(),
        typeaheadCanvas(),
        theme.textIdle_
    );
    ASSERT_EQ(commands.size(), dropdown.items_[1].size());
    ASSERT_EQ(commands.size(), baseline.size());
    for (std::size_t i = 0; i < commands.size(); ++i) {
        const auto expected = i >= 2 && i < 4 ? theme.matchText_ : theme.textIdle_;
        EXPECT_EQ(commands[i].colorPacked, expected.toPackedRGBA());
        EXPECT_EQ(commands[i].positionPacked, baseline[i].positionPacked);
        EXPECT_EQ(commands[i].firstFontRow, baseline[i].firstFontRow);
        EXPECT_EQ(commands[i].distance, baseline[i].distance);
        EXPECT_EQ(commands[i].styleFlags, baseline[i].styleFlags);
    }

    tap(IRInput::kKeyButtonBackspace);
    EXPECT_EQ(dropdown.typeahead_, "l");
    EXPECT_GT(dropdown.shownItemCount(), static_cast<int>(shown.size()));
    tap(IRInput::kKeyButtonEscape);
    EXPECT_TRUE(dropdown.typeahead_.empty());
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_EQ(dropdown.shownItemCount(), 40);
    tap(IRInput::kKeyButtonEscape);
    EXPECT_FALSE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());
    tap(IRInput::kKeyButtonSpace);
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());
}

TEST_F(WidgetDropdownTypeahead, ArrowAndEnterPickShownRows) {
    IRPrefab::Widget::focus(dropdownId_);
    frame();
    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonO);
    auto &state = IREntity::getComponent<IRComponents::C_WidgetState>(dropdownId_);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 1);
    tap(IRInput::kKeyButtonUp);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 1) << "Up clamps at the first match";
    tap(IRInput::kKeyButtonDown);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 3);
    tap(IRInput::kKeyButtonDown);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 4);
    tap(IRInput::kKeyButtonDown);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 27);
    tap(IRInput::kKeyButtonDown);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 27) << "Down clamps at the last match";
    tap(IRInput::kKeyButtonUp);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 4) << "Up returns from the last match";
    tap(IRInput::kKeyButtonEnter);

    const auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    EXPECT_EQ(dropdown.selectedIndex_, 4);
    EXPECT_FALSE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());
    auto selected = lua_.lua().safe_script("return IRGui.dropdownSelected(dd)");
    ASSERT_TRUE(selected.valid());
    EXPECT_EQ(selected.get<int>(), 5);

    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonO);
    tap(IRInput::kKeyButtonEnter);
    EXPECT_EQ(dropdown.selectedIndex_, 1) << "Enter defaults to the first match";
    EXPECT_FALSE(dropdown.isOpen_);
}

TEST_F(WidgetDropdownTypeahead, RestingPointerYieldsToArrowsAndClickStillPicks) {
    IRPrefab::Widget::focus(dropdownId_);
    frame();
    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonO);

    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    auto &state = IREntity::getComponent<IRComponents::C_WidgetState>(dropdownId_);
    auto &hitbox = IREntity::getComponent<IRComponents::C_HitBox2DGui>(dropdownId_);
    const auto layout = dropdown.stripLayout(690, 20, 720);
    g_typeaheadMousePosition = IRMath::vec2(21.0f, 690.0f + dropdown.itemCenterOffsetY(layout, 3));
    hitbox.hovered_ = true;
    frame();
    ASSERT_EQ(static_cast<int>(state.dragValue_), 3);

    tap(IRInput::kKeyButtonDown);
    EXPECT_EQ(static_cast<int>(state.dragValue_), 4);
    tap(IRInput::kKeyButtonEnter);
    EXPECT_EQ(dropdown.selectedIndex_, 4);
    EXPECT_FALSE(dropdown.isOpen_);

    g_typeaheadMousePosition = IRMath::vec2(-1000.0f);
    hitbox.hovered_ = false;
    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonO);
    const auto reopenedLayout = dropdown.stripLayout(690, 20, 720);
    g_typeaheadMousePosition =
        IRMath::vec2(21.0f, 690.0f + dropdown.itemCenterOffsetY(reopenedLayout, 27));
    hitbox.hovered_ = true;
    frame();
    ASSERT_EQ(static_cast<int>(state.dragValue_), 27);
    tap(IRInput::kMouseButtonLeft);
    EXPECT_EQ(dropdown.selectedIndex_, 27);
    EXPECT_FALSE(dropdown.isOpen_);
}

TEST_F(WidgetDropdownTypeahead, NoMatchStaysOpenAndHeaderWidthCapsTheQuery) {
    IRPrefab::Widget::focus(dropdownId_);
    frame();
    tap(IRInput::kKeyButtonJ);

    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    ASSERT_TRUE(dropdown.isOpen_);
    ASSERT_EQ(dropdown.shownItemCount(), 0);
    const int selectedBefore = dropdown.selectedIndex_;
    tap(IRInput::kKeyButtonEnter);
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_EQ(dropdown.selectedIndex_, selectedBefore);

    tap(IRInput::kKeyButtonEscape);
    auto &widget = IREntity::getComponent<IRComponents::C_Widget>(dropdownId_);
    widget.size_.x = 32;
    int charactersThatFit = 0;
    while (IRSystem::System<IRSystem::WIDGET_APPLY_DROPDOWN>::queryFits(
        widget,
        dropdown,
        static_cast<std::size_t>(charactersThatFit + 1)
    )) {
        ++charactersThatFit;
    }
    ASSERT_GT(charactersThatFit, 0);
    for (int i = 0; i < charactersThatFit + 2; ++i) {
        tap(IRInput::kKeyButtonA);
    }
    EXPECT_EQ(dropdown.typeahead_.size(), static_cast<std::size_t>(charactersThatFit));
    EXPECT_TRUE(commandManager_->isButtonCaptured(IRInput::kKeyButtonA));
}

TEST_F(WidgetDropdownTypeahead, BareAndModifiedCommandsHaveOneConsumer) {
    int bareM = 0;
    int controlZ = 0;
    int escapeCommand = 0;
    commandManager_->createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonM, [&] {
        ++bareM;
    });
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&] { ++controlZ; },
        IRInput::kModifierControl
    );
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonEscape,
        [&] { ++escapeCommand; }
    );

    IRPrefab::Widget::focus(dropdownId_);
    frame();
    tap(IRInput::kKeyButtonL);
    tap(IRInput::kKeyButtonM);
    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    EXPECT_EQ(dropdown.typeahead_, "lm");
    EXPECT_EQ(bareM, 0);
    EXPECT_TRUE(commandManager_->isButtonCaptured(IRInput::kKeyButtonM));

    press(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonZ);
    EXPECT_EQ(controlZ, 1);
    EXPECT_EQ(dropdown.typeahead_, "lm");
    EXPECT_FALSE(commandManager_->isButtonCaptured(IRInput::kKeyButtonZ));
    release(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 0);
    EXPECT_EQ(dropdown.typeahead_, "lmm");

    tap(IRInput::kKeyButtonEscape);
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());
    EXPECT_EQ(escapeCommand, 0);
    tap(IRInput::kKeyButtonEscape);
    EXPECT_FALSE(dropdown.isOpen_);
    EXPECT_EQ(escapeCommand, 0);

    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 1);
    EXPECT_FALSE(dropdown.isOpen_);
    tap(IRInput::kKeyButtonL);
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_EQ(dropdown.typeahead_, "l");
    tap(IRInput::kKeyButtonEscape);
    tap(IRInput::kKeyButtonEscape);
    tap(IRInput::kKeyButtonEscape);
    EXPECT_EQ(escapeCommand, 1);
}

TEST_F(WidgetDropdownTypeahead, PointerFocusAndHotkeysKeepOneConsumer) {
    int bareM = 0;
    commandManager_->createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonM, [&] {
        ++bareM;
    });
    ASSERT_TRUE(
        IRPrefab::Widget::setHotkey(
            dropdownId_,
            IRInput::kKeyButtonP,
            IRInput::kModifierAlt,
            "PARAMETER"
        )
    );
    const auto other =
        IRPrefab::Widget::makeButton(IRMath::ivec2(240, 690), IRMath::ivec2(80, 20), "OTHER");
    ASSERT_TRUE(IRPrefab::Widget::setHotkey(other, IRInput::kKeyButtonM));

    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    auto &hitbox = IREntity::getComponent<IRComponents::C_HitBox2DGui>(dropdownId_);
    g_typeaheadMousePosition = IRMath::vec2(21.0f, 695.0f);
    hitbox.hovered_ = true;
    press(IRInput::kMouseButtonLeft);
    ASSERT_EQ(IRPrefab::Widget::focusedWidget(), dropdownId_);
    ASSERT_FALSE(IRPrefab::Widget::focusVisible());
    ASSERT_FALSE(dropdown.isOpen_);
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 1);
    EXPECT_TRUE(dropdown.typeahead_.empty());
    EXPECT_FALSE(dropdown.isOpen_);
    release(IRInput::kMouseButtonLeft);
    tap(IRInput::kKeyButtonEscape);

    press(IRInput::kKeyButtonLeftAlt);
    tap(IRInput::kKeyButtonP);
    release(IRInput::kKeyButtonLeftAlt);
    ASSERT_TRUE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());

    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 1);
    EXPECT_EQ(dropdown.typeahead_, "m");
    EXPECT_FALSE(IRPrefab::Widget::wasClicked(other));
}

TEST_F(WidgetDropdownTypeahead, ModifierTransitionsStillGiveEachPressOneConsumer) {
    int bareM = 0;
    int controlZ = 0;
    int altZ = 0;
    commandManager_->createCommand(IRInput::KEY_MOUSE, IRInput::PRESSED, IRInput::kKeyButtonM, [&] {
        ++bareM;
    });
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&] { ++controlZ; },
        IRInput::kModifierControl
    );
    commandManager_->createCommand(
        IRInput::KEY_MOUSE,
        IRInput::PRESSED,
        IRInput::kKeyButtonZ,
        [&] { ++altZ; },
        IRInput::kModifierAlt
    );

    IRPrefab::Widget::focus(dropdownId_);
    frame();
    tap(IRInput::kKeyButtonL);
    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    ASSERT_EQ(dropdown.typeahead_, "l");

    IRInput::injectButton(IRInput::kKeyButtonLeftControl, IRInput::PRESSED);
    IRInput::injectButton(IRInput::kKeyButtonZ, IRInput::PRESSED);
    frame();
    EXPECT_EQ(controlZ, 1);
    EXPECT_EQ(dropdown.typeahead_, "l");

    IRInput::injectButton(IRInput::kKeyButtonZ, IRInput::RELEASED);
    IRInput::injectButton(IRInput::kKeyButtonLeftControl, IRInput::RELEASED);
    frame();
    press(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonZ);
    EXPECT_EQ(controlZ, 2);
    EXPECT_EQ(dropdown.typeahead_, "l");

    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 1);
    EXPECT_EQ(dropdown.typeahead_, "l");
    release(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 1);
    EXPECT_EQ(dropdown.typeahead_, "lm");

    press(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 2);
    EXPECT_EQ(dropdown.typeahead_, "lm");
    release(IRInput::kKeyButtonLeftControl);
    frame();
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 2);
    EXPECT_EQ(dropdown.typeahead_, "lmm");

    IRInput::injectButton(IRInput::kKeyButtonLeftAlt, IRInput::PRESSED);
    IRInput::injectButton(IRInput::kKeyButtonZ, IRInput::PRESSED);
    frame();
    EXPECT_EQ(altZ, 1);
    EXPECT_EQ(dropdown.typeahead_, "lmm");

    IRInput::injectButton(IRInput::kKeyButtonZ, IRInput::RELEASED);
    IRInput::injectButton(IRInput::kKeyButtonLeftAlt, IRInput::RELEASED);
    frame();
    press(IRInput::kKeyButtonLeftAlt);
    tap(IRInput::kKeyButtonZ);
    EXPECT_EQ(altZ, 2);
    EXPECT_EQ(dropdown.typeahead_, "lmm");
    release(IRInput::kKeyButtonLeftAlt);

    press(IRInput::kKeyButtonLeftControl);
    tap(IRInput::kKeyButtonM);
    EXPECT_EQ(bareM, 3);
    EXPECT_EQ(dropdown.typeahead_, "lmm");
    IRInput::injectButton(IRInput::kKeyButtonLeftControl, IRInput::RELEASED);
    IRInput::injectButton(IRInput::kKeyButtonM, IRInput::PRESSED);
    frame();
    EXPECT_EQ(bareM, 3);
    EXPECT_EQ(dropdown.typeahead_, "lmmm");
    EXPECT_TRUE(commandManager_->isButtonCaptured(IRInput::kKeyButtonM));
    release(IRInput::kKeyButtonM);
}

TEST_F(WidgetDropdownTypeahead, LuaCloseClearsBeforeImmediateReopen) {
    auto &dropdown = IREntity::getComponent<IRComponents::C_WidgetDropdown>(dropdownId_);
    dropdown.isOpen_ = true;
    dropdown.typeahead_ = "lo";
    auto result =
        lua_.lua().safe_script("IRGui.setDropdownOpen(dd, false); IRGui.setDropdownOpen(dd, true)");
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(dropdown.isOpen_);
    EXPECT_TRUE(dropdown.typeahead_.empty());
}

} // namespace
