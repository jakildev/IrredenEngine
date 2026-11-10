#include <gtest/gtest.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/input/components/component_mouse_scroll_lua.hpp>
#include <irreden/script/lua_input_bindings.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/time/ir_time_types.hpp>
#include <irreden/video/auto_screenshot.hpp>

#include <sol/sol.hpp>

#include <optional>

namespace {

// ---- Tier 1: binding-presence tests (no display required) ------------------
//
// Owns LuaScript + EntityManager + CommandManager — the same slice as
// LuaCommandTest. bindLuaInput() extends the IRInput table created by
// bindLuaCommands(); both calls are made here so the idempotency guards
// exercise the cross-call path.
//
// Destruction order (reverse declaration): m_command_manager, m_entity_manager,
// m_lua — keeps sol::protected_function refs alive until after CommandManager
// is gone (same lifetime contract as lua_command_test.cpp).
class LuaInputBindingsTest : public testing::Test {
  protected:
    LuaInputBindingsTest()
        : m_lua{}
        , m_entity_manager{}
        , m_command_manager{} {
        m_lua.bindLuaCommands();
        m_lua.bindLuaInput();
    }

    bool isInteger(const char *expr) {
        auto result = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'number'",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    bool isFunction(const char *expr) {
        auto result = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'function'",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    lua_Integer evalInt(const char *expr) {
        auto result =
            m_lua.lua().safe_script(std::string("return ") + expr, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << expr;
        return result.get<lua_Integer>();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRCommand::CommandManager m_command_manager;
};

// ---- KeyMouseButtons enum table -------------------------------------------

TEST_F(LuaInputBindingsTest, KeyMouseButtonsTablePresent) {
    EXPECT_TRUE(isInteger("IRInput.KeyMouseButtons.kKeyButtonSpace"));
    EXPECT_TRUE(isInteger("IRInput.KeyMouseButtons.kKeyButtonEscape"));
    EXPECT_TRUE(isInteger("IRInput.KeyMouseButtons.kMouseButtonLeft"));
    EXPECT_TRUE(isInteger("IRInput.KeyMouseButtons.kNullButton"));
}

TEST_F(LuaInputBindingsTest, KeyMouseButtonsValuesMatchCpp) {
    EXPECT_EQ(
        evalInt("IRInput.KeyMouseButtons.kKeyButtonSpace"),
        static_cast<lua_Integer>(IRInput::kKeyButtonSpace)
    );
    EXPECT_EQ(
        evalInt("IRInput.KeyMouseButtons.kMouseButtonLeft"),
        static_cast<lua_Integer>(IRInput::kMouseButtonLeft)
    );
    EXPECT_EQ(evalInt("IRInput.KeyMouseButtons.kNullButton"), 0);
}

// ---- ButtonStatuses enum table --------------------------------------------

TEST_F(LuaInputBindingsTest, ButtonStatusesTablePresent) {
    EXPECT_TRUE(isInteger("IRInput.ButtonStatuses.NOT_HELD"));
    EXPECT_TRUE(isInteger("IRInput.ButtonStatuses.PRESSED"));
    EXPECT_TRUE(isInteger("IRInput.ButtonStatuses.HELD"));
    EXPECT_TRUE(isInteger("IRInput.ButtonStatuses.RELEASED"));
    EXPECT_TRUE(isInteger("IRInput.ButtonStatuses.PRESSED_AND_RELEASED"));
}

TEST_F(LuaInputBindingsTest, ButtonStatusesValuesMatchCpp) {
    EXPECT_EQ(
        evalInt("IRInput.ButtonStatuses.NOT_HELD"),
        static_cast<lua_Integer>(IRInput::NOT_HELD)
    );
    EXPECT_EQ(
        evalInt("IRInput.ButtonStatuses.PRESSED"),
        static_cast<lua_Integer>(IRInput::PRESSED)
    );
}

// ButtonStatuses and ButtonStatus (singular) must agree on ordinals so
// both tables can be used interchangeably as trigger status arguments.
TEST_F(LuaInputBindingsTest, ButtonStatusesAgreesWithButtonStatus) {
    EXPECT_EQ(evalInt("IRInput.ButtonStatuses.PRESSED"), evalInt("IRInput.ButtonStatus.PRESSED"));
    EXPECT_EQ(evalInt("IRInput.ButtonStatuses.NOT_HELD"), evalInt("IRInput.ButtonStatus.NOT_HELD"));
}

// ---- Synthetic-input function presence ------------------------------------

TEST_F(LuaInputBindingsTest, SyntheticInputFunctionsBound) {
    EXPECT_TRUE(isFunction("IRInput.beginSyntheticInput"));
    EXPECT_TRUE(isFunction("IRInput.isSyntheticInputActive"));
    EXPECT_TRUE(isFunction("IRInput.injectButton"));
    EXPECT_TRUE(isFunction("IRInput.injectMouseMove"));
    EXPECT_TRUE(isFunction("IRInput.injectScroll"));
    EXPECT_TRUE(isFunction("IRInput.injectPointerGesture"));
    EXPECT_TRUE(isFunction("IRInput.pointerGestures"));
}

TEST_F(LuaInputBindingsTest, PointerGestureEnumsMatchCpp) {
    EXPECT_EQ(
        evalInt("IRInput.PointerGestureKind.MAGNIFY"),
        static_cast<lua_Integer>(IRInput::PointerGestureKind::MAGNIFY)
    );
    EXPECT_EQ(
        evalInt("IRInput.PointerGestureSource.UNKNOWN"),
        static_cast<lua_Integer>(IRInput::PointerGestureSource::UNKNOWN)
    );
    EXPECT_EQ(
        evalInt("IRInput.PointerGestureSource.PRECISE_SCROLL"),
        static_cast<lua_Integer>(IRInput::PointerGestureSource::PRECISE_SCROLL)
    );
    EXPECT_EQ(
        evalInt("IRInput.PointerGesturePhase.CANCELLED"),
        static_cast<lua_Integer>(IRInput::PointerGesturePhase::CANCELLED)
    );
}

TEST_F(LuaInputBindingsTest, PointerGestureRejectsInvalidMetadataBeforeInjection) {
    for (const char *fields :
         {"kind='MAGNIFY'",
          "kind=2",
          "source=0.5",
          "source=99",
          "phase=64",
          "momentumPhase=-1",
          "modifiers=16",
          "x=0/0",
          "y=math.huge",
          "magnification=math.huge",
          "windowX='12'",
          "windowY=math.huge",
          "directionInverted=1"}) {
        const auto result = m_lua.lua().safe_script(
            std::string("IRInput.injectPointerGesture({") + fields + "})",
            sol::script_pass_on_error
        );
        EXPECT_FALSE(result.valid()) << fields;
    }
}

// ---- Scroll query + C_MouseScroll binding presence ------------------------

TEST_F(LuaInputBindingsTest, ScrollQueryFunctionsBound) {
    EXPECT_TRUE(isFunction("IRInput.scrollDelta"));
    EXPECT_TRUE(isFunction("IRInput.scrollOffset"));
}

TEST_F(LuaInputBindingsTest, MouseScrollComponentBindsXAndYOffset) {
    static_assert(IRScript::kHasLuaBinding<IRComponents::C_MouseScroll>);
    IRScript::bindLuaType<IRComponents::C_MouseScroll>(m_lua);
    auto result = m_lua.lua().safe_script(
        R"(
        local scroll = C_MouseScroll.new(1.5, -2)
        return scroll.xoffset, scroll.yoffset
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const std::pair<double, double> offsets = result;
    EXPECT_DOUBLE_EQ(offsets.first, 1.5);
    EXPECT_DOUBLE_EQ(offsets.second, -2.0);
}

// ---- Idempotency: second bindLuaInput() call is a no-op ------------------

TEST_F(LuaInputBindingsTest, BindLuaInputIdempotent) {
    // A second call must not overwrite existing values or raise.
    m_lua.bindLuaInput();
    EXPECT_TRUE(isFunction("IRInput.beginSyntheticInput"));
    EXPECT_TRUE(isInteger("IRInput.KeyMouseButtons.kKeyButtonSpace"));
}

// ---- Tier 2: full inject→dispatch path (requires GLFW) --------------------
//
// Constructs a real InputManager so the synthetic-inject→tick→command-dispatch
// pipeline can run end-to-end. The SetUp() gates on glfwInit() and GTEST_SKIPs
// when no display is available (e.g. headless CI without a GPU).
//
// InputManager::initJoystickEntities() asserts g_irglfwWindow != nullptr.
// IRGLFWWindow::joystickPresent() wraps glfwJoystickPresent() with no 'this'
// member access, so pointing g_irglfwWindow at a sentinel address is safe after
// glfwInit(). No joystick is connected in a test environment, so the if-block
// inside initJoystickEntities() never executes and no member of the sentinel
// object is accessed.
//
// Destruction order (reverse of declaration): m_command_manager, m_input_manager,
// m_entity_manager, m_lua — sol::protected_function refs released while the
// sol::state is still alive.
class LuaInputInjectionTest : public testing::Test {
  protected:
    void SetUp() override {
        if (!glfwInit()) {
            GTEST_SKIP() << "glfwInit failed — headless host, injection path skipped";
        }
        IRWindow::g_irglfwWindow = reinterpret_cast<IRWindow::IRGLFWWindow *>(m_window_sentinel);
        m_entity_manager.emplace();
        m_input_manager.emplace();
        m_command_manager.emplace();
        m_lua.emplace();
        m_lua->bindLuaCommands();
        m_lua->bindLuaInput();
    }

    void TearDown() override {
        // Destroy in the order that preserves the lua-outlives-command-manager
        // lifetime contract: command manager first, lua state last.
        m_command_manager.reset();
        m_input_manager.reset();
        m_entity_manager.reset();
        m_lua.reset();
        IRWindow::g_irglfwWindow = nullptr;
        glfwTerminate();
    }

    // Declared in reverse-destruction order: lua last (outlives command manager).
    std::optional<IRScript::LuaScript> m_lua;
    std::optional<IREntity::EntityManager> m_entity_manager;
    std::optional<IRInput::InputManager> m_input_manager;
    std::optional<IRCommand::CommandManager> m_command_manager;

    // Sentinel storage whose address satisfies the g_irglfwWindow != nullptr
    // assert in IRWindow::getWindow(). No IRGLFWWindow members are accessed.
    alignas(std::max_align_t) char m_window_sentinel[1];
};

// Pump one synthetic frame: tick() drains injected events, advanceInputState()
// snapshots the INPUT-event state, executeUserKeyboardCommandsAll() fires
// any command whose trigger matches the snapshot.
void pumpInputFrame() {
    IRInput::g_inputManager->tick();
    IRInput::g_inputManager->advanceInputState(IRTime::Events::INPUT);
    IRCommand::getCommandManager().executeUserKeyboardCommandsAll();
}

// A Lua command bound to kKeyButtonSpace PRESSED fires when SPACE is injected.
TEST_F(LuaInputInjectionTest, InjectSpaceFiresCommand) {
    auto &lua = m_lua->lua();
    auto result = lua.safe_script(
        R"(
        flag = false
        IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.KeyMouseButtons.kKeyButtonSpace,
            function() flag = true end
        )
        IRInput.beginSyntheticInput()
        IRInput.injectButton(
            IRInput.KeyMouseButtons.kKeyButtonSpace,
            IRInput.ButtonStatuses.PRESSED
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();

    pumpInputFrame();

    const bool flag = lua["flag"];
    EXPECT_TRUE(flag) << "command body should have run after SPACE injection";
}

// With no command bound, injecting SPACE leaves all observable state unchanged.
TEST_F(LuaInputInjectionTest, InjectWithNoCommandBoundLeavesStateClear) {
    auto &lua = m_lua->lua();
    auto result = lua.safe_script(
        R"(
        flag = false
        IRInput.beginSyntheticInput()
        IRInput.injectButton(
            IRInput.KeyMouseButtons.kKeyButtonSpace,
            IRInput.ButtonStatuses.PRESSED
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();

    pumpInputFrame();

    const bool flag = lua["flag"];
    EXPECT_FALSE(flag) << "flag must stay false when no command is wired to SPACE";
}

// scrollDelta()/scrollOffset() hold the frame's summed wheel events for every
// pipeline event, not just INPUT, and reset on a frame with none.
TEST_F(LuaInputInjectionTest, ScrollQuerySumsFrameAcrossPipelineEvents) {
    IRInput::beginSyntheticInput();
    EXPECT_EQ(IRInput::scrollDelta(), 0);
    EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.0f, 0.0f));

    IRInput::injectScroll(0.0, 1.0);
    IRInput::injectScroll(0.0, 1.0);
    IRInput::injectScroll(0.0, -2.5);
    pumpInputFrame();
    for (const IRTime::Events event : {IRTime::Events::UPDATE, IRTime::Events::RENDER}) {
        IRInput::g_inputManager->advanceInputState(event);
        EXPECT_EQ(IRInput::scrollDelta(), 1);
        EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.0f, -0.5f));
    }

    pumpInputFrame();
    EXPECT_EQ(IRInput::scrollDelta(), 0);
    EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.0f, 0.0f));
}

TEST_F(LuaInputInjectionTest, LuaScrollQueryReadsInjectedEvents) {
    auto &lua = m_lua->lua();
    auto before = lua.safe_script(
        "return IRInput.scrollDelta(), IRInput.scrollOffset()",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(before.valid()) << before.get<sol::error>().what();
    EXPECT_EQ(before.get<int>(0), 0);
    EXPECT_DOUBLE_EQ(before.get<double>(1), 0.0);
    EXPECT_DOUBLE_EQ(before.get<double>(2), 0.0);

    auto result = lua.safe_script(
        R"(
        IRInput.beginSyntheticInput()
        IRInput.injectScroll(0.5, 1)
        IRInput.injectScroll(0, 1)
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    pumpInputFrame();

    auto after = lua.safe_script(
        "return IRInput.scrollDelta(), IRInput.scrollOffset()",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(after.valid()) << after.get<sol::error>().what();
    EXPECT_EQ(after.get<int>(0), 2);
    EXPECT_DOUBLE_EQ(after.get<double>(1), 0.5);
    EXPECT_DOUBLE_EQ(after.get<double>(2), 2.0);
}

#ifndef IR_RELEASE
TEST_F(LuaInputInjectionTest, PointerGestureRequiresSyntheticModeBeforeQueuing) {
    EXPECT_THROW(IRInput::injectPointerGesture({}), std::runtime_error);
    IRInput::beginSyntheticInput();
    pumpInputFrame();
    EXPECT_TRUE(IRInput::pointerGestures().empty());
}
#endif

TEST_F(LuaInputInjectionTest, PointerGesturesPreserveOrderMetadataAndZeroPhases) {
    IRInput::beginSyntheticInput();
    IRInput::PointerGestureEvent event;
    event.kind_ = IRInput::PointerGestureKind::MAGNIFY;
    event.phase_ = IRInput::PointerGesturePhase::BEGAN;
    event.windowPoint_ = {120.5, 80.25};
    IRInput::injectPointerGesture(event);
    event.kind_ = IRInput::PointerGestureKind::SCROLL;
    event.source_ = IRInput::PointerGestureSource::PRECISE_SCROLL;
    event.phase_ = IRInput::PointerGesturePhase::CHANGED;
    event.scroll_ = {0.75, -1.25};
    event.modifiers_ = IRInput::kModifierShift;
    event.directionInverted_ = true;
    IRInput::injectPointerGesture(event);
    event.kind_ = IRInput::PointerGestureKind::MAGNIFY;
    event.magnification_ = 0.125;
    event.scroll_ = {0.0, 0.0};
    IRInput::injectPointerGesture(event);
    event.magnification_ = 0.0;
    event.phase_ = IRInput::PointerGesturePhase::CANCELLED;
    IRInput::injectPointerGesture(event);
    event.kind_ = IRInput::PointerGestureKind::SCROLL;
    event.phase_ = IRInput::PointerGesturePhase::NONE;
    event.momentumPhase_ = IRInput::PointerGesturePhase::ENDED;
    IRInput::injectPointerGesture(event);
    pumpInputFrame();
    for (const auto stage : IRInput::kTrackedEvents) {
        IRInput::getInputManager().advanceInputState(stage);
        const auto events = IRInput::pointerGestures();
        ASSERT_EQ(events.size(), 5u);
        EXPECT_EQ(events[0].kind_, IRInput::PointerGestureKind::MAGNIFY);
        EXPECT_EQ(events[0].phase_, IRInput::PointerGesturePhase::BEGAN);
        EXPECT_DOUBLE_EQ(events[0].magnification_, 0.0);
        EXPECT_EQ(events[1].source_, IRInput::PointerGestureSource::PRECISE_SCROLL);
        EXPECT_EQ(events[1].scroll_, IRMath::dvec2(0.75, -1.25));
        EXPECT_EQ(events[1].windowPoint_, IRMath::dvec2(120.5, 80.25));
        EXPECT_EQ(events[1].modifiers_, IRInput::kModifierShift);
        EXPECT_TRUE(events[1].directionInverted_);
        EXPECT_DOUBLE_EQ(events[2].magnification_, 0.125);
        EXPECT_EQ(events[3].phase_, IRInput::PointerGesturePhase::CANCELLED);
        EXPECT_EQ(events[4].momentumPhase_, IRInput::PointerGesturePhase::ENDED);
        EXPECT_EQ(IRInput::scrollDelta(), -1);
        EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.75f, -1.25f));
    }
    int scrollEntities = 0;
    IREntity::forEachComponent<IRComponents::C_MouseScroll>(
        [&](IREntity::EntityId, IRComponents::C_MouseScroll &) { ++scrollEntities; }
    );
    EXPECT_EQ(scrollEntities, 1);
    pumpInputFrame();
    EXPECT_TRUE(IRInput::pointerGestures().empty());
    EXPECT_EQ(IRInput::scrollDelta(), 0);
    EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.0f));
}

