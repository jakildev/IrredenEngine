// Lua surface for EntityAnchor (#2563): `IRComponent.EntityAnchor.{CORNER,
// CENTER,GROUND}`, minted in LuaScript::bindLuaDrivenEcs alongside
// IRComponent.RotationMode per `.claude/rules/cpp-lua-enums.md` (integers, no
// string names).
//
// What this file is actually guarding is DRIFT. The binding uses a stringized
// macro so the Lua key can't diverge from the C++ identifier, but nothing
// stops the C++ ordinals from being reordered while Lua callers keep spelling
// the same name — a renumber would silently re-point every existing
// `IRComponent.EntityAnchor.GROUND` at a different mode. So every assertion
// below compares against `static_cast<lua_Integer>(EntityAnchor::X)` rather
// than a literal: the test tracks the enum instead of freezing a number.
//
// The second half of this file drives the other Lua-side surface the anchor
// added: the `C_VoxelSetNew.new(size, color, anchor, targetCanvas)` ctor. The
// 2- and 3-arg forms allocate from the ACTIVE canvas via the asserting
// `IRPrefab::VoxelPool::activeCanvasEntity()` and so need a live
// RenderManager, but the 4-arg form takes the canvas explicitly, which is what
// lets a headless fixture create its own `C_VoxelPool` entity and assert the
// placement a Lua caller actually gets. Asserting the ORDINALS alone would
// leave the binding untested where it matters: the failure mode the
// anchor-only Lua surface exists to prevent (a value silently binding the
// wrong ctor arm) is invisible in the enum table and only observable in a
// constructed set's baked positions.

#include <gtest/gtest.h>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_set_lua.hpp>

#include <string>

namespace {

using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

constexpr float kEps = 1e-5f;

// LuaScript first so its sol::state outlives sol::function-bearing columns
// held by EntityManager (mirrors lua_enum_register_test.cpp).
class LuaEntityAnchorTest : public testing::Test {
  protected:
    LuaEntityAnchorTest()
        : m_lua{}
        , m_entity_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;

    // The math usertypes are registered by each creation's own binding pass
    // (`creations/demos/default/lua_bindings.cpp`), not by `bindLuaDrivenEcs`,
    // so the ctor arms below need them minted here in the same shape.
    void registerVoxelSetSurface() {
        m_lua.registerType<Color, Color(int, int, int, int)>("Color");
        m_lua.registerType<ivec3, ivec3(int, int, int)>("ivec3");
        m_lua.registerTypeFromTraits<C_VoxelSetNew>();
    }

    static IREntity::EntityId makeCanvas() {
        return IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)});
    }

    static vec3 localPos(const C_VoxelSetNew &set, ivec3 cell) {
        return set.positions_[IRMath::index3DtoIndex1D(cell, set.size_)].pos_;
    }

    static void expectVec3Near(vec3 actual, vec3 expected, const char *what) {
        EXPECT_NEAR(actual.x, expected.x, kEps) << what << " .x";
        EXPECT_NEAR(actual.y, expected.y, kEps) << what << " .y";
        EXPECT_NEAR(actual.z, expected.z, kEps) << what << " .z";
    }
};

TEST_F(LuaEntityAnchorTest, TableExposesEveryModeAtTheCppOrdinal) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return IRComponent.EntityAnchor.CORNER, "
        "       IRComponent.EntityAnchor.CENTER, "
        "       IRComponent.EntityAnchor.GROUND"
    );
    ASSERT_TRUE(result.valid());

    std::tuple<lua_Integer, lua_Integer, lua_Integer> values = result;
    EXPECT_EQ(std::get<0>(values), static_cast<lua_Integer>(EntityAnchor::CORNER));
    EXPECT_EQ(std::get<1>(values), static_cast<lua_Integer>(EntityAnchor::CENTER));
    EXPECT_EQ(std::get<2>(values), static_cast<lua_Integer>(EntityAnchor::GROUND));
}

TEST_F(LuaEntityAnchorTest, ModesAreDistinctIntegers) {
    // Guards the failure the ordinal check above cannot see on its own: if the
    // C++ enum collapsed two modes to the same value, each assertion there
    // would still pass while the Lua surface silently lost a mode.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local A = IRComponent.EntityAnchor\n"
        "return type(A.CORNER) == 'number' and A.CORNER ~= A.CENTER and "
        "       A.CENTER ~= A.GROUND and A.CORNER ~= A.GROUND"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaEntityAnchorTest, RangeSentinelsBracketEveryExposedMode) {
    // `kFirst`/`kLast` are what binding-layer range checks validate against
    // (`.claude/rules/cpp-lua-enums.md`), so a mode exposed to Lua that falls
    // outside them would be rejected by every validator despite being a legal
    // spelling. Adding a mode without bumping kLast is the drift this catches.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local A = IRComponent.EntityAnchor\n"
        "return math.min(A.CORNER, A.CENTER, A.GROUND), "
        "       math.max(A.CORNER, A.CENTER, A.GROUND)"
    );
    ASSERT_TRUE(result.valid());

    std::tuple<lua_Integer, lua_Integer> bounds = result;
    EXPECT_EQ(std::get<0>(bounds), static_cast<lua_Integer>(EntityAnchor::kFirst));
    EXPECT_EQ(std::get<1>(bounds), static_cast<lua_Integer>(EntityAnchor::kLast));
}

