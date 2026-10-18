#include <irreden/script/lua_script.hpp>
#include <irreden/world/tool_spawn.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>

namespace {

class LuaSpawnToolTest : public testing::Test {
  protected:
    LuaSpawnToolTest()
        : m_root(
              std::filesystem::temp_directory_path() /
              ("ir_lua_spawn_" +
               std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))
          )
        , m_service(makeRoot()) {
        IREngine::bindToolSpawnLua(m_lua, m_service, [this]() { return m_mainThread; });
    }

    ~LuaSpawnToolTest() override {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    std::filesystem::path makeRoot() {
        std::filesystem::create_directories(m_root / "creations");
        return m_root;
    }

    void expectRaises(const char *source, const std::string &message) {
        const sol::protected_function_result result =
            m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_FALSE(result.valid()) << source;
        EXPECT_NE(std::string{sol::error(result).what()}.find(message), std::string::npos);
    }

    IRScript::LuaScript m_lua;
    std::filesystem::path m_root;
    IREngine::ToolService m_service;
    bool m_mainThread = true;
};

TEST_F(LuaSpawnToolTest, RefusesUnknownAndUntypedInput) {
    expectRaises("IREngine.spawnTool{ tool = 'shell', path = '.' }", "unknown tool");
    expectRaises("IREngine.spawnTool{ tool = 'ir-build', target = 4 }", "target must be a string");
    expectRaises(
        "IREngine.spawnTool{ tool = 'ir-build', target = 'IRDemo', cwd = '.' }",
        "unknown key"
    );
    expectRaises(
        "IREngine.spawnTool{ tool = 'ir-build', target = 'IRDemo', buildDir = 'build' }",
        "unknown key"
    );
    expectRaises(
        "IREngine.spawnTool{ tool = 'ir-build', target = 'IRDemo', args = {} }",
        "unknown key"
    );
    expectRaises(
        "IREngine.spawnTool{ tool = 'editor', path = 'a' .. string.char(0) .. 'b' }",
        "NUL byte"
    );
    expectRaises("IREngine.spawnTool{ tool = 'editor', path = '/etc' }", "inside the tool root");
}

TEST_F(LuaSpawnToolTest, RefusesOffMainThread) {
    m_mainThread = false;
    expectRaises("IREngine.spawnTool{ tool = 'ir-build', target = 'IRDemo' }", "main thread");
}

TEST_F(LuaSpawnToolTest, ExposesTheConfiguredRootReadOnly) {
    const sol::protected_function_result result = m_lua.lua().safe_script(
        "assert(type(IREngine.toolRoot()) == 'string')",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    const sol::object setter = m_lua.lua()["IREngine"]["setToolRoot"];
    EXPECT_TRUE(!setter.valid() || setter.get_type() == sol::type::lua_nil);
}

} // namespace