TEST_F(LuaInputInjectionTest, LegacyScrollCapturesCursorAndModifiersAtInjection) {
    IRInput::beginSyntheticInput();
    IRInput::injectMouseMove({120, 80});
    IRInput::injectButton(IRInput::kKeyButtonLeftShift, IRInput::PRESSED);
    IRInput::injectButton(IRInput::kKeyButtonRightShift, IRInput::PRESSED);
    IRInput::injectButton(IRInput::kKeyButtonLeftShift, IRInput::RELEASED);
    IRInput::injectScroll(0.5, 1.0);
    IRInput::injectButton(IRInput::kKeyButtonRightShift, IRInput::RELEASED);
    IRInput::injectMouseMove({500, 400});
    IRInput::injectScroll(0.0, -1.0);
    pumpInputFrame();
    const auto events = IRInput::pointerGestures();
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].windowPoint_, IRMath::dvec2(120, 80));
    EXPECT_EQ(events[0].modifiers_, IRInput::kModifierShift);
    EXPECT_EQ(events[0].source_, IRInput::PointerGestureSource::UNKNOWN);
    EXPECT_EQ(events[1].windowPoint_, IRMath::dvec2(500, 400));
    EXPECT_EQ(events[1].modifiers_, IRInput::kModifierNone);
    EXPECT_EQ(IRInput::getMousePosition(), IRMath::vec2(500, 400));
    EXPECT_EQ(IRInput::scrollDelta(), 0);
    EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.5f, 0.0f));
}

