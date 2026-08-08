#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>

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

// ---- Named Lua commands + the help-overlay registry (#2550) ---------------

// The trailing name / description strings are what put a Lua-defined command
// in the help overlay: the registry records only NAMED PRESSED bindings, so
// without them every Lua command body was invisible there.
TEST_F(LuaCommandTest, CreateCommandWithNameAndDescriptionEntersRegistry) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        return IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.J,
            function() end,
            nil, nil,
            "LUA PULSE",
            "FIRE THE LUA TEST PULSE"
        )
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();

    const auto &registrations = m_command_manager.getCommandRegistrations();
    ASSERT_EQ(registrations.size(), 1u);
    EXPECT_EQ(registrations[0].name, "LUA PULSE");
    EXPECT_EQ(registrations[0].description, "FIRE THE LUA TEST PULSE");
    EXPECT_EQ(registrations[0].button, IRInput::kKeyButtonJ);
}

// Both are optional: without them the command still registers and fires, it
// just doesn't appear in the overlay.
TEST_F(LuaCommandTest, CreateCommandWithoutNameStaysOutOfRegistry) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        _G.unnamed_fired = 0
        local id = IRCommand.createCommand(
            IRInput.InputType.KEY_MOUSE,
            IRInput.ButtonStatus.PRESSED,
            IRInput.Key.K,
            function() _G.unnamed_fired = _G.unnamed_fired + 1 end
        )
        IRCommand.fire(id)
        return _G.unnamed_fired
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<lua_Integer>(), 1);
    EXPECT_TRUE(m_command_manager.getCommandRegistrations().empty());
}

// The new prefab command must be spellable from Lua — a missing IR_BIND_CMD
// row resolves to nil at binding time, which is the silent failure mode the
// engine/command hand-list checklist exists to catch.
TEST_F(LuaCommandTest, ToggleHelpOverlayCommandNameIsBound) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return IRCommand.CommandName.TOGGLE_HELP_OVERLAY",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<lua_Integer>(), static_cast<lua_Integer>(IRCommand::TOGGLE_HELP_OVERLAY));
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

// ---- IRCommand.Suite / suiteDefaults / registerSuite ----------------------

TEST_F(LuaCommandTest, SuiteEnumIsAnIntegerTable) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        return type(IRCommand.Suite.CAMERA) == 'number'
           and type(IRCommand.Suite.CAPTURE) == 'number'
           and IRCommand.Suite.CAMERA ~= IRCommand.Suite.CAPTURE
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaCommandTest, SuiteDefaultsRowsMatchTheCppManifest) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        local rows = IRCommand.suiteDefaults(IRCommand.Suite.CAMERA)
        local released = 0
        for _, row in ipairs(rows) do
            if row.status == IRInput.ButtonStatus.RELEASED then released = released + 1 end
        end
        return #rows, released,
               rows[1].command, rows[1].button, rows[1].inputType, rows[1].modifiers
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(0), 11);
    EXPECT_EQ(result.get<int>(1), 4);
    EXPECT_EQ(result.get<int>(2), static_cast<int>(IRCommand::CLOSE_WINDOW));
    EXPECT_EQ(result.get<int>(3), static_cast<int>(IRInput::kKeyButtonEscape));
    EXPECT_EQ(result.get<int>(4), static_cast<int>(IRInput::KEY_MOUSE));
    EXPECT_EQ(result.get<int>(5), static_cast<int>(IRInput::kModifierNone));
}

TEST_F(LuaCommandTest, SuiteDefaultsCaptureHasThreeRows) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return #IRCommand.suiteDefaults(IRCommand.Suite.CAPTURE)",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(), 3);
}

TEST_F(LuaCommandTest, SuiteDefaultsRejectsAnOutOfRangeSuite) {
    // Logs and returns an empty table rather than throwing — same contract as
    // bindPrefab on an unimplemented command name.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script("return #IRCommand.suiteDefaults(99)", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(), 0);
}

