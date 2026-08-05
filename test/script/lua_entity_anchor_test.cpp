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
// Not covered here, deliberately: constructing a C_VoxelSetNew from Lua. That
// ctor allocates from the ACTIVE canvas via the asserting
// `IRPrefab::VoxelPool::activeCanvasEntity()`, so it needs a live
// RenderManager and cannot run in a headless unit test. The placement
// semantics the Lua ctor reaches are covered headlessly in
// test/ecs/voxel_set_anchor_test.cpp (which passes an explicit target canvas),
// and the demo `creations/demos/default/main.lua` is the in-tree Lua spawn.

#include <gtest/gtest.h>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::EntityAnchor;

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

} // namespace