TEST_F(LuaInputInjectionTest, GuiReplayDispatchesGesturePayloadAndLegacyEventsInOrder) {
    IRInput::beginSyntheticInput();
    IRVideo::GuiInputEvent event;
    event.screenPx_ = {80, 60};
    IRVideo::injectGuiInputEvent(event);
    event.type_ = IRVideo::GuiInputEvent::Type::PRESS;
    event.button_ = IRInput::kKeyButtonLeftShift;
    IRVideo::injectGuiInputEvent(event);
    event.type_ = IRVideo::GuiInputEvent::Type::SCROLL;
    event.scroll_ = {0.5f, 1.0f};
    IRVideo::injectGuiInputEvent(event);
    event.type_ = IRVideo::GuiInputEvent::Type::POINTER_GESTURE;
    event.pointerGesture_.kind_ = IRInput::PointerGestureKind::MAGNIFY;
    event.pointerGesture_.phase_ = IRInput::PointerGesturePhase::BEGAN;
    event.pointerGesture_.windowPoint_ = {95.25, 110.5};
    IRVideo::injectGuiInputEvent(event);
    event.pointerGesture_.phase_ = IRInput::PointerGesturePhase::CHANGED;
    event.pointerGesture_.magnification_ = -0.125;
    IRVideo::injectGuiInputEvent(event);
    event.pointerGesture_.phase_ = IRInput::PointerGesturePhase::ENDED;
    event.pointerGesture_.magnification_ = 0.0;
    IRVideo::injectGuiInputEvent(event);
    event.type_ = IRVideo::GuiInputEvent::Type::RELEASE;
    IRVideo::injectGuiInputEvent(event);
    pumpInputFrame();
    const auto gestures = IRInput::pointerGestures();
    ASSERT_EQ(gestures.size(), 4u);
    EXPECT_EQ(gestures[0].kind_, IRInput::PointerGestureKind::SCROLL);
    EXPECT_EQ(gestures[0].modifiers_, IRInput::kModifierShift);
    EXPECT_EQ(gestures[1].phase_, IRInput::PointerGesturePhase::BEGAN);
    EXPECT_EQ(gestures[1].windowPoint_, IRMath::dvec2(95.25, 110.5));
    EXPECT_DOUBLE_EQ(gestures[2].magnification_, -0.125);
    EXPECT_EQ(gestures[3].phase_, IRInput::PointerGesturePhase::ENDED);
    EXPECT_DOUBLE_EQ(gestures[3].magnification_, 0.0);
    EXPECT_EQ(IRInput::getMousePosition(), IRMath::vec2(80, 60));
    EXPECT_EQ(IRInput::heldKeyModifiers(), IRInput::kModifierNone);
    EXPECT_EQ(IRInput::scrollDelta(), 1);
    EXPECT_EQ(IRInput::scrollOffset(), IRMath::vec2(0.5f, 1.0f));
}

