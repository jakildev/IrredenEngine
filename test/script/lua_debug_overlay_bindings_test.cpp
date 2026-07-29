#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/ir_script_utils.hpp>
#include <irreden/script/lua_debug_overlay_bindings.hpp>
#include <irreden/script/lua_script.hpp>

#include <irreden/render/debug_overlay_draws.hpp>

#include <sol/sol.hpp>

#include <string>

namespace {

using IRMath::vec3;
using IRMath::vec4;

// Owns the managers `bindLuaDrivenEcs()` touches. There is deliberately NO
// RenderManager, and unlike `LuaRenderBindingsTest` this fixture does not stop
// at presence checks: every IRDebug draw is a pure CPU `push_back` into the
// `debug_overlay_draws.hpp` buffers, so the bindings can be INVOKED headless
// and asserted against the resulting records. Only the DEBUG_OVERLAY flush
// needs a GPU, and it is not exercised here.
class LuaDebugOverlayBindingsTest : public testing::Test {
  protected:
    LuaDebugOverlayBindingsTest()
        // LuaScript first → destroyed last (see test/CLAUDE.md "Lua seam tests").
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    // The buffers are process-wide inline statics shared across TUs, so tests
    // must not inherit each other's records.
    void SetUp() override {
        IRDebug::clear();
    }
    void TearDown() override {
        IRDebug::clear();
    }

    bool isFunction(const char *expr) {
        auto result = m_lua.lua().safe_script(
            std::string("return type(") + expr + ") == 'function'",
            sol::script_pass_on_error
        );
        return result.valid() && result.get<bool>();
    }

    // Runs `source` and asserts it did NOT raise, so a binding regression
    // surfaces as this assertion rather than an empty-buffer mystery.
    void run(const char *source) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        ASSERT_TRUE(result.valid()) << "Lua raised: " << sol::error{result}.what();
    }

