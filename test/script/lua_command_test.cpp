#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

namespace {

// Owns the minimum slice needed to exercise the T-193 IRCommand /
// IRInput Lua bindings end-to-end: CommandManager (sets g_commandManager
// in its ctor), EntityManager (needed by any prefab command that
// touches ECS state), and LuaScript with bindLuaCommands() called.
//
// Destruction order — see acceptance test #6 in
// docs/design/lua-input-commands.md "Lifetime contract": LuaScript
// must outlive CommandManager so the captured sol::protected_function
// references release while the sol::state is still open. The class
// declaration order picks the right teardown order (members destruct
// in reverse declaration order) — entity manager and command manager
// declared AFTER the Lua state.
class LuaCommandTest : public testing::Test {
  protected:
    LuaCommandTest()
        : m_lua{}
        , m_entity_manager{}
        , m_command_manager{} {
        m_lua.bindLuaCommands();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRCommand::CommandManager m_command_manager;
};

// ---- IRCommand.bindPrefab -------------------------------------------------

TEST_F(LuaCommandTest, BindPrefabReturnsValidCommandId) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        return IRCommand.bindPrefab(
            IRCommand.CommandName.SPAWN_PARTICLE_MOUSE_POSITION,
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.SPACE
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const auto id = result.get<lua_Integer>();
    EXPECT_GE(id, 0);
    EXPECT_NE(id, static_cast<lua_Integer>(IRCommand::kInvalidCommandId));
}

TEST_F(LuaCommandTest, BindPrefabUnimplementedReturnsInvalidCommandId) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        return IRCommand.bindPrefab(
            IRCommand.CommandName.NULL_COMMAND,
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.A
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    // kInvalidCommandId = ~0u — cast through lua_Integer for parity with
    // what the Lua side returns (Lua sees the value as an unsigned 32-bit
    // int converted to a Lua double).
    const auto id = result.get<lua_Integer>();
    EXPECT_EQ(static_cast<IRCommand::CommandId>(id), IRCommand::kInvalidCommandId);
}

// ---- IRCommand.createCommand (Lua closure body) ---------------------------

TEST_F(LuaCommandTest, CreateCommandRunsLuaClosureOnFire) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        _G.test_counter = 0
        local id = IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.A,
            function() _G.test_counter = _G.test_counter + 1 end
        )
        return id
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    const auto id = static_cast<IRCommand::CommandId>(result.get<lua_Integer>());

    // The closure has not fired yet — counter still 0.
    EXPECT_EQ(lua["test_counter"].get<int>(), 0);

    IRCommand::fire(id);
    EXPECT_EQ(lua["test_counter"].get<int>(), 1);

    IRCommand::fire(id);
    EXPECT_EQ(lua["test_counter"].get<int>(), 2);
}

TEST_F(LuaCommandTest, FireFromLuaInvokesLuaClosure) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        _G.lua_fire_counter = 0
        local id = IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.B,
            function() _G.lua_fire_counter = _G.lua_fire_counter + 1 end
        )
        IRCommand.fire(id)
        IRCommand.fire(id)
        IRCommand.fire(id)
        return _G.lua_fire_counter
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(), 3);
}

// ---- IRCommand.fireByName -------------------------------------------------

TEST_F(LuaCommandTest, FireByNameUnimplementedDoesNotCrash) {
    auto &lua = m_lua.lua();
    // NULL_COMMAND has no Command<NAME> specialization; fireByName must
    // log an error and return cleanly (no exception, no crash).
    auto result = lua.safe_script(
        "IRCommand.fireByName(IRCommand.CommandName.NULL_COMMAND); return true",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaCommandTest, FireByNameImplementedRunsBody) {
    // SPAWN_PARTICLE_MOUSE_POSITION's Command<NAME>::create() body is a
    // no-op stub (declares a local vec2). The test verifies the dispatch
    // path completes without exception for an implemented command —
    // covering the C++ side of the design's Q5 "fire a prefab command by
    // name" contract without requiring an IRRender / IRVideo manager
    // (which would need a full World).
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRCommand.fireByName(IRCommand.CommandName.SPAWN_PARTICLE_MOUSE_POSITION)
        return true
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>());
}

