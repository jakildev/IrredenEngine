#include <gtest/gtest.h>

#include <irreden/script/lua_script.hpp>

namespace {

// `getTable` backs every creation's `--config-preset` / `config.lua` read. A
// preset may carry only the tables its reader owns, so a nil global has to come
// back as an invalid table the caller's `.valid()` guard can test — not throw.
class LuaScriptGetTableTest : public testing::Test {
  protected:
    IRScript::LuaScript m_lua;
};

TEST_F(LuaScriptGetTableTest, MissingGlobalReturnsInvalidTable) {
    sol::table table;
    EXPECT_NO_THROW(table = m_lua.getTable("no_such_global"));
    EXPECT_FALSE(table.valid());
}

TEST_F(LuaScriptGetTableTest, NonTableGlobalReturnsInvalidTable) {
    m_lua.lua()["scalar_global"] = 42;
    sol::table table;
    EXPECT_NO_THROW(table = m_lua.getTable("scalar_global"));
    EXPECT_FALSE(table.valid());
}

TEST_F(LuaScriptGetTableTest, TableGlobalReturnsValidTable) {
    m_lua.lua().safe_script("present = { answer = 42 }");
    const sol::table table = m_lua.getTable("present");
    ASSERT_TRUE(table.valid());
    EXPECT_EQ(table["answer"].get<int>(), 42);
}

} // namespace