TEST_F(LuaCommandTest, RegisterSuiteHonorsOmit) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRCommand.registerSuite(IRCommand.Suite.CAMERA,
                                {omit = {IRCommand.CommandName.CLOSE_WINDOW}})
        return true
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();

    const auto &regs = IRCommand::getCommandManager().getCommandRegistrations();
    ASSERT_EQ(regs.size(), 6u); // 7 PRESSED camera rows minus CLOSE_WINDOW
    for (const auto &reg : regs) {
        EXPECT_NE(reg.button, IRInput::kKeyButtonEscape);
    }
}

TEST_F(LuaCommandTest, RegisterSuiteHonorsRemapOntoKeypad) {
    // Also covers the keypad additions to IRInput.Key — the remap target is
    // only nameable from Lua because those entries now exist.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRCommand.registerSuite(IRCommand.Suite.CAMERA, {
            omit = {IRCommand.CommandName.CLOSE_WINDOW,
                    IRCommand.CommandName.ZOOM_IN,
                    IRCommand.CommandName.ZOOM_OUT},
            remap = {{IRInput.Key.W, IRInput.Key.KP_8},
                     {IRInput.Key.S, IRInput.Key.KP_2},
                     {IRInput.Key.A, IRInput.Key.KP_4},
                     {IRInput.Key.D, IRInput.Key.KP_6}},
        })
        return true
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();

    const auto &regs = IRCommand::getCommandManager().getCommandRegistrations();
    ASSERT_EQ(regs.size(), 4u);
    EXPECT_EQ(regs[0].button, IRInput::kKeyButtonKP8);
    EXPECT_EQ(regs[1].button, IRInput::kKeyButtonKP2);
    EXPECT_EQ(regs[2].button, IRInput::kKeyButtonKP4);
    EXPECT_EQ(regs[3].button, IRInput::kKeyButtonKP6);
}

TEST_F(LuaCommandTest, RegisterSuiteWithNoOverridesBindsTheWholeSuite) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "IRCommand.registerSuite(IRCommand.Suite.CAPTURE); return true",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(IRCommand::getCommandManager().getCommandRegistrations().size(), 3u);
}

TEST_F(LuaCommandTest, KeypadKeysAreNameable) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        return IRInput.Key.KP_0, IRInput.Key.KP_9, IRInput.Key.KP_ADD,
               IRInput.Key.KP_ENTER, IRInput.Key.KP_DECIMAL, IRInput.Key.KP_EQUAL
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(0), static_cast<int>(IRInput::kKeyButtonKP0));
    EXPECT_EQ(result.get<int>(1), static_cast<int>(IRInput::kKeyButtonKP9));
    EXPECT_EQ(result.get<int>(2), static_cast<int>(IRInput::kKeyButtonKPAdd));
    EXPECT_EQ(result.get<int>(3), static_cast<int>(IRInput::kKeyButtonKPEnter));
    EXPECT_EQ(result.get<int>(4), static_cast<int>(IRInput::kKeyButtonKPDecimal));
    EXPECT_EQ(result.get<int>(5), static_cast<int>(IRInput::kKeyButtonKPEqual));
}

// ---- IRCommand.isButtonBound / getRegisteredBindings (#2570) ---------------

// The acceptance criterion: W is bound after the camera suite registers. The
// RELEASED arm is the discriminator — `MOVE_CAMERA_UP_END` is
// {KEY_MOUSE, RELEASED, W} in the manifest and is never recorded in the
// registration map, so it is true iff the query scans the live user-command
// list. An implementation reading `getRegisteredBindings()` fails it.
TEST_F(LuaCommandTest, IsButtonBoundSeesCameraSuiteIncludingReleasedRows) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRCommand.registerSuite(IRCommand.Suite.CAMERA)
        local KM = IRInput.InputType.KEY_MOUSE
        return IRCommand.isButtonBound(KM, IRInput.ButtonStatus.PRESSED,  IRInput.Key.W),
               IRCommand.isButtonBound(KM, IRInput.ButtonStatus.RELEASED, IRInput.Key.W),
               IRCommand.isButtonBound(KM, IRInput.ButtonStatus.PRESSED,  IRInput.Key.ESCAPE),
               IRCommand.isButtonBound(KM, IRInput.ButtonStatus.PRESSED,  IRInput.Key.F12),
               IRCommand.isButtonBound(KM, IRInput.ButtonStatus.RELEASED, IRInput.Key.ESCAPE)
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_TRUE(result.get<bool>(0)) << "MOVE_CAMERA_UP_START binds PRESSED W";
    EXPECT_TRUE(result.get<bool>(1)) << "MOVE_CAMERA_UP_END binds RELEASED W (registry-invisible)";
    EXPECT_TRUE(result.get<bool>(2)) << "CLOSE_WINDOW binds PRESSED Escape";
    EXPECT_FALSE(result.get<bool>(3)) << "F12 is in no suite";
    EXPECT_FALSE(result.get<bool>(4)) << "Escape has no RELEASED half";
}