    bool raises(const char *source) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        return !result.valid();
    }

    // Asserting the MESSAGE, not just that something raised, is what makes the
    // wrong-vector-type cases meaningful: those already raised before the
    // per-type check landed, but from `*FromLua` indexing a metatable-less
    // userdata ("attempt to index a userdata value") — an error that names
    // neither the argument nor the expected type.
    testing::AssertionResult raisesWith(const char *source, const std::string &needle) {
        auto result = m_lua.lua().safe_script(source, sol::script_pass_on_error);
        if (result.valid()) {
            return testing::AssertionFailure() << source << " did not raise";
        }
        const std::string message = sol::error{result}.what();
        if (message.find(needle) == std::string::npos) {
            return testing::AssertionFailure() << source << " raised \"" << message
                                               << "\", expected it to mention \"" << needle << '"';
        }
        return testing::AssertionSuccess();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// ---- presence --------------------------------------------------------------

TEST_F(LuaDebugOverlayBindingsTest, EveryDrawIsBound) {
    EXPECT_TRUE(isFunction("IRDebug.drawLine3D"));
    EXPECT_TRUE(isFunction("IRDebug.drawCircle3D"));
    EXPECT_TRUE(isFunction("IRDebug.drawTriangle3D"));
    EXPECT_TRUE(isFunction("IRDebug.drawDiamond3D"));
    EXPECT_TRUE(isFunction("IRDebug.drawPath3D"));
    EXPECT_TRUE(isFunction("IRDebug.drawLineScreen"));
    EXPECT_TRUE(isFunction("IRDebug.drawTriangleScreen"));
    EXPECT_TRUE(isFunction("IRDebug.drawRectScreen"));
    EXPECT_TRUE(isFunction("IRDebug.drawDotScreen"));
}

// `clear()` stays unbound on purpose: the DEBUG_OVERLAY flush owns clearing,
// and a Lua caller clearing mid-frame would silently drop other systems' draws.
TEST_F(LuaDebugOverlayBindingsTest, ClearIsNotBound) {
    EXPECT_FALSE(isFunction("IRDebug.clear"));
}

TEST_F(LuaDebugOverlayBindingsTest, ExtendsExistingIRDebugTable) {
    auto &lua = m_lua.lua();
    lua["IRDebug"]["creationOnly"] = 42;
    IRScript::detail::bindDebugOverlay(m_lua); // re-run as a creation would
    EXPECT_EQ(lua["IRDebug"]["creationOnly"].get<int>(), 42);
    EXPECT_TRUE(isFunction("IRDebug.drawLine3D"));
}

// ---- 3D draws: invocation + buffer contents --------------------------------

TEST_F(LuaDebugOverlayBindingsTest, DrawLine3DBuffersOneRecordAndDefaultsAlpha) {
    run("IRDebug.drawLine3D({1, 2, 3}, {4, 5, 6}, 1, 0, 0)");

    const auto &lines = IRDebug::getLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FLOAT_EQ(lines[0].from.x, 1.0f);
    EXPECT_FLOAT_EQ(lines[0].from.y, 2.0f);
    EXPECT_FLOAT_EQ(lines[0].from.z, 3.0f);
    EXPECT_FLOAT_EQ(lines[0].to.x, 4.0f);
    EXPECT_FLOAT_EQ(lines[0].to.y, 5.0f);
    EXPECT_FLOAT_EQ(lines[0].to.z, 6.0f);
    EXPECT_FLOAT_EQ(lines[0].r, 1.0f);
    EXPECT_FLOAT_EQ(lines[0].g, 0.0f);
    EXPECT_FLOAT_EQ(lines[0].b, 0.0f);
    EXPECT_FLOAT_EQ(lines[0].a, 1.0f); // omitted → C++ default
}

TEST_F(LuaDebugOverlayBindingsTest, DrawLine3DAcceptsKeyedTableAndExplicitAlpha) {
    run("IRDebug.drawLine3D({x = 1, y = 2, z = 3}, {x = 4, y = 5, z = 6}, 0, 1, 0, 0.25)");

    const auto &lines = IRDebug::getLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FLOAT_EQ(lines[0].from.y, 2.0f);
    EXPECT_FLOAT_EQ(lines[0].to.z, 6.0f);
    EXPECT_FLOAT_EQ(lines[0].a, 0.25f);
}

// `IRMath::vec3` is not a creation-registered usertype, but sol2 makes any C++
// value pushed from the host side a userdata — which is the shape a Lua system
// receives when a binding hands it a vec3. Exercises the `obj.is<vec3>()`
// branch of the arg helpers, not just the table branch.
TEST_F(LuaDebugOverlayBindingsTest, DrawLine3DAcceptsVec3Userdata) {
    auto &lua = m_lua.lua();
    lua["fromUd"] = vec3(7.0f, 8.0f, 9.0f);
    lua["toUd"] = vec3(10.0f, 11.0f, 12.0f);
    run("IRDebug.drawLine3D(fromUd, toUd, 1, 1, 1)");

    const auto &lines = IRDebug::getLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FLOAT_EQ(lines[0].from.x, 7.0f);
    EXPECT_FLOAT_EQ(lines[0].from.z, 9.0f);
    EXPECT_FLOAT_EQ(lines[0].to.x, 10.0f);
    EXPECT_FLOAT_EQ(lines[0].to.z, 12.0f);
}

TEST_F(LuaDebugOverlayBindingsTest, DrawCircle3DDefaultsSegmentsTo32) {
    run("IRDebug.drawCircle3D({0, 0, 0}, 2.5, 1, 1, 0)");

    const auto &circles = IRDebug::getCircles();
    ASSERT_EQ(circles.size(), 1u);
    EXPECT_FLOAT_EQ(circles[0].radius, 2.5f);
    EXPECT_FLOAT_EQ(circles[0].a, 1.0f);
    EXPECT_EQ(circles[0].segments, 32);
}

// The flush clamps the top end to kCircleLutMaxSegments; the binding records
// the caller's value verbatim above the kMinCircleSegments floor.
TEST_F(LuaDebugOverlayBindingsTest, DrawCircle3DRecordsSegmentsOverrideVerbatim) {
    run("IRDebug.drawCircle3D({0, 0, 0}, 1, 1, 1, 1, 0.5, 64)");

    const auto &circles = IRDebug::getCircles();
    ASSERT_EQ(circles.size(), 1u);
    EXPECT_FLOAT_EQ(circles[0].a, 0.5f);
    EXPECT_EQ(circles[0].segments, 64);
}

// The floor is the raise-don't-silently-misdraw contract applied to `segments`:
// 0 reached an integer division by zero in the flush, and 1/2 can't close a
// ring. Buffering nothing is what keeps the bad record away from the flush.
TEST_F(LuaDebugOverlayBindingsTest, DrawCircle3DBelowMinimumSegmentsRaises) {
    EXPECT_TRUE(raises("IRDebug.drawCircle3D({0, 0, 0}, 1, 1, 1, 1, 1, 0)"));
    EXPECT_TRUE(IRDebug::getCircles().empty());

    EXPECT_TRUE(raises("IRDebug.drawCircle3D({0, 0, 0}, 1, 1, 1, 1, 1, -4)"));
    EXPECT_TRUE(IRDebug::getCircles().empty());

    // kMinCircleSegments itself is accepted.
    run("IRDebug.drawCircle3D({0, 0, 0}, 1, 1, 1, 1, 1, 3)");
    ASSERT_EQ(IRDebug::getCircles().size(), 1u);
    EXPECT_EQ(IRDebug::getCircles()[0].segments, IRDebug::kMinCircleSegments);
}

// A segment count that doesn't divide the LUT must still close the ring: the
// last vertex has to land back on the first. Stepping the table truncated, so
// 12 segments stopped at 270 degrees.
TEST_F(LuaDebugOverlayBindingsTest, CircleUnitPointClosesTheRingForNonDivisorSegmentCounts) {
    const IRDebug::CircleLut &lut = IRDebug::getCircleLut();
    for (const int segs : {3, 5, 12, 16, 32}) {
        const IRMath::vec2 first = IRDebug::circleUnitPoint(0, segs, lut);
        const IRMath::vec2 last = IRDebug::circleUnitPoint(segs, segs, lut);
        EXPECT_NEAR(first.x, last.x, 1e-5f) << "segments=" << segs;
        EXPECT_NEAR(first.y, last.y, 1e-5f) << "segments=" << segs;
    }
}

TEST_F(LuaDebugOverlayBindingsTest, DrawTriangle3DBuffersOneRecord) {
    run("IRDebug.drawTriangle3D({0, 0, 0}, {1, 0, 0}, {0, 1, 0}, 0.1, 0.2, 0.3)");

    const auto &triangles = IRDebug::getTriangles();
    ASSERT_EQ(triangles.size(), 1u);
    EXPECT_FLOAT_EQ(triangles[0].b.x, 1.0f);
    EXPECT_FLOAT_EQ(triangles[0].c.y, 1.0f);
    EXPECT_FLOAT_EQ(triangles[0].r, 0.1f);
    EXPECT_FLOAT_EQ(triangles[0].g, 0.2f);
    EXPECT_FLOAT_EQ(triangles[0].bColor, 0.3f);
    EXPECT_FLOAT_EQ(triangles[0].aColor, 1.0f);
}

// drawDiamond3D decomposes into exactly two triangles C++-side.
TEST_F(LuaDebugOverlayBindingsTest, DrawDiamond3DDecomposesIntoTwoTriangles) {
    run("IRDebug.drawDiamond3D({5, 5, 0}, 1, 1, 0, 1)");

    EXPECT_EQ(IRDebug::getTriangles().size(), 2u);
}

// A 3-point path is 2 chained segments: (p0,p1), (p1,p2).
TEST_F(LuaDebugOverlayBindingsTest, DrawPath3DChainsSegments) {
    run("IRDebug.drawPath3D({ {0, 0, 0}, {1, 1, 1}, {2, 2, 2} }, 1, 1, 1)");

    const auto &lines = IRDebug::getLines();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_FLOAT_EQ(lines[0].from.x, 0.0f);
    EXPECT_FLOAT_EQ(lines[0].to.x, 1.0f);
    EXPECT_FLOAT_EQ(lines[1].from.x, 1.0f);
    EXPECT_FLOAT_EQ(lines[1].to.x, 2.0f);
}

TEST_F(LuaDebugOverlayBindingsTest, DrawPath3DWithFewerThanTwoPointsEmitsNothing) {
    run("IRDebug.drawPath3D({ {0, 0, 0} }, 1, 1, 1)");

    EXPECT_TRUE(IRDebug::getLines().empty());
}

// ---- screen-space draws ----------------------------------------------------

TEST_F(LuaDebugOverlayBindingsTest, DrawLineScreenBuffersOneRecord) {
    run("IRDebug.drawLineScreen({10, 20}, {30, 40}, 1, 0, 1, 0.75)");

    const auto &lines = IRDebug::getScreenLines();
    ASSERT_EQ(lines.size(), 1u);
    EXPECT_FLOAT_EQ(lines[0].from.x, 10.0f);
    EXPECT_FLOAT_EQ(lines[0].from.y, 20.0f);
    EXPECT_FLOAT_EQ(lines[0].to.x, 30.0f);
    EXPECT_FLOAT_EQ(lines[0].to.y, 40.0f);
    EXPECT_FLOAT_EQ(lines[0].a, 0.75f);
}

TEST_F(LuaDebugOverlayBindingsTest, DrawTriangleScreenBuffersOneRecord) {
    run("IRDebug.drawTriangleScreen({0, 0}, {4, 0}, {0, 4}, 1, 1, 1)");

    const auto &triangles = IRDebug::getScreenTriangles();
    ASSERT_EQ(triangles.size(), 1u);
    EXPECT_FLOAT_EQ(triangles[0].b.x, 4.0f);
    EXPECT_FLOAT_EQ(triangles[0].c.y, 4.0f);
    EXPECT_FLOAT_EQ(triangles[0].aColor, 1.0f);
}

// The C++ decomposition: 2 fill triangles + 4 border lines.
TEST_F(LuaDebugOverlayBindingsTest, DrawRectScreenDecomposesIntoTwoTrianglesAndFourLines) {
    run("IRDebug.drawRectScreen({0, 0}, {10, 10}, {1, 0, 0, 1}, {0, 1, 0, 1})");

    ASSERT_EQ(IRDebug::getScreenTriangles().size(), 2u);
    ASSERT_EQ(IRDebug::getScreenLines().size(), 4u);
    EXPECT_FLOAT_EQ(IRDebug::getScreenTriangles()[0].r, 1.0f); // fill
    EXPECT_FLOAT_EQ(IRDebug::getScreenLines()[0].g, 1.0f);     // border
}

// vec4 colors accept the {r,g,b,a} spelling as well as {x,y,z,w} / indexed.
TEST_F(LuaDebugOverlayBindingsTest, DrawRectScreenAcceptsRgbaKeyedColors) {
    run("IRDebug.drawRectScreen({0, 0}, {2, 2},"
        " {r = 0.25, g = 0.5, b = 0.75, a = 1},"
        " {r = 1, g = 1, b = 1, a = 0.5})");

    const auto &triangles = IRDebug::getScreenTriangles();
    ASSERT_EQ(triangles.size(), 2u);
    EXPECT_FLOAT_EQ(triangles[0].r, 0.25f);
    EXPECT_FLOAT_EQ(triangles[0].g, 0.5f);
    EXPECT_FLOAT_EQ(triangles[0].bColor, 0.75f);
    ASSERT_EQ(IRDebug::getScreenLines().size(), 4u);
    EXPECT_FLOAT_EQ(IRDebug::getScreenLines()[0].a, 0.5f);
}

TEST_F(LuaDebugOverlayBindingsTest, DrawDotScreenDecomposesIntoTwoTriangles) {
    run("IRDebug.drawDotScreen({8, 8}, 3, {1, 1, 0, 1})");

    EXPECT_EQ(IRDebug::getScreenTriangles().size(), 2u);
}

// ---- argument validation ---------------------------------------------------

// The `*FromLua` helpers zero-default by contract, so without a callsite type
// check a typo'd argument would draw silently at the origin instead of failing.
TEST_F(LuaDebugOverlayBindingsTest, NonVectorArgumentRaisesInsteadOfDrawingAtOrigin) {
    EXPECT_TRUE(raises("IRDebug.drawLine3D(42, {4, 5, 6}, 1, 0, 0)"));
    EXPECT_TRUE(IRDebug::getLines().empty());

    EXPECT_TRUE(raises("IRDebug.drawLineScreen('nope', {3, 4}, 1, 0, 0)"));
    EXPECT_TRUE(IRDebug::getScreenLines().empty());

    EXPECT_TRUE(raises("IRDebug.drawDotScreen({1, 2}, 3, 7)"));
    EXPECT_TRUE(IRDebug::getScreenTriangles().empty());
}

// A WRONG-shaped vector userdata is the subtler half of the same contract.
// `is<sol::table>()` is true for userdata, so a table-first shape check admits
// every userdata; the mismatch then reaches `*FromLua`'s table branch and dies
// indexing a metatable-less userdata. It raises either way — so these assert on
// the MESSAGE, which is the part the contract actually promises.
TEST_F(LuaDebugOverlayBindingsTest, WrongVectorUserdataTypeRaisesNamingTheArgument) {
    auto &lua = m_lua.lua();
    lua["v2"] = IRMath::vec2(1.0f, 2.0f);
    lua["v3"] = vec3(1.0f, 2.0f, 3.0f);
    lua["v4"] = vec4(1.0f, 2.0f, 3.0f, 4.0f);

    // vec2 / vec4 where vec3 is expected.
    EXPECT_TRUE(raisesWith(
        "IRDebug.drawLine3D(v2, {4, 5, 6}, 1, 0, 0)",
        "IRDebug.drawLine3D: 'from' must be an IRMath vec3"
    ));
    EXPECT_TRUE(raisesWith(
        "IRDebug.drawLine3D({4, 5, 6}, v4, 1, 0, 0)",
        "IRDebug.drawLine3D: 'to' must be an IRMath vec3"
    ));
    EXPECT_TRUE(IRDebug::getLines().empty());

    // vec3 where vec2 is expected.
    EXPECT_TRUE(raisesWith(
        "IRDebug.drawLineScreen(v3, {3, 4}, 1, 0, 0)",
        "IRDebug.drawLineScreen: 'from' must be an IRMath vec2"
    ));
    EXPECT_TRUE(IRDebug::getScreenLines().empty());

    // vec2 where a vec4 color is expected.
    EXPECT_TRUE(raisesWith(
        "IRDebug.drawDotScreen({1, 2}, 3, v2)",
        "IRDebug.drawDotScreen: 'color' must be an IRMath vec4"
    ));
    EXPECT_TRUE(IRDebug::getScreenTriangles().empty());

    // The matching type still passes, so the check discriminates rather than
    // rejecting all userdata.
    run("IRDebug.drawLineScreen(v2, {3, 4}, 1, 0, 0)");
    EXPECT_EQ(IRDebug::getScreenLines().size(), 1u);
}

TEST_F(LuaDebugOverlayBindingsTest, NonTablePathRaises) {
    EXPECT_TRUE(raises("IRDebug.drawPath3D(42, 1, 1, 1)"));
    EXPECT_TRUE(IRDebug::getLines().empty());
}

TEST_F(LuaDebugOverlayBindingsTest, NonVectorPathEntryRaises) {
    EXPECT_TRUE(raises("IRDebug.drawPath3D({ {0, 0, 0}, 5 }, 1, 1, 1)"));
}

// ---- vec2FromLua / vec4FromLua (new shared helpers) ------------------------

TEST_F(LuaDebugOverlayBindingsTest, Vec2FromLuaIndexedAndKeyed) {
    auto &lua = m_lua.lua();
    const IRMath::vec2 indexed = IRScript::vec2FromLua(lua.create_table_with(1, 3, 2, 4));
    EXPECT_FLOAT_EQ(indexed.x, 3.0f);
    EXPECT_FLOAT_EQ(indexed.y, 4.0f);

    const IRMath::vec2 keyed = IRScript::vec2FromLua(lua.create_table_with("x", 5, "y", 6));
    EXPECT_FLOAT_EQ(keyed.x, 5.0f);
    EXPECT_FLOAT_EQ(keyed.y, 6.0f);
}

TEST_F(LuaDebugOverlayBindingsTest, Vec2FromLuaMissingComponentsZeroDefault) {
    sol::object nilObj = m_lua.lua()["__nonexistent__"];
    const IRMath::vec2 zero = IRScript::vec2FromLua(nilObj);
    EXPECT_FLOAT_EQ(zero.x, 0.0f);
    EXPECT_FLOAT_EQ(zero.y, 0.0f);
}

TEST_F(LuaDebugOverlayBindingsTest, Vec4FromLuaAcceptsXyzwAndRgbaAndIndexed) {
    auto &lua = m_lua.lua();
    const vec4 xyzw = IRScript::vec4FromLua(lua.create_table_with("x", 1, "y", 2, "z", 3, "w", 4));
    EXPECT_FLOAT_EQ(xyzw.x, 1.0f);
    EXPECT_FLOAT_EQ(xyzw.w, 4.0f);

    const vec4 rgba =
        IRScript::vec4FromLua(lua.create_table_with("r", 0.1, "g", 0.2, "b", 0.3, "a", 0.4));
    EXPECT_FLOAT_EQ(rgba.x, 0.1f);
    EXPECT_FLOAT_EQ(rgba.w, 0.4f);

    const vec4 indexed = IRScript::vec4FromLua(lua.create_table_with(1, 9, 2, 8, 3, 7, 4, 6));
    EXPECT_FLOAT_EQ(indexed.x, 9.0f);
    EXPECT_FLOAT_EQ(indexed.w, 6.0f);
}

// Unlike `quatFromLua` (identity-defaults, `w = 1`) this zero-defaults, so an
// omitted alpha is transparent rather than opaque.
TEST_F(LuaDebugOverlayBindingsTest, Vec4FromLuaZeroDefaultsUnlikeQuatFromLua) {
    sol::object nilObj = m_lua.lua()["__nonexistent__"];
    const vec4 zero = IRScript::vec4FromLua(nilObj);
    EXPECT_FLOAT_EQ(zero.w, 0.0f);
    EXPECT_FLOAT_EQ(IRScript::quatFromLua(nilObj).w, 1.0f);
}

} // namespace
