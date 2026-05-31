// #1403 EVAL coverage: the runtime `IREnum.register` surface bound in
// LuaScript::bindLuaDrivenEcs — Lua-defined closed enums, the Lua-native
// counterpart to the C++ registerEnum stopgap. The CODEGEN side (the
// build-time stub sharing detail::buildLuaEnumTable) is covered in
// lua_component_codegen_test.cpp, which proves an enum member resolves as a
// component-field default during a real codegen invocation.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

#include <string>
#include <tuple>

namespace {

// LuaScript first so its sol::state outlives sol::function-bearing columns
// held by EntityManager (mirrors lua_component_register_test.cpp).
class LuaEnumTest : public testing::Test {
  protected:
    LuaEnumTest()
        : m_lua{}
        , m_entity_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

// ---- Happy path ------------------------------------------------------------

TEST_F(LuaEnumTest, MembersResolveToZeroBasedOrdinals) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local E = IREnum.register('DeviceType', { 'EFFECT', 'SYNTH', 'CONTROLLER' })\n"
        "return E.EFFECT, E.SYNTH, E.CONTROLLER"
    );
    ASSERT_TRUE(result.valid());
    auto [effect, synth, controller] =
        result.get<std::tuple<lua_Integer, lua_Integer, lua_Integer>>();
    EXPECT_EQ(effect, 0);
    EXPECT_EQ(synth, 1);
    EXPECT_EQ(controller, 2);
}

TEST_F(LuaEnumTest, RegisteredEnumIsAccessibleByNameOnIREnumTable) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "IREnum.register('Tag', { 'RED', 'GREEN', 'BLUE' })\n"
        "return IREnum.Tag.GREEN"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<lua_Integer>(), 1);
}

TEST_F(LuaEnumTest, ReturnedHandleAndGlobalEntryAreTheSameTable) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local E = IREnum.register('Same', { 'A', 'B' })\n"
        "return E == IREnum.Same"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(result.get<bool>());
}

TEST_F(LuaEnumTest, EnumMemberUsableAsComponentFieldDefault) {
    // An ordinal is a plain integer, so it infers as int32 exactly like a
    // numeric literal default would — the enum is "usable wherever a
    // registerEnum enum is today" (acceptance criterion).
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local Kind = IREnum.register('Kind', { 'NONE', 'PLAYER', 'ENEMY' })\n"
        "local C = IRComponent.register('Actor', { kind = Kind.ENEMY })\n"
        "return Kind.ENEMY, C.fields.kind.type"
    );
    ASSERT_TRUE(result.valid());
    auto [ordinal, fieldType] = result.get<std::tuple<lua_Integer, std::string>>();
    EXPECT_EQ(ordinal, 2);
    EXPECT_EQ(fieldType, "int32");
}

TEST_F(LuaEnumTest, TypoAccessYieldsNil) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local E = IREnum.register('TypoCheck', { 'ALPHA', 'BETA' })\n"
        "return E.GAMMA == nil"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_TRUE(result.get<bool>());
}

// ---- Validation (errors at registration, not silent) -----------------------

TEST_F(LuaEnumTest, DuplicateEnumNameRaises) {
    auto &lua = m_lua.lua();
    auto first = lua.safe_script("IREnum.register('Dup', { 'A' })", sol::script_pass_on_error);
    ASSERT_TRUE(first.valid());
    auto second = lua.safe_script("IREnum.register('Dup', { 'B' })", sol::script_pass_on_error);
    EXPECT_FALSE(second.valid());
}

TEST_F(LuaEnumTest, NonStringMemberRaises) {
    auto &lua = m_lua.lua();
    auto result =
        lua.safe_script("IREnum.register('Bad', { 'A', 42, 'C' })", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaEnumTest, EmptyEnumNameRaises) {
    auto &lua = m_lua.lua();
    EXPECT_FALSE(lua.safe_script("IREnum.register('', { 'A' })", sol::script_pass_on_error).valid());
}

TEST_F(LuaEnumTest, EmptyMemberListRaises) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script("IREnum.register('Empty', {})", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaEnumTest, DuplicateMemberRaises) {
    auto &lua = m_lua.lua();
    auto result =
        lua.safe_script("IREnum.register('DupMem', { 'A', 'B', 'A' })", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaEnumTest, EmptyStringMemberRaises) {
    auto &lua = m_lua.lua();
    auto result =
        lua.safe_script("IREnum.register('EmptyMem', { 'A', '' })", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaEnumTest, ReservedRegisterNameRaises) {
    auto &lua = m_lua.lua();
    auto result =
        lua.safe_script("IREnum.register('register', { 'A' })", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
    // The register function must survive the rejected call.
    auto after =
        lua.safe_script("return type(IREnum.register)", sol::script_pass_on_error);
    ASSERT_TRUE(after.valid());
    EXPECT_EQ(after.get<std::string>(), "function");
}

TEST_F(LuaEnumTest, FailedRegistrationDoesNotReserveTheName) {
    // A registration that throws during validation must not leave the name
    // marked as taken — a corrected re-registration should succeed.
    auto &lua = m_lua.lua();
    auto bad =
        lua.safe_script("IREnum.register('Retry', { 'A', 7 })", sol::script_pass_on_error);
    ASSERT_FALSE(bad.valid());
    auto good =
        lua.safe_script("return IREnum.register('Retry', { 'A', 'B' }).B", sol::script_pass_on_error);
    ASSERT_TRUE(good.valid());
    EXPECT_EQ(good.get<lua_Integer>(), 1);
}

} // namespace
