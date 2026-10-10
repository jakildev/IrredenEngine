#include <irreden/script/lua_script.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <string>

namespace {

class LuaFileBindingsTest : public testing::Test {
  protected:
    LuaFileBindingsTest()
        : m_testDirectory(std::filesystem::current_path() / "lua_file_bindings_test") {
        std::error_code error;
        std::filesystem::remove_all(m_testDirectory, error);
        m_lua.bindLuaFiles();
    }

    ~LuaFileBindingsTest() override {
        std::error_code error;
        std::filesystem::remove_all(m_testDirectory, error);
    }

    IRScript::LuaScript m_lua;
    std::filesystem::path m_testDirectory;
};

TEST_F(LuaFileBindingsTest, RoundTripsNestedTextAndReportsModificationTime) {
    const sol::protected_function_result result = m_lua.lua().safe_script(
        R"lua(
        local path = 'lua_file_bindings_test/nested/settings.lua'
        assert(IRFile.writeText(path, 'return { volume = 0.75 }'))
        assert(IRFile.readText(path) == 'return { volume = 0.75 }')
        assert(IRFile.mtime(path) ~= 0)
        assert(IRFile.mtime('lua_file_bindings_test/missing.lua') == 0)
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_TRUE(std::filesystem::is_regular_file(m_testDirectory / "nested/settings.lua"));
}

TEST_F(LuaFileBindingsTest, RejectsAbsoluteAndEscapingPaths) {
    const std::string absolutePath = (m_testDirectory / "absolute.txt").string();
    m_lua.lua()["absolutePath"] = absolutePath;
    const sol::protected_function_result result = m_lua.lua().safe_script(
        R"lua(
        assert(IRFile.writeText('../lua_file_bindings_outside.txt', 'outside') == false)
        assert(IRFile.readText('../lua_file_bindings_outside.txt') == nil)
        assert(IRFile.mtime('../lua_file_bindings_outside.txt') == 0)
        assert(IRFile.writeText(absolutePath, 'absolute') == false)
        assert(IRFile.readText(absolutePath) == nil)
        assert(IRFile.mtime(absolutePath) == 0)
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_FALSE(std::filesystem::exists(m_testDirectory / "absolute.txt"));
    EXPECT_FALSE(
        std::filesystem::exists(
            std::filesystem::current_path().parent_path() / "lua_file_bindings_outside.txt"
        )
    );
}

TEST(LuaFileBindingRegistrationTest, IsOptInIdempotentAndPreservesCustomKeys) {
    IRScript::LuaScript lua;
    EXPECT_FALSE(lua.lua()["IRFile"].valid());
    lua.lua().safe_script("IRFile = { custom = 17 }");
    lua.bindLuaFiles();
    lua.bindLuaFiles();
    EXPECT_EQ(lua.lua()["IRFile"]["custom"].get<int>(), 17);
    EXPECT_EQ(lua.lua()["IRFile"]["readText"].get_type(), sol::type::function);
}

TEST(LuaFileBindingRegistrationTest, RejectsNonTableCollision) {
    IRScript::LuaScript lua;
    lua.lua()["IRFile"] = 17;
    EXPECT_THROW(lua.bindLuaFiles(), std::invalid_argument);
    EXPECT_EQ(lua.lua()["IRFile"].get<int>(), 17);
}

} // namespace
