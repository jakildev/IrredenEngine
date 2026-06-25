#include <gtest/gtest.h>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_window.hpp>
#include <irreden/script/lua_input_bindings.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/time/ir_time_types.hpp>

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
        auto result = m_lua.lua().safe_script(
            std::string("return ") + expr, sol::script_pass_on_error
        );
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
    EXPECT_EQ(
        evalInt("IRInput.ButtonStatuses.PRESSED"),
        evalInt("IRInput.ButtonStatus.PRESSED")
    );
    EXPECT_EQ(
        evalInt("IRInput.ButtonStatuses.NOT_HELD"),
        evalInt("IRInput.ButtonStatus.NOT_HELD")
    );
}

// ---- Synthetic-input function presence ------------------------------------

TEST_F(LuaInputBindingsTest, SyntheticInputFunctionsBound) {
    EXPECT_TRUE(isFunction("IRInput.beginSyntheticInput"));
    EXPECT_TRUE(isFunction("IRInput.isSyntheticInputActive"));
    EXPECT_TRUE(isFunction("IRInput.injectButton"));
    EXPECT_TRUE(isFunction("IRInput.injectMouseMove"));
    EXPECT_TRUE(isFunction("IRInput.injectScroll"));
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
        IRWindow::g_irglfwWindow =
            reinterpret_cast<IRWindow::IRGLFWWindow *>(m_window_sentinel);
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