// The other half of the criterion: a creation that never registers the suite
// sees false. The fixture builds a fresh CommandManager per test, so this is
// the un-registered world. The trailing true arm proves the query is live
// rather than stuck returning false — without it, a hard-coded `return false`
// would pass this test.
TEST_F(LuaCommandTest, IsButtonBoundIsFalseWithoutTheCameraSuite) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        local KM = IRInput.InputType.KEY_MOUSE
        local before = IRCommand.isButtonBound(KM, IRInput.ButtonStatus.PRESSED, IRInput.Key.W)
        IRCommand.createCommand(KM, IRInput.ButtonStatus.PRESSED, IRInput.Key.W, function() end)
        local after = IRCommand.isButtonBound(KM, IRInput.ButtonStatus.PRESSED, IRInput.Key.W)
        return before, after
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_FALSE(result.get<bool>(0)) << "no camera suite registered in this fixture";
    EXPECT_TRUE(result.get<bool>(1)) << "an unnamed Lua command body still counts as bound";
}

TEST_F(LuaCommandTest, GetRegisteredBindingsReturnsTheNamedPressedRows) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRCommand.registerSuite(IRCommand.Suite.CAPTURE)
        local rows = IRCommand.getRegisteredBindings()
        local first = rows[1]
        return #rows, first.name, first.description, first.button, first.status,
               first.requiredModifiers, first.modifiers == nil
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    // Three named PRESSED rows; the camera suite's four RELEASED rows are
    // absent by the registry's own filter, which is why `isButtonBound` above
    // cannot be built on this surface.
    EXPECT_EQ(result.get<int>(0), 3);
    EXPECT_EQ(result.get<std::string>(1), IRCommand::commandNameToString(IRCommand::SCREENSHOT));
    EXPECT_EQ(result.get<std::string>(2), IRCommand::commandDescription(IRCommand::SCREENSHOT));
    EXPECT_EQ(result.get<int>(3), static_cast<int>(IRInput::kKeyButtonF8));
    EXPECT_EQ(result.get<int>(4), static_cast<int>(IRInput::PRESSED));
    EXPECT_EQ(result.get<int>(5), static_cast<int>(IRInput::kModifierNone));
    // The mask field is the *required* mask and says so — `CommandRegistration`
    // carries no blocked mask, so a bare `modifiers` would over-promise. The
    // nil arm pins the spelling: without it the row could carry both and the
    // rename would be untested.
    EXPECT_TRUE(result.get<bool>(6)) << "the mask ships as requiredModifiers only";
}

TEST_F(LuaCommandTest, GetRegisteredBindingsIsEmptyBeforeAnyNamedRegistration) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        local KM = IRInput.InputType.KEY_MOUSE
        IRCommand.createCommand(KM, IRInput.ButtonStatus.PRESSED, IRInput.Key.J, function() end)
        return #IRCommand.getRegisteredBindings()
        )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<int>(), 0) << "an unnamed binding must stay out of the registry view";
}

// ---- Idempotence ----------------------------------------------------------

TEST_F(LuaCommandTest, BindLuaCommandsIsIdempotent) {
    // A second call must not overwrite or duplicate the tables; the
    // earlier IRCommand handle and IRInput.Key.A integer must still be
    // valid afterward.
    auto &lua = m_lua.lua();
    const auto firstKeyA = lua.script("return IRInput.Key.A").get<lua_Integer>();
    m_lua.bindLuaCommands();
    const auto secondKeyA = lua.script("return IRInput.Key.A").get<lua_Integer>();
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