TEST_F(LuaEntityAnchorTest, StringNamesAreNotAValidSpelling) {
    // The rule this binding exists to satisfy is "no string-name lookups", so
    // a typo'd or string-typed spelling must come back nil rather than
    // resolving — that is what moves the typo class up to Lua-eval time.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return IRComponent.EntityAnchor.GROUNDD == nil and "
        "       IRComponent.EntityAnchor['ground'] == nil"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaEntityAnchorTest, GroundSpawnFromLuaBakesCenterXyBottomZ) {
    // Acceptance criterion 2's primary arm: spawn a GROUND set THROUGH the Lua
    // ctor and assert the placement, rather than inferring it from the C++
    // suite. The two are different surfaces — the C++ arm proves the offset
    // math, this one proves a Lua integer reaches `EntityAnchor` intact
    // through sol2's conversion, which is the only mechanism unique to the
    // Lua path.
    registerVoxelSetSurface();
    const IREntity::EntityId canvas = makeCanvas();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = canvas;

    auto result = lua.safe_script(
        "return C_VoxelSetNew.new(ivec3.new(2, 2, 2), Color.new(200, 100, 50, 255), "
        "                         IRComponent.EntityAnchor.GROUND, testCanvas)"
    );
    ASSERT_TRUE(result.valid());
    ASSERT_EQ(result.get_type(), sol::type::userdata);

    const C_VoxelSetNew &set = result.get<const C_VoxelSetNew &>();
    ASSERT_EQ(set.numVoxels_, 8);
    EXPECT_EQ(set.anchor_, EntityAnchor::GROUND);
    // Same expected positions as test/ecs/voxel_set_anchor_test.cpp's C++ arm:
    // the ground-contact face lands at local z == 0, i.e. at the translation.
    expectVec3Near(localPos(set, ivec3(0, 0, 0)), vec3(-0.5f, -0.5f, -1.5f), "GROUND cell(0,0,0)");
    expectVec3Near(localPos(set, ivec3(1, 1, 1)), vec3(0.5f, 0.5f, -0.5f), "GROUND cell(1,1,1)");
}

TEST_F(LuaEntityAnchorTest, EveryAnchorSpellingRoutesToItsOwnOffsetFromLua) {
    // GROUND alone cannot show that the argument is READ — a ctor that ignored
    // its anchor and hard-coded GROUND would pass the arm above. Driving all
    // three spellings through the same call shape and requiring three
    // different origins is what makes the routing claim non-vacuous.
    registerVoxelSetSurface();
    auto &lua = m_lua.lua();

    struct Arm {
        const char *spelling_;
        EntityAnchor anchor_;
        vec3 expectedOrigin_;
    };
    const Arm arms[] = {
        {"CORNER", EntityAnchor::CORNER, vec3(0.0f, 0.0f, 0.0f)},
        {"CENTER", EntityAnchor::CENTER, vec3(-0.5f, -0.5f, -0.5f)},
        {"GROUND", EntityAnchor::GROUND, vec3(-0.5f, -0.5f, -1.5f)},
    };

    for (const Arm &arm : arms) {
        lua["testCanvas"] = makeCanvas();
        const std::string script =
            std::string(
                "return C_VoxelSetNew.new(ivec3.new(2, 2, 2), Color.new(10, 20, 30, 255), "
                "                         IRComponent.EntityAnchor."
            ) +
            arm.spelling_ + ", testCanvas)";

        auto result = lua.safe_script(script);
        ASSERT_TRUE(result.valid()) << arm.spelling_;

        const C_VoxelSetNew &set = result.get<const C_VoxelSetNew &>();
        EXPECT_EQ(set.anchor_, arm.anchor_) << arm.spelling_;
        expectVec3Near(localPos(set, ivec3(0, 0, 0)), arm.expectedOrigin_, arm.spelling_);
    }
}

TEST_F(LuaEntityAnchorTest, LegacyBoolArmIsRejectedRatherThanSilentlyBoundAsAnAnchor) {
    // `component_voxel_set_lua.hpp` deliberately does NOT register the legacy
    // `bool centerAroundOrigin` ctor: a Lua boolean and a Lua integer in one
    // sol2 overload set would resolve by declaration order, and picking wrong
    // is silent because `false`/`true` coincide with CORNER/CENTER. This pins
    // the consequence — a boolean must FAIL to construct.
    //
    // The integer arm below is the positive control. Without it a `pcall`
    // returning false proves nothing: a typo'd global, an unregistered
    // usertype, or a dead canvas would fail identically, and the test would
    // pass while never reaching the overload set it claims to be about.
    registerVoxelSetSurface();
    auto &lua = m_lua.lua();
    lua["testCanvas"] = makeCanvas();

    auto control = lua.safe_script(
        "return pcall(C_VoxelSetNew.new, ivec3.new(2, 2, 2), Color.new(10, 20, 30, 255), "
        "             IRComponent.EntityAnchor.CENTER, testCanvas)"
    );
    ASSERT_TRUE(control.valid());
    ASSERT_TRUE(control.get<bool>()) << "control: the integer-anchor arm must construct, "
                                        "otherwise the rejection below is vacuous";

    lua["testCanvas"] = makeCanvas();
    auto boolArm = lua.safe_script(
        "return pcall(C_VoxelSetNew.new, ivec3.new(2, 2, 2), Color.new(10, 20, 30, 255), "
        "             true, testCanvas)"
    );
    ASSERT_TRUE(boolArm.valid());
    EXPECT_FALSE(boolArm.get<bool>())
        << "a Lua boolean bound an anchor arm — the legacy bool form is reachable after all, "
           "which is the silent CORNER/CENTER mis-binding the anchor-only surface prevents";
}

} // namespace