// ---- Error trap inside a Lua command body ---------------------------------

TEST_F(LuaCommandTest, LuaCommandBodyErrorDoesNotPropagate) {
    auto &lua = m_lua.lua();
    auto setup = lua.safe_script(
        R"(
        _G.before = 0
        _G.errored = false
        local id = IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.X,
            function()
                _G.before = _G.before + 1
                _G.errored = true
                error("intentional test error")
            end
        )
        return id
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << setup.get<sol::error>().what();
    const auto id = static_cast<IRCommand::CommandId>(setup.get<lua_Integer>());

    // The wrapper traps sol::protected_function errors in-VM and logs;
    // the call returns cleanly. The Lua body did run up to the error
    // (`before` counter incremented).
    EXPECT_NO_THROW(IRCommand::fire(id));
    EXPECT_EQ(lua["before"].get<int>(), 1);
    EXPECT_TRUE(lua["errored"].get<bool>());

    // A second fire still works — the error trap doesn't poison the
    // dispatch path.
    EXPECT_NO_THROW(IRCommand::fire(id));
    EXPECT_EQ(lua["before"].get<int>(), 2);
}

// ---- Modifier mask composition via LuaJIT bit.bor -------------------------

TEST_F(LuaCommandTest, BindPrefabAcceptsBitOrModifierMask) {
    // Reaches the design doc's Q6 spelling: compose Shift+Ctrl via the
    // native LuaJIT bit.bor without an IRInput.modMask helper.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        local mods = bit.bor(IRInput.Modifier.CONTROL, IRInput.Modifier.SHIFT)
        return IRCommand.bindPrefab(
            IRCommand.CommandName.GUI_ZOOM_IN,
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.EQUAL,
            mods
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_NE(
        static_cast<IRCommand::CommandId>(result.get<lua_Integer>()),
        IRCommand::kInvalidCommandId
    );
}

// ---- Idempotence ----------------------------------------------------------

TEST_F(LuaCommandTest, BindLuaCommandsIsIdempotent) {
    // A second call must not overwrite or duplicate the tables; the
    // earlier IRCommand handle and IRInput.Key.A integer must still be
    // valid afterward.
    auto &lua = m_lua.lua();
    const auto firstKeyA =
        lua.script("return IRInput.Key.A").get<lua_Integer>();
    m_lua.bindLuaCommands();
    const auto secondKeyA =
        lua.script("return IRInput.Key.A").get<lua_Integer>();
    EXPECT_EQ(firstKeyA, secondKeyA);

    // Functions still callable.
    auto result = lua.safe_script(
        "return type(IRCommand.bindPrefab) == 'function'",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>());
}

// ---- Teardown lifetime contract -------------------------------------------

TEST(LuaCommandTeardownTest, TeardownAfterRegistrationIsClean) {
    // Build the managers in the canonical engine/world declaration order so
    // destruction proceeds bottom-up the way World does at shutdown:
    // LuaScript first, then EntityManager, then CommandManager LAST in
    // declaration → CommandManager destructs FIRST. That order matters —
    // each std::function<void()> stored in CommandManager's m_userCommands
    // captures a sol::protected_function whose ref index sits in the
    // sol::state's registry. Destroying the wrappers WHILE the sol::state
    // is still open lets luaL_unref complete cleanly; destroying them
    // after sol::state is gone is UB (the registry ref index points into
    // freed memory). World mirrors this order at
    // engine/world/include/irreden/world.hpp:44-48 (m_lua first,
    // m_commandManager last).
    {
        IRScript::LuaScript lua;
        IREntity::EntityManager entityManager;
        IRCommand::CommandManager commandManager;
        lua.bindLuaCommands();

        auto result = lua.lua().safe_script(
            R"(
            IRCommand.createCommand(
                IRInput.InputType.KEY_MOUSE,
                IRInput.ButtonStatus.PRESSED,
                IRInput.Key.Q,
                function() end
            )
            return true
            )",
            sol::script_pass_on_error
        );
        ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    }
    SUCCEED();
}

} // namespace
