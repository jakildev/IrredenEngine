#include <gtest/gtest.h>

#include <irreden/ir_script.hpp>
#include <irreden/input/ir_input_types.hpp>

TEST(LuaInputEnumsTest, BindsKeysWithoutCommandSurface) {
    IRScript::LuaScript lua;
    lua.bindLuaInputEnums();

    const sol::protected_function_result result = lua.lua().safe_script(R"lua(
        return IRInput.Key.S, IRCommand
    )lua");
    ASSERT_TRUE(result.valid());
    const std::tuple<lua_Integer, sol::object> values = result;
    EXPECT_EQ(std::get<0>(values), static_cast<lua_Integer>(IRInput::kKeyButtonS));
    EXPECT_EQ(std::get<1>(values).get_type(), sol::type::lua_nil);
}
