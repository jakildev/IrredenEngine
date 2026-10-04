#include <gtest/gtest.h>

#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

class LuaCursorPosition : public testing::Test {
  protected:
    LuaCursorPosition() {
        m_lua.bindLuaCommands();
    }

    IRScript::LuaScript m_lua;
};

TEST_F(LuaCursorPosition, BindsOnlyTheSupportedCursorReaders) {
    auto result = m_lua.lua().safe_script(
        R"lua(
            return type(IRInput.mouseWorldPosAt),
                type(IRInput.mouseIsoScreen),
                IRInput.mousePosition2DIsoWorldRender,
                IRInput.mouseCanvasTexelWorld
        )lua",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<std::string>(0), "function");
    EXPECT_EQ(result.get<std::string>(1), "function");
    EXPECT_EQ(result.get<sol::object>(2).get_type(), sol::type::lua_nil);
    EXPECT_EQ(result.get<sol::object>(3).get_type(), sol::type::lua_nil);
}

TEST_F(LuaCursorPosition, RebindKeepsCursorReaderOverrides) {
    auto setSentinel = m_lua.lua().safe_script(
        "IRInput.mouseIsoScreen = function() return 'sentinel' end",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setSentinel.valid()) << setSentinel.get<sol::error>().what();

    m_lua.bindLuaCommands();

    auto result =
        m_lua.lua().safe_script("return IRInput.mouseIsoScreen()", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << result.get<sol::error>().what();
    EXPECT_EQ(result.get<std::string>(), "sentinel");
}

TEST_F(LuaCursorPosition, RejectsNonVectorReferencesBeforeRenderAccess) {
    for (const char *call : {"IRInput.mouseWorldPosAt(5)", "IRInput.mouseWorldPosAt()"}) {
        auto result = m_lua.lua().safe_script(call, sol::script_pass_on_error);
        ASSERT_FALSE(result.valid());
        const std::string error = result.get<sol::error>().what();
        EXPECT_NE(error.find("mouseWorldPosAt"), std::string::npos) << error;
    }
}

} // namespace
