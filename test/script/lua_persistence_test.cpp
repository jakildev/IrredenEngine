#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/lua_persistence_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

// Owns the managers `bindLuaDrivenEcs()` touches. Exercises the IRSave
// surface in-memory only — set/get/has/remove/clear + the Lua<->Value type
// conversion. The file I/O path (IRSave.load/save) resolves to the real
// per-user data dir, so it is covered by the hermetic C++ KeyValueStore
// FileRoundTrip test (writing to /tmp) rather than here, to keep this test
// from touching the developer's home directory.
class LuaPersistenceTest : public testing::Test {
  protected:
    LuaPersistenceTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    // Evaluates a Lua expression and returns whether it is truthy. Fails the
    // test (returns false) if the script errors.
    bool evalTrue(const std::string &expr) {
        auto result = m_lua.lua().safe_script(
            "return (" + expr + ") and true or false",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    // Returns true if running @p stmt raises a Lua error (binding rejected it).
    bool raisesError(const std::string &stmt) {
        auto result = m_lua.lua().safe_script(stmt, sol::script_pass_on_error);
        return !result.valid();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(LuaPersistenceTest, SurfaceBound) {
    EXPECT_TRUE(evalTrue("type(IRSave) == 'table'"));
    EXPECT_TRUE(evalTrue("type(IRSave.set) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.get) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.has) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.remove) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.clear) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.load) == 'function'"));
    EXPECT_TRUE(evalTrue("type(IRSave.save) == 'function'"));
}

TEST_F(LuaPersistenceTest, ScalarRoundTrips) {
    m_lua.lua().safe_script(R"(
        IRSave.set("s", "score", 12345)
        IRSave.set("s", "name", "ACE")
        IRSave.set("s", "soundOn", true)
        IRSave.set("s", "musicOn", false)
    )");
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'score') == 12345"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'name') == 'ACE'"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'soundOn') == true"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'musicOn') == false"));
}

TEST_F(LuaPersistenceTest, ListRoundTrips) {
    m_lua.lua().safe_script(R"(
        IRSave.set("s", "topScores", { 5000, 3000, 1000 })
        IRSave.set("s", "players", { "ACE", "BEE" })
    )");
    EXPECT_TRUE(evalTrue("#IRSave.get('s', 'topScores') == 3"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'topScores')[1] == 5000"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'topScores')[3] == 1000"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'players')[2] == 'BEE'"));
}

TEST_F(LuaPersistenceTest, GetReturnsDefaultWhenAbsent) {
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'nope', 42) == 42"));
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'nope') == nil"));
    // Default is only used when the key is absent, never to mask a stored value.
    m_lua.lua().safe_script("IRSave.set('s', 'present', 7)");
    EXPECT_TRUE(evalTrue("IRSave.get('s', 'present', 42) == 7"));
}

TEST_F(LuaPersistenceTest, HasRemoveClear) {
    m_lua.lua().safe_script(R"(
        IRSave.set("s", "a", 1)
        IRSave.set("s", "b", 2)
    )");
    EXPECT_TRUE(evalTrue("IRSave.has('s', 'a')"));
    EXPECT_TRUE(evalTrue("IRSave.remove('s', 'a') == true"));
    EXPECT_TRUE(evalTrue("IRSave.has('s', 'a') == false"));
    EXPECT_TRUE(evalTrue("IRSave.remove('s', 'a') == false")); // already gone
    EXPECT_TRUE(evalTrue("IRSave.has('s', 'b')"));
    m_lua.lua().safe_script("IRSave.clear('s')");
    EXPECT_TRUE(evalTrue("IRSave.has('s', 'b') == false"));
}

TEST_F(LuaPersistenceTest, StoresAreNamespacedByName) {
    m_lua.lua().safe_script(R"(
        IRSave.set("scores", "top", 100)
        IRSave.set("settings", "top", 5)
    )");
    EXPECT_TRUE(evalTrue("IRSave.get('scores', 'top') == 100"));
    EXPECT_TRUE(evalTrue("IRSave.get('settings', 'top') == 5"));
}

TEST_F(LuaPersistenceTest, NonArrayTableIsRejected) {
    // A map-style table can't be represented (lists are arrays only) — the
    // binding must raise a clear error rather than silently dropping it.
    EXPECT_TRUE(raisesError("IRSave.set('s', 'bad', { a = 1, b = 2 })"));
}

TEST_F(LuaPersistenceTest, NestedListIsRejected) {
    // Lists do not nest in v1.
    EXPECT_TRUE(raisesError("IRSave.set('s', 'bad', { { 1, 2 }, { 3, 4 } })"));
}

} // namespace
