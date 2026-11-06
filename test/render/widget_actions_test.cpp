#include <gtest/gtest.h>

#include <irreden/command/command_manager.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/input_manager.hpp>
#include <irreden/ir_command.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/render/actions_window.hpp>
#include <irreden/render/systems/system_widget_apply_slider.hpp>
#include <irreden/render/systems/system_widget_apply_text_input.hpp>
#include <irreden/render/systems/system_widget_input.hpp>
#include <irreden/render/widget_actions.hpp>
#include <irreden/render/widgets.hpp>

#include <list>
#include <map>
#include <optional>
#include <string>
#include <vector>

// Controls bound to actions, and the Actions window, driven headlessly through
// the production INPUT pipeline with synthetic input (no WIDGET_APPLY_LIST: it
// reads the mouse through the render manager, so rows are selected directly): the badge follows a
// rebind, focus and step actions find their field after it is rebuilt, a
// click and a chord run one operation, and the window's capture flow settles a
// conflict.

namespace {

IRMath::vec2 offscreenMouse() {
    return IRMath::vec2(-1000.0f);
}

constexpr IRMath::ivec2 kWindowOrigin{0, 0};

class WidgetActionsTest : public testing::Test {
  protected:
    void SetUp() override {
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(m_windowSentinel);
        m_inputManager.emplace();
        IRInput::beginSyntheticInput();
        m_commandManager.emplace();
        const IRSystem::SystemId input = IRSystem::createSystem<IRSystem::WIDGET_INPUT>();
        auto *params =
            m_systemManager.getSystemParams<IRSystem::System<IRSystem::WIDGET_INPUT>>(input);
        ASSERT_NE(params, nullptr);
        params->mousePosition_ = &offscreenMouse;
        m_systemManager.registerPipeline(
            IRTime::Events::INPUT,
            {
                input,
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_SLIDER>(),
                IRSystem::createSystem<IRSystem::WIDGET_APPLY_TEXT_INPUT>(),
            }
        );
        // The window's systems run as their own stage, between WIDGET_INPUT and
        // command dispatch exactly where they sit in INPUT, so a test can latch
        // a button click after WIDGET_INPUT has cleared last frame's.
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            IRPrefab::ActionsWindow::inputSystems({kWindowOrigin, 900})
        );
    }

    void TearDown() override {
        m_commandManager.reset();
        m_inputManager.reset();
        IRWindow::g_irglfwWindow = nullptr;
    }

    void frame() {
        m_inputManager->tick();
        m_inputManager->advanceInputState(IRTime::Events::INPUT);
        m_systemManager.executePipeline(IRTime::Events::INPUT);
        for (const IREntity::EntityId widget : m_clicks) {
            IREntity::getComponent<IRComponents::C_WidgetState>(widget).fireAction_ = true;
        }
        m_clicks.clear();
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        m_inputManager->advanceInputState(IRTime::Events::UPDATE);
        m_commandManager->executeUserKeyboardCommandsAll();
        m_entityManager.destroyMarkedEntities();
    }

    void tap(IRInput::KeyMouseButtons key) {
        IRInput::injectButton(key, IRInput::PRESSED);
        frame();
        IRInput::injectButton(key, IRInput::RELEASED);
        frame();
    }

    void registerCounter(const std::string &id, std::vector<IRCommand::KeyChord> keys) {
        IRCommand::ActionSpec spec;
        spec.id_ = id;
        spec.label_ = id;
        spec.section_ = "TEST";
        spec.defaultChords_ = std::move(keys);
        spec.invoke_ = [this, id]() { ++m_runs[id]; };
        ASSERT_TRUE(IRCommand::registerAction(std::move(spec)));
    }

    // A click as WIDGET_INPUT would latch it, delivered on the next frame.
    void click(IREntity::EntityId widget) {
        m_clicks.push_back(widget);
    }

    const IRSystem::System<IRSystem::ACTIONS_WINDOW> &window() {
        return *IRPrefab::ActionsWindow::systemOrNull();
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    std::optional<IRInput::InputManager> m_inputManager;
    std::optional<IRCommand::CommandManager> m_commandManager;
    std::map<std::string, int> m_runs;
    std::vector<IREntity::EntityId> m_clicks;
    alignas(std::max_align_t) char m_windowSentinel[1];
};

constexpr IRCommand::KeyChord kK{IRInput::kKeyButtonK, IRInput::kModifierNone};
constexpr IRCommand::KeyChord kJ{IRInput::kKeyButtonJ, IRInput::kModifierNone};

TEST_F(WidgetActionsTest, BadgeFollowsTheResolvedChord) {
    registerCounter("test.tool", {kK});
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(400, 0), IRMath::ivec2(30, 12), "TOOL");
    IRPrefab::Widget::bindAction(button, "test.tool");
    const auto hint = [&]() {
        return IREntity::getComponent<IRComponents::C_WidgetHotkey>(button).hint_;
    };
    EXPECT_EQ(hint(), "K");
    ASSERT_TRUE(IRCommand::actions().replaceChord("test.tool", kK, kJ).ok());
    frame();
    EXPECT_EQ(hint(), "J");
    ASSERT_TRUE(IRCommand::actions().clearChords("test.tool").ok());
    frame();
    EXPECT_EQ(hint(), "");

    IREntity::getComponent<IRComponents::C_WidgetState>(button).fireAction_ = true;
    EXPECT_TRUE(IRPrefab::Widget::invokeIfClicked(button));
    EXPECT_EQ(m_runs["test.tool"], 1);
}

