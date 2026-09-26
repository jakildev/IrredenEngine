#include <gtest/gtest.h>

#include <irreden/profile/logger_spd.hpp>
#include <irreden/script/lua_script.hpp>

#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/ostream_sink.h>

#include <algorithm>
#include <memory>
#include <sstream>
#include <string>

namespace {

// Captures ScriptLog output for one test. The sink is erased in the
// destructor because LoggerSpd is a leaked process-global: a sink left
// attached would keep capturing every later test's script output. The
// pattern is set explicitly for the reason engine/profile/CLAUDE.md gives
// for the console sink — spdlog's default "%+" routes through
// full_formatter, whose thread_local MDC map crashes mingw's emutls at
// thread exit. The eol is pinned to "\n" because spdlog's default is "\r\n"
// on Windows, and the expectations are byte-exact.
class ScriptLogCapture {
  public:
    ScriptLogCapture()
        : m_captured{}
        , m_logger{LoggerSpd::instance()->getScriptLogger()}
        , m_sink{std::make_shared<spdlog::sinks::ostream_sink_st>(m_captured)} {
        m_sink->set_formatter(std::make_unique<spdlog::pattern_formatter>(
            "[%n] [%l] %v",
            spdlog::pattern_time_type::local,
            "\n"
        ));
        m_logger->sinks().push_back(m_sink);
    }

    ~ScriptLogCapture() {
        auto &sinks = m_logger->sinks();
        sinks.erase(std::remove(sinks.begin(), sinks.end(), m_sink), sinks.end());
    }

    ScriptLogCapture(const ScriptLogCapture &) = delete;
    ScriptLogCapture &operator=(const ScriptLogCapture &) = delete;

    std::string text() const {
        return m_captured.str();
    }

  private:
    std::ostringstream m_captured;
    spdlog::logger *m_logger;
    std::shared_ptr<spdlog::sinks::ostream_sink_st> m_sink;
};

class LuaPrintSinkTest : public testing::Test {
  protected:
    std::string printed(const char *script) {
        ScriptLogCapture capture;
        auto result = m_lua.lua().safe_script(script, sol::script_pass_on_error);
        EXPECT_TRUE(result.valid()) << sol::error{result}.what();
        return capture.text();
    }

    IRScript::LuaScript m_lua;
};

TEST_F(LuaPrintSinkTest, PrintRoutesToTheScriptLoggerTabJoined) {
    const std::string captured = printed("print('hello', 1, true, nil)");

    EXPECT_EQ(captured, "[ScriptLog] [info] hello\t1\ttrue\tnil\n");
}

TEST_F(LuaPrintSinkTest, PrintHonoursToStringMetamethods) {
    const std::string captured = printed(
        "local t = setmetatable({}, { __tostring = function() return 'CUSTOM' end })\n"
        "print(t)"
    );

    EXPECT_EQ(captured, "[ScriptLog] [info] CUSTOM\n");
}

// Stock `print` writes a bare newline for a call with no arguments, and never
// quotes or escapes a string argument — a run-log grep for a script's own text
// has to keep matching after the routing change.
TEST_F(LuaPrintSinkTest, PrintKeepsBodiesVerbatim) {
    EXPECT_EQ(printed("print()"), "[ScriptLog] [info] \n");
    EXPECT_EQ(
        printed("print('LUA_PROBE ok=1 path=a/b')"),
        "[ScriptLog] [info] LUA_PROBE ok=1 path=a/b\n"
    );
}

} // namespace
