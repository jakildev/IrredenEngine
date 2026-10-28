#include <gtest/gtest.h>

#include <irreden/script/lua_bindings_default.hpp>
#include <irreden/script/lua_script.hpp>

// A second `new_usertype<T>` strips T's metatables at the next GC, so every
// read below follows a forced full collection; without it a double
// registration reads fine and the test is vacuous.

namespace {

constexpr const char *kFullCollect = "collectgarbage('collect'); collectgarbage('collect')";

template <typename T> T runLua(sol::state &lua, const char *chunk) {
    sol::protected_function_result result = lua.safe_script(chunk, &sol::script_pass_on_error);
    EXPECT_TRUE(result.valid()) << chunk;
    if (!result.valid()) {
        return T{};
    }
    return result.get<T>();
}

TEST(LuaStandardBindingsTest, SecondCallIsANoOpAndTypesSurviveGc) {
    IRScript::LuaScript script;
    IRScript::registerStandardBindings(script);
    IRScript::registerStandardBindings(script);
    script.lua().safe_script(kFullCollect);
    sol::state &lua = script.lua();

    EXPECT_EQ(
        runLua<int>(
            lua,
            "local c = Color.new(1, 2, 3, 4); return c.r + c.g * 10 + c.b * 100 + c.a * 1000"
        ),
        4321
    );
    EXPECT_EQ(runLua<double>(lua, "local v = vec2.new(1.5, 2.5); return v.x + v.y * 10"), 26.5);
    EXPECT_EQ(
        runLua<double>(lua, "local v = vec3.new(1, 2, 3); return v.x + v.y * 10 + v.z * 100"),
        321.0
    );
    EXPECT_EQ(
        runLua<double>(
            lua,
            "local v = vec4.new(1, 2, 3, 4); return v.x + v.y * 10 + v.z * 100 + v.w * 1000"
        ),
        4321.0
    );
    EXPECT_EQ(runLua<int>(lua, "local v = ivec2.new(1, 2); return v.x + v.y * 10"), 21);
    EXPECT_EQ(
        runLua<int>(lua, "local v = ivec3.new(1, 2, 3); return v.x + v.y * 10 + v.z * 100"),
        321
    );
}

TEST(LuaStandardBindingsTest, SecondCallLeavesEnumsAndTablesUntouched) {
    IRScript::LuaScript script;
    IRScript::registerStandardBindings(script);
    sol::state &lua = script.lua();
    lua.safe_script(
        "firstEasing, firstMidi, firstAlignH, firstAlignV = "
        "IREasingFunction, MidiNote, TextAlignH, TextAlignV"
    );
    IRScript::registerStandardBindings(script);

    EXPECT_TRUE(
        runLua<bool>(
            lua,
            "return rawequal(IREasingFunction, firstEasing) and rawequal(MidiNote, firstMidi)"
            " and rawequal(TextAlignH, firstAlignH) and rawequal(TextAlignV, firstAlignV)"
        )
    );
}

TEST(LuaStandardBindingsTest, Vec3AddsAndSubtracts) {
    IRScript::LuaScript script;
    IRScript::registerStandardBindings(script);
    script.lua().safe_script(kFullCollect);
    sol::state &lua = script.lua();

    EXPECT_EQ(
        runLua<double>(
            lua,
            "local s = vec3.new(1, 2, 3) + vec3.new(10, 20, 30); return s.x + s.y + s.z"
        ),
        66.0
    );
    EXPECT_EQ(
        runLua<double>(
            lua,
            "local d = vec3.new(10, 20, 30) - vec3.new(1, 2, 3); return d.x + d.y + d.z"
        ),
        54.0
    );
}

TEST(LuaStandardBindingsTest, EnumsAndTextAlignTablesAreReadable) {
    IRScript::LuaScript script;
    IRScript::registerStandardBindings(script);
    IRScript::registerStandardBindings(script);
    script.lua().safe_script(kFullCollect);
    sol::state &lua = script.lua();

    EXPECT_EQ(runLua<int>(lua, "return IREasingFunction.LINEAR_INTERPOLATION"), 0);
    EXPECT_EQ(runLua<int>(lua, "return MidiNote.A0"), 21);
    EXPECT_EQ(runLua<int>(lua, "return TextAlignH.RIGHT"), 2);
    EXPECT_EQ(runLua<int>(lua, "return TextAlignV.BOTTOM"), 2);
}

} // namespace
