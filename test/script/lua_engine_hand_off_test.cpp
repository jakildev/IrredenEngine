#include <irreden/script/lua_script.hpp>
#include <irreden/world/hand_off.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using IREngine::HandOffRequest;
using IREngine::HandOffResult;

class LuaEngineHandOffTest : public testing::Test {
  protected:
    LuaEngineHandOffTest() {
        IREngine::bindHandOffLua(
            m_lua,
            [this](const HandOffRequest &request) {
                m_requests.push_back(request.argv_);
                return m_next;
            },
            [this]() { return m_onMainThread; }
        );
    }

    sol::protected_function_result run(const char *source) {
        return m_lua.lua().safe_script(source, sol::script_pass_on_error);
    }

    void expectRaises(const char *source, const std::string &message) {
        const sol::protected_function_result result = run(source);
        ASSERT_FALSE(result.valid()) << source;
        const std::string what = sol::error(result).what();
        EXPECT_NE(what.find("IREngine.handOff"), std::string::npos) << what;
        EXPECT_NE(what.find(message), std::string::npos) << what;
    }

    IRScript::LuaScript m_lua;
    std::vector<std::vector<std::string>> m_requests;
    HandOffResult m_next{true, true, 0, ""};
    bool m_onMainThread = true;
};

TEST_F(LuaEngineHandOffTest, PassesArgvAndReturnsTheResultTable) {
    const sol::protected_function_result result = run(R"lua(
        local r = IREngine.handOff{ argv = { "IRLuaWidgetsDemo", "--auto-screenshot", "3" } }
        assert(r.ok == true, "ok")
        assert(r.supported == true, "supported")
        assert(r.exitCode == 0, "exitCode")
        assert(r.error == nil, "error")
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    ASSERT_EQ(m_requests.size(), 1u);
    EXPECT_EQ(
        m_requests.front(),
        (std::vector<std::string>{"IRLuaWidgetsDemo", "--auto-screenshot", "3"})
    );
}

TEST_F(LuaEngineHandOffTest, ExplicitResumeTrueMatchesTheDefault) {
    const sol::protected_function_result result = run(R"lua(
        assert(IREngine.handOff{ argv = { "IRDemo" }, resume = true }.ok)
        assert(IREngine.handOff{ argv = { "IRDemo" }, resume = nil }.ok)
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(m_requests.size(), 2u);
}

TEST_F(LuaEngineHandOffTest, ChildFailureReportsExitCodeAndError) {
    m_next = HandOffResult{false, true, 3, "the child exited with 3"};
    const sol::protected_function_result result = run(R"lua(
        local r = IREngine.handOff{ argv = { "IRDemo" } }
        assert(r.ok == false and r.supported == true)
        assert(r.exitCode == 3)
        assert(r.error == "the child exited with 3")
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

TEST_F(LuaEngineHandOffTest, UnsupportedHostReturnsWithoutAnExitCode) {
    m_next = HandOffResult{false, false, std::nullopt, "not launched through ir-run"};
    const sol::protected_function_result result = run(R"lua(
        local r = IREngine.handOff{ argv = { "IRDemo" } }
        assert(r.ok == false and r.supported == false)
        assert(r.exitCode == nil)
        assert(type(r.error) == "string")
    )lua");
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
}

TEST_F(LuaEngineHandOffTest, MalformedInputRaisesBeforeTheHandOff) {
    expectRaises("IREngine.handOff('IRDemo')", "expects a table");
    expectRaises("IREngine.handOff{}", "non-empty array of strings");
    expectRaises("IREngine.handOff{ argv = 'IRDemo' }", "non-empty array of strings");
    expectRaises("IREngine.handOff{ argv = {} }", "non-empty array of strings");
    expectRaises("IREngine.handOff{ argv = { [1] = 'IRDemo', [3] = '-x' } }", "holes");
    expectRaises("IREngine.handOff{ argv = { 'IRDemo', 5 } }", "argv[2] must be a string");
    expectRaises("IREngine.handOff{ argv = { 'IRDemo', mode = 'x' } }", "non-integer key");
    expectRaises("IREngine.handOff{ argv = { [1.5] = 'IRDemo' } }", "non-integer key");
    expectRaises("IREngine.handOff{ argv = { 'IRDemo', [1e300] = '-x' } }", "holes");
    expectRaises("IREngine.handOff{ argv = { 'IRDemo', [math.huge] = '-x' } }", "holes");
    expectRaises(
        "IREngine.handOff{ argv = { 'IRDemo', '--x' .. string.char(0) .. 'y' } }",
        "argv[2] contains a NUL byte"
    );
    expectRaises("IREngine.handOff{ argv = { string.char(0) } }", "argv[1] contains a NUL byte");
    expectRaises("IREngine.handOff{ argv = { 'IRDemo' }, resume = 1 }", "boolean");
    EXPECT_TRUE(m_requests.empty());
}

TEST_F(LuaEngineHandOffTest, ResumeFalseIsRejected) {
    expectRaises("IREngine.handOff{ argv = { 'IRDemo' }, resume = false }", "resume = false");
    EXPECT_TRUE(m_requests.empty());
}

TEST_F(LuaEngineHandOffTest, OffTheMainThreadRaises) {
    m_onMainThread = false;
    expectRaises("IREngine.handOff{ argv = { 'IRDemo' } }", "main thread");
    EXPECT_TRUE(m_requests.empty());
}

// The binding in front of the real controller: outside ir-run the call is
// inert — no drain, no child, no lease verb.
TEST(LuaEngineHandOffUnsupportedTest, OutsideTheWrapperStartsNothing) {
    struct CountingPlatform : IREngine::HandOffPlatform {
        void drainGpu() override {
            ++drains_;
        }
        IREngine::ChildExit runToExit(const std::vector<std::string> &) override {
            ++launches_;
            return {true, 0, ""};
        }
        int drains_ = 0;
        int launches_ = 0;
    } platform;
    IRScript::LuaScript lua;
    IREngine::bindHandOffLua(
        lua,
        [&platform](const HandOffRequest &request) {
            return IREngine::runHandOff(request, IREngine::HandOffEnvironment{}, platform).result_;
        },
        []() { return true; }
    );
    const sol::protected_function_result result = lua.lua().safe_script(
        R"lua(
        local r = IREngine.handOff{ argv = { "IRDemo", "--auto-screenshot", "3" } }
        assert(r.ok == false and r.supported == false and r.exitCode == nil)
    )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error(result).what();
    EXPECT_EQ(platform.drains_, 0);
    EXPECT_EQ(platform.launches_, 0);
}

} // namespace
