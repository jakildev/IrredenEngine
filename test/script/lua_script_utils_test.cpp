#include <gtest/gtest.h>

#include <irreden/ir_math.hpp>
#include <irreden/script/ir_script_utils.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;
using IRMath::vec4;

// Covers the ordering contract of the six `*FromLua` helpers: concrete usertype
// FIRST, exact Lua table type SECOND, documented default otherwise.
//
// The wrong-typed-userdata cases need a driver rather than a direct C++ call.
// A helper that admits userdata into its table branch indexes it there, which
// raises a Lua error from inside the helper — so every helper is reached
// through a bound probe invoked from a protected `safe_script`, and each case
// asserts BOTH that the call did not raise AND that the documented default came
// back. Neither assertion alone is sufficient: a table-first shape check errors
// for a metatable-less userdata but silently coerces a registered one through
// its `__index`, so "it raised" and "it did not raise" each pass against one of
// the two shapes. See #2673.
class LuaScriptUtilsTest : public testing::Test {
  protected:
    LuaScriptUtilsTest() {
        m_lua.open_libraries(sol::lib::base);

        // Registered so the "silent coercion" arm has a userdata whose
        // metatable answers `x` / `y`. vec3/vec4/ivec3/Color stay UNregistered,
        // which is the metatable-less shape a binding hands a Lua system.
        m_lua.new_usertype<vec2>(
            "vec2",
            sol::constructors<vec2(float, float)>(),
            "x",
            &vec2::x,
            "y",
            &vec2::y
        );

        m_lua.set_function("probeVec3", [this](sol::object o) {
            m_vec3 = IRScript::vec3FromLua(o);
        });
        m_lua.set_function("probeVec2", [this](sol::object o) {
            m_vec2 = IRScript::vec2FromLua(o);
        });
        m_lua.set_function("probeVec4", [this](sol::object o) {
            m_vec4 = IRScript::vec4FromLua(o);
        });
        m_lua.set_function("probeIvec3", [this](sol::object o) {
            m_ivec3 = IRScript::ivec3FromLua(o);
        });
        m_lua.set_function("probeColor", [this](sol::object o) {
            m_color = IRScript::colorFromLua(o);
        });
        m_lua.set_function("probeQuat", [this](sol::object o) {
            m_quat = IRScript::quatFromLua(o);
        });
    }

    // Poisoned before each probe so a helper that never ran cannot be mistaken
    // for one that returned the default.
    void SetUp() override {
        m_vec2 = vec2{-1.0f, -1.0f};
        m_vec3 = vec3{-1.0f, -1.0f, -1.0f};
        m_vec4 = vec4{-1.0f, -1.0f, -1.0f, -1.0f};
        m_ivec3 = ivec3{-1, -1, -1};
        m_color = Color{1, 2, 3, 4};
        m_quat = vec4{-1.0f, -1.0f, -1.0f, -1.0f};
    }

    void run(const char *source) {
        auto result = m_lua.safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << source << " raised: " << sol::error{result}.what();
    }

    sol::state m_lua;
    vec2 m_vec2{};
    vec3 m_vec3{};
    vec4 m_vec4{};
    ivec3 m_ivec3{};
    Color m_color{};
    vec4 m_quat{};
};

// ---- positive controls: the accepted shapes still work ---------------------
//
// Without these, every "returns the default" assertion below would also pass
// against a helper that returned its default unconditionally.

TEST_F(LuaScriptUtilsTest, AcceptsMatchingUsertype) {
    m_lua["ud"] = vec3(1.0f, 2.0f, 3.0f);
    run("probeVec3(ud)");
    EXPECT_FLOAT_EQ(m_vec3.x, 1.0f);
    EXPECT_FLOAT_EQ(m_vec3.y, 2.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 3.0f);
}

TEST_F(LuaScriptUtilsTest, AcceptsKeyedAndIndexedTables) {
    run("probeVec3({x = 4, y = 5, z = 6})");
    EXPECT_FLOAT_EQ(m_vec3.x, 4.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 6.0f);

    run("probeVec3({7, 8, 9})");
    EXPECT_FLOAT_EQ(m_vec3.x, 7.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 9.0f);
}

// A table stays arity-blind by contract — this is what the fix must NOT change.
TEST_F(LuaScriptUtilsTest, PartialTableZeroFillsMissingComponents) {
    run("probeVec3({x = 4, y = 5})");
    EXPECT_FLOAT_EQ(m_vec3.x, 4.0f);
    EXPECT_FLOAT_EQ(m_vec3.y, 5.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 0.0f);
}

TEST_F(LuaScriptUtilsTest, NilYieldsTheDocumentedDefault) {
    run("probeVec3(nil)");
    EXPECT_FLOAT_EQ(m_vec3.x, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.y, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 0.0f);

    run("probeColor(nil)");
    EXPECT_EQ(m_color.red_, 255);
    EXPECT_EQ(m_color.alpha_, 255);

    run("probeQuat(nil)");
    EXPECT_FLOAT_EQ(m_quat.x, 0.0f);
    EXPECT_FLOAT_EQ(m_quat.w, 1.0f);
}

