#include <irreden/script/lua_script.hpp>
#include <irreden/world/tool_spawn.hpp>

#include <gtest/gtest.h>

#include "../common/env_var.hpp"
#include "../common/executable_file.hpp"

#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <thread>

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
        m_wrapper = std::make_unique<IRTest::ScopedEnv>(
            "IR_RUN_WRAPPER",
            (m_root / "bin" / "ir-run").string().c_str()
        );
        IREngine::bindToolSpawnLua(m_lua, m_service, [this]() { return m_mainThread; });
    }

    ~LuaSpawnToolTest() override {
        std::error_code error;
        std::filesystem::remove_all(m_root, error);
    }

    std::filesystem::path makeRoot() {
        std::filesystem::create_directories(m_root / "creations");
        IRTest::writeExecutableFile(
            m_root / "bin" / "ir-build",
            "#!/usr/bin/env bash\n"
            "if [[ \"$2\" == Sleep ]]; then sleep 10; fi\n"
            "printf 'done\\n'\n"
        );
        IRTest::writeExecutableFile(m_root / "bin" / "ir-run", "#!/usr/bin/env bash\nexit 0\n");
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
    std::unique_ptr<IRTest::ScopedEnv> m_wrapper;
    bool m_mainThread = true;
};

TEST_F(LuaSpawnToolTest, ExposesHandleLifecycleAndRejectsAStaleHandle) {
    sol::protected_function_result result = m_lua.lua().safe_script(
        R"lua(first = IREngine.spawnTool{ tool = "ir-build", target = "Exit" })lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();

    std::optional<int> exit;
    for (int attempt = 0; attempt < 100 && !exit.has_value(); ++attempt) {
        result =
            m_lua.lua().safe_script("first:poll(); return first:exit()", sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << sol::error(result).what();
        exit = result.get<std::optional<int>>();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    ASSERT_EQ(exit, 0);

    result = m_lua.lua().safe_script(
        R"lua(
            assert(type(first:droppedLines()) == "number")
            local second = IREngine.spawnTool{ tool = "ir-build", target = "Sleep" }
            local ok, error = pcall(function() first:poll() end)
            assert(not ok and string.find(error, "stale tool handle", 1, true))
            second:poll()
            second:kill()
            assert(second:exit() ~= nil)
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

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
    expectRaises(
        "IREngine.spawnTool{ tool = 'editor', path = 'creations', editor = 'vim' }",
        "unknown editor"
    );
    expectRaises(
        "IREngine.spawnTool{ tool = 'editor', path = 'creations', editor = 1 }",
        "editor must be a string"
    );
    expectRaises(
        "IREngine.spawnTool{ tool = 'open-folder', path = 'creations', editor = 'code' }",
        "unknown key"
    );
}

TEST_F(LuaSpawnToolTest, ListsTheFixedEditorTable) {
    const sol::protected_function_result result = m_lua.lua().safe_script(
        R"lua(
            local editors = IREngine.toolEditors()
            assert(#editors == 2)
            assert(editors[1].name == "cursor" and editors[2].name == "code")
            assert(type(editors[1].found) == "boolean" and type(editors[2].found) == "boolean")
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

TEST_F(LuaSpawnToolTest, RefusesOffMainThread) {
    m_mainThread = false;
    expectRaises("IREngine.spawnTool{ tool = 'ir-build', target = 'IRDemo' }", "main thread");
}

TEST_F(LuaSpawnToolTest, HandleMethodsRefuseOffMainThread) {
    const sol::protected_function_result spawned = m_lua.lua().safe_script(
        "handle = IREngine.spawnTool{ tool = 'ir-build', target = 'Sleep' }",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(spawned.valid()) << sol::error(spawned).what();

    m_mainThread = false;
    expectRaises("handle:poll()", "must be called from the main thread");
    expectRaises("handle:exit()", "must be called from the main thread");
    expectRaises("handle:kill()", "must be called from the main thread");
    expectRaises("handle:droppedLines()", "must be called from the main thread");

    m_mainThread = true;
    const sol::protected_function_result killed =
        m_lua.lua().safe_script("handle:kill()", sol::script_pass_on_error);
    ASSERT_TRUE(killed.valid()) << sol::error(killed).what();
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