TEST_F(LuaInputInjectionTest, LuaPointerGestureRoundTripAndLegacyIsolation) {
    auto result = m_lua->lua().safe_script(
        R"(
        IRInput.beginSyntheticInput()
        IRInput.injectPointerGesture({
            kind=IRInput.PointerGestureKind.MAGNIFY,
            phase=IRInput.PointerGesturePhase.BEGAN,
            magnification=0.25, windowX=45.5, windowY=90.25,
            modifiers=IRInput.Modifier.SHIFT
        })
        IRInput.injectPointerGesture({
            kind=IRInput.PointerGestureKind.SCROLL,
            source=IRInput.PointerGestureSource.PRECISE_SCROLL,
            phase=IRInput.PointerGesturePhase.CHANGED,
            momentumPhase=IRInput.PointerGesturePhase.BEGAN,
            x=0.5, y=-0.75, directionInverted=true
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    pumpInputFrame();
    result = m_lua->lua().safe_script(
        R"(
        local events=IRInput.pointerGestures()
        assert(#events==2)
        assert(events[1].kind==IRInput.PointerGestureKind.MAGNIFY)
        assert(events[1].phase==IRInput.PointerGesturePhase.BEGAN)
        assert(events[1].magnification==0.25)
        assert(events[1].windowX==45.5 and events[1].windowY==90.25)
        assert(events[1].modifiers==IRInput.Modifier.SHIFT)
        assert(events[2].source==IRInput.PointerGestureSource.PRECISE_SCROLL)
        assert(events[2].momentumPhase==IRInput.PointerGesturePhase.BEGAN)
        assert(events[2].directionInverted)
        local x,y=IRInput.scrollOffset()
        assert(x==0.5 and y==-0.75 and IRInput.scrollDelta()==-1)
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
}

// isSyntheticInputActive() reflects the mode set by beginSyntheticInput().
TEST_F(LuaInputInjectionTest, IsSyntheticInputActiveReflectsMode) {
    auto &lua = m_lua->lua();
    auto r1 = lua.safe_script("return IRInput.isSyntheticInputActive()", sol::script_pass_on_error);
    ASSERT_TRUE(r1.valid());
    EXPECT_FALSE(r1.get<bool>()) << "should be inactive before beginSyntheticInput";

    auto r2 = lua.safe_script(
        "IRInput.beginSyntheticInput() return IRInput.isSyntheticInputActive()",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(r2.valid());
    EXPECT_TRUE(r2.get<bool>()) << "should be active after beginSyntheticInput";
}

} // namespace