TEST_F(WidgetActionsTest, BoundWidgetKeyFiresOnceThroughTheAction) {
    registerCounter("test.tool", {kK});
    const auto button =
        IRPrefab::Widget::makeButton(IRMath::ivec2(400, 0), IRMath::ivec2(30, 12), "TOOL");
    IRPrefab::Widget::bindAction(button, "test.tool");
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(m_runs["test.tool"], 1) << "the keyless widget hotkey must not fire a second time";
    EXPECT_FALSE(IRPrefab::Widget::wasClicked(button));
}

TEST_F(WidgetActionsTest, FocusAndStepFindARebuiltField) {
    IRCommand::ActionSpec focus;
    focus.id_ = "test.width.focus";
    ASSERT_TRUE(IRPrefab::Widget::registerFocusAction(focus));
    IRCommand::ActionSpec step;
    step.id_ = "test.width.increase";
    ASSERT_TRUE(IRPrefab::Widget::registerStepAction(step, "test.width.focus", 1.0f));

    auto slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(400, 40),
        IRMath::ivec2(60, 12),
        "W",
        0.0f,
        10.0f,
        4.0f
    );
    IRPrefab::Widget::bindAction(slider, "test.width.focus");
    IREntity::destroyEntity(slider);
    m_entityManager.destroyMarkedEntities();
    slider = IRPrefab::Widget::makeSlider(
        IRMath::ivec2(400, 60),
        IRMath::ivec2(60, 12),
        "W",
        0.0f,
        10.0f,
        9.5f
    );
    IRPrefab::Widget::bindAction(slider, "test.width.focus");

    EXPECT_TRUE(IRCommand::invokeAction("test.width.increase"));
    EXPECT_FLOAT_EQ(IRPrefab::Widget::sliderValue(slider), 10.0f) << "clamped to the range";
    EXPECT_TRUE(IRCommand::invokeAction("test.width.focus"));
    frame();
    EXPECT_EQ(IRPrefab::Widget::focusedWidget(), slider);
}

TEST_F(WidgetActionsTest, WindowSearchesRunsAndSettlesAConflict) {
    registerCounter("test.paint", {kK});
    registerCounter("test.erase", {});
    IRPrefab::ActionsWindow::registerToggleAction();
    EXPECT_TRUE(IRCommand::invokeAction(IRPrefab::ActionsWindow::kToggleActionId));
    frame();
    ASSERT_TRUE(window().built_);
    EXPECT_EQ(IRPrefab::ActionsWindow::listedActionIds().size(), 3u);

    IRPrefab::Widget::setTextInputValue(window().search_, "erase");
    frame();
    ASSERT_EQ(IRPrefab::ActionsWindow::listedActionIds(), std::vector<std::string>{"test.erase"});
    IRPrefab::Widget::setListSelectedIndex(window().list_, 0);
    frame();
    EXPECT_EQ(IRPrefab::ActionsWindow::selectedActionId(), "test.erase");

    click(window().button(IRPrefab::ActionsWindow::Button::RUN));
    frame();
    EXPECT_EQ(m_runs["test.erase"], 1) << "an unbound action runs from the list";

    click(window().button(IRPrefab::ActionsWindow::Button::ASSIGN));
    frame();
    ASSERT_EQ(window().mode_, IRSystem::System<IRSystem::ACTIONS_WINDOW>::Mode::CAPTURE);
    frame();
    EXPECT_TRUE(m_commandManager->isKeyboardCaptured()) << "capture keeps the chord's owner quiet";
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(m_runs["test.paint"], 0);
    ASSERT_EQ(window().mode_, IRSystem::System<IRSystem::ACTIONS_WINDOW>::Mode::CONFLICT);
    EXPECT_NE(
        IRPrefab::ActionsWindow::statusText().find("K IS USED BY test.paint"),
        std::string::npos
    );

    click(window().replace_);
    frame();
    EXPECT_EQ(IRCommand::actions().findAction("test.erase")->chords_, std::vector{kK});
    EXPECT_TRUE(IRCommand::actions().findAction("test.paint")->chords_.empty());

    click(window().button(IRPrefab::ActionsWindow::Button::CLOSE));
    frame();
    EXPECT_FALSE(IRPrefab::ActionsWindow::isOpen());
    frame();
    tap(IRInput::kKeyButtonK);
    EXPECT_EQ(m_runs["test.erase"], 2);
    EXPECT_EQ(m_runs["test.paint"], 0);
}

TEST_F(WidgetActionsTest, CaptureEscapeCancelsAndRemoveAndResetEditKeys) {
    registerCounter("test.paint", {kK});
    IRPrefab::ActionsWindow::setOpen(true);
    frame();
    IRPrefab::Widget::setListSelectedIndex(window().list_, 0);
    frame();
    ASSERT_EQ(IRPrefab::ActionsWindow::selectedActionId(), "test.paint");

    click(window().button(IRPrefab::ActionsWindow::Button::ADD));
    frame();
    frame();
    tap(IRInput::kKeyButtonEscape);
    EXPECT_EQ(window().mode_, IRSystem::System<IRSystem::ACTIONS_WINDOW>::Mode::BROWSE);
    EXPECT_EQ(IRCommand::actions().findAction("test.paint")->chords_, std::vector{kK});

    click(window().button(IRPrefab::ActionsWindow::Button::ADD));
    frame();
    frame();
    tap(IRInput::kKeyButtonJ);
    EXPECT_EQ(IRCommand::actions().findAction("test.paint")->chords_, (std::vector{kK, kJ}));

    click(window().button(IRPrefab::ActionsWindow::Button::REMOVE));
    frame();
    EXPECT_EQ(IRCommand::actions().findAction("test.paint")->chords_, std::vector{kK});
    ASSERT_TRUE(IRCommand::actions().clearChords("test.paint").ok());
    click(window().button(IRPrefab::ActionsWindow::Button::RESET));
    frame();
    EXPECT_EQ(IRCommand::actions().findAction("test.paint")->chords_, std::vector{kK});
}

} // namespace
