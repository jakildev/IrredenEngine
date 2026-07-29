#include <gtest/gtest.h>

#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

namespace {

// Covers IRSystem::EntityEventHandlers::clear() — the mechanism the #2572 exit
// SIGSEGV fix depends on. getEntityEventHandlers() returns a process-lifetime
// static holding sol::protected_function refs into whatever LuaScript VM
// registered them; clear() drops every handler (destroying those refs) so the
// engine tail can empty the registry while the VM is still alive, before the
// static's own destructor would otherwise run luaL_unref after lua_close.
//
// Member order is load-bearing: m_lua is the only member and outlives the
// TearDown() clear(), so the sol::protected_functions are unref'd against a
// still-open lua_State (same lifetime contract as lua_input_bindings_test.cpp).
class EntityEventHandlersTest : public testing::Test {
  protected:
    void TearDown() override {
        // Leave the process-static registry empty even if an assertion aborts
        // the body before its own clear() — a surviving ref would luaL_unref
        // into the closed VM at process exit and segfault the whole test
        // binary, masking the real failure. This is the exact clear() contract
        // #2572 added at the engine tail.
        IRSystem::getEntityEventHandlers().clear();
    }

    IRScript::LuaScript m_lua;
};

// Register one handler through each of the four add* paths, then clear(): every
// handler vector must go from non-empty back to empty. The pre-clear size
// assertions are the positive control — they prove the registrations landed, so
// the post-clear emptiness is a real drop rather than a vacuous already-empty
// pass on the shared process static.
TEST_F(EntityEventHandlersTest, ClearEmptiesAllFourHandlerVectors) {
    auto &handlers = IRSystem::getEntityEventHandlers();
    handlers.clear(); // known-empty baseline; the static may carry prior state

    ASSERT_TRUE(
        m_lua.lua().safe_script("noopHandler = function() end", sol::script_pass_on_error).valid()
    );
    sol::protected_function fn = m_lua.lua()["noopHandler"];

    handlers.addOnHovered(fn);
    handlers.addOnUnhovered(fn);
    handlers.addOnClicked(fn);
    handlers.addOnRightClick(fn);

    EXPECT_EQ(handlers.onHovered.size(), 1u);
    EXPECT_EQ(handlers.onUnhovered.size(), 1u);
    EXPECT_EQ(handlers.onClicked.size(), 1u);
    EXPECT_EQ(handlers.onRightClick.size(), 1u);

    handlers.clear();

    EXPECT_TRUE(handlers.onHovered.empty());
    EXPECT_TRUE(handlers.onUnhovered.empty());
    EXPECT_TRUE(handlers.onClicked.empty());
    EXPECT_TRUE(handlers.onRightClick.empty());
}

} // namespace