// ---- wrong-typed userdata reaches the documented default -------------------
//
// `vec2` IS registered here, so a table-first shape check would not raise: it
// reads `x`/`y` straight off the wrong vector's metatable and returns
// {7, 9, 0}, indistinguishable from a legitimate `{x = 7, y = 9}` table. The
// value, not the absence of an error, is therefore the assertion.
TEST_F(LuaScriptUtilsTest, Vec2UserdataWhereVec3ExpectedYieldsZeroDefault) {
    run("probeVec3(vec2.new(7, 9))");
    EXPECT_FLOAT_EQ(m_vec3.x, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.y, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 0.0f);
}

// The metatable-less shape of the same case: an unregistered usertype has no
// `__index`, so a table branch that admitted it would raise "attempt to index a
// userdata value" from inside the helper. `run()`'s valid-result assertion is
// what catches that half.
TEST_F(LuaScriptUtilsTest, UnregisteredUserdataWhereVec3ExpectedYieldsZeroDefault) {
    m_lua["ud"] = vec4(7.0f, 8.0f, 9.0f, 10.0f);
    run("probeVec3(ud)");
    EXPECT_FLOAT_EQ(m_vec3.x, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.y, 0.0f);
    EXPECT_FLOAT_EQ(m_vec3.z, 0.0f);
}

TEST_F(LuaScriptUtilsTest, WrongUserdataWhereVec2ExpectedYieldsZeroDefault) {
    m_lua["ud"] = vec3(7.0f, 8.0f, 9.0f);
    run("probeVec2(ud)");
    EXPECT_FLOAT_EQ(m_vec2.x, 0.0f);
    EXPECT_FLOAT_EQ(m_vec2.y, 0.0f);
}

TEST_F(LuaScriptUtilsTest, WrongUserdataWhereVec4ExpectedYieldsZeroDefault) {
    m_lua["ud"] = vec3(7.0f, 8.0f, 9.0f);
    run("probeVec4(ud)");
    EXPECT_FLOAT_EQ(m_vec4.x, 0.0f);
    EXPECT_FLOAT_EQ(m_vec4.y, 0.0f);
    EXPECT_FLOAT_EQ(m_vec4.z, 0.0f);
    EXPECT_FLOAT_EQ(m_vec4.w, 0.0f);
}

TEST_F(LuaScriptUtilsTest, WrongUserdataWhereIvec3ExpectedYieldsZeroDefault) {
    m_lua["ud"] = vec3(7.0f, 8.0f, 9.0f);
    run("probeIvec3(ud)");
    EXPECT_EQ(m_ivec3.x, 0);
    EXPECT_EQ(m_ivec3.y, 0);
    EXPECT_EQ(m_ivec3.z, 0);
}

// colorFromLua's default is opaque WHITE, not zero — the 255 floor is the
// documented "omitted alpha is opaque" convention, so the wrong-type path must
// land there and not on a transparent black.
TEST_F(LuaScriptUtilsTest, WrongUserdataWhereColorExpectedYieldsOpaqueWhite) {
    m_lua["ud"] = vec3(7.0f, 8.0f, 9.0f);
    run("probeColor(ud)");
    EXPECT_EQ(m_color.red_, 255);
    EXPECT_EQ(m_color.green_, 255);
    EXPECT_EQ(m_color.blue_, 255);
    EXPECT_EQ(m_color.alpha_, 255);
}

// quatFromLua identity-defaults (`w = 1`); a zero quat is degenerate.
TEST_F(LuaScriptUtilsTest, WrongUserdataWhereQuatExpectedYieldsIdentity) {
    m_lua["ud"] = vec3(7.0f, 8.0f, 9.0f);
    run("probeQuat(ud)");
    EXPECT_FLOAT_EQ(m_quat.x, 0.0f);
    EXPECT_FLOAT_EQ(m_quat.y, 0.0f);
    EXPECT_FLOAT_EQ(m_quat.z, 0.0f);
    EXPECT_FLOAT_EQ(m_quat.w, 1.0f);
}

// A `vec4` userdata is the accepted shape for BOTH vec4FromLua and quatFromLua
// — the wrong-type rejection above must not have cost them their shared
// usertype path.
TEST_F(LuaScriptUtilsTest, Vec4UserdataStillReachesBothVec4AndQuat) {
    m_lua["ud"] = vec4(1.0f, 2.0f, 3.0f, 4.0f);
    run("probeVec4(ud)");
    EXPECT_FLOAT_EQ(m_vec4.w, 4.0f);
    run("probeQuat(ud)");
    EXPECT_FLOAT_EQ(m_quat.w, 4.0f);
}

} // namespace
