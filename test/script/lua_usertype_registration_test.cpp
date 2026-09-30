#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/script/lua_script.hpp>

#include <stdexcept>
#include <vector>

// Usertype registration is once per C++ type per Lua state. Under the pinned
// sol2, a second `new_usertype<T>` un-pins the first registration's storage
// userdata; when the GC later finalizes it, its `__gc` nils the registry
// metatable names the second registration installed, and every `T` pushed to
// Lua from then on gets a bare metatable with no `__index` — `h.entity` raises
// "attempt to index a userdata value". Nothing breaks until a collection runs,
// so every member read below follows a forced full GC; without it a double
// registration reads fine and the test is vacuous.

namespace {

struct PushedFirst {
    PushedFirst(int value)
        : value_{value} {}
    int value_;
};

struct RawFirst {
    RawFirst(int value)
        : value_{value} {}
    int value_;
};

constexpr const char *kFullCollect = "collectgarbage('collect'); collectgarbage('collect')";

void bindHandleFactories(sol::state &lua) {
    lua["makeHandle"] = [](IREntity::EntityId id) { return IRScript::LuaEntity{id}; };
    lua["makeHandles"] = []() {
        return std::vector<IRScript::LuaEntity>{IRScript::LuaEntity{7}, IRScript::LuaEntity{9}};
    };
}

template <typename T> T runLua(sol::state &lua, const char *chunk) {
    sol::protected_function_result result = lua.safe_script(chunk, &sol::script_pass_on_error);
    EXPECT_TRUE(result.valid()) << chunk;
    if (!result.valid()) {
        return T{};
    }
    return result.get<T>();
}

TEST(LuaUsertypeRegistrationTest, BareLuaScriptRegistersLuaEntity) {
    IRScript::LuaScript lua;
    EXPECT_EQ(
        runLua<double>(lua.lua(), "collectgarbage('collect'); return LuaEntity.new(7).entity"),
        7.0
    );
}

TEST(LuaUsertypeRegistrationTest, LuaDrivenEcsKeepsLuaEntityMembersAcrossGc) {
    IRScript::LuaScript lua;
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    lua.bindLuaDrivenEcs();
    lua.lua().safe_script(kFullCollect);

    EXPECT_EQ(runLua<double>(lua.lua(), "return LuaEntity.new(7).entity"), 7.0);
    EXPECT_TRUE(runLua<bool>(lua.lua(), "return getmetatable(LuaEntity.new(7)).__index ~= nil"));
}

// Positive control: pins the sol2 behaviour the guard exists for. If a sol2
// bump makes this pass-through read succeed, the "Usertype ownership" rule
// of engine/script/CLAUDE.md is stale.
TEST(LuaUsertypeRegistrationTest, RawDoubleNewUsertypeStripsMembersAtNextGc) {
    sol::state lua;
    lua.open_libraries(sol::lib::base);
    for (int pass = 0; pass < 2; ++pass) {
        lua.new_usertype<IRScript::LuaEntity>(
            "LuaEntity",
            sol::constructors<IRScript::LuaEntity(IREntity::EntityId)>(),
            "entity",
            &IRScript::LuaEntity::entity
        );
    }
    bindHandleFactories(lua);
    lua.safe_script(kFullCollect);

    EXPECT_FALSE(
        runLua<bool>(
            lua,
            "local h = makeHandle(42); return (pcall(function() return h.entity end))"
        )
    );
}

// The refused call returns the existing registration, so a caller chaining
// members onto the result still extends the one live usertype.
TEST(LuaUsertypeRegistrationTest, RepeatRegisterTypeIsRefusedAndMembersSurviveGc) {
    IRScript::LuaScript lua;
    bindHandleFactories(lua.lua());

    sol::usertype<IRScript::LuaEntity> existing =
        lua.registerType<IRScript::LuaEntity, IRScript::LuaEntity(IREntity::EntityId)>(
            "LuaEntity",
            "entity",
            &IRScript::LuaEntity::entity
        );
    existing["answer"] = []() { return 3; };
    lua.lua().safe_script(kFullCollect);

    EXPECT_EQ(runLua<int>(lua.lua(), "return LuaEntity.answer()"), 3);

    EXPECT_EQ(runLua<double>(lua.lua(), "return makeHandle(42).entity"), 42.0);
    EXPECT_EQ(runLua<double>(lua.lua(), "return makeHandles()[1].entity"), 7.0);
    EXPECT_EQ(runLua<double>(lua.lua(), "return makeHandles()[2].entity"), 9.0);
}

// The guard keys on the C++ type, not the Lua name: a repeat under another
// name strips the same metatables, so it is refused too, and none of its
// members or its global are applied.
TEST(LuaUsertypeRegistrationTest, RepeatUnderAnotherNameIsRefused) {
    IRScript::LuaScript lua;
    bindHandleFactories(lua.lua());

    lua.registerType<IRScript::LuaEntity, IRScript::LuaEntity(IREntity::EntityId)>(
        "LuaEntityAlias",
        "id",
        &IRScript::LuaEntity::entity
    );
    lua.lua().safe_script(kFullCollect);

    EXPECT_TRUE(runLua<bool>(lua.lua(), "return LuaEntityAlias == nil"));
    EXPECT_EQ(runLua<double>(lua.lua(), "return makeHandle(42).entity"), 42.0);
}

// Pushing a not-yet-registered type makes sol2 create a bare registry
// metatable under the type's name. That is not a registration, so the first
// `registerType` afterwards must go through.
TEST(LuaUsertypeRegistrationTest, PushBeforeRegisterIsNotARepeat) {
    IRScript::LuaScript lua;
    lua.lua()["makePushed"] = []() { return PushedFirst{5}; };
    lua.lua().safe_script("local early = makePushed()");

    EXPECT_NO_THROW((lua.registerType<PushedFirst, PushedFirst(int)>(
        "PushedFirst",
        "value",
        &PushedFirst::value_
    )));
    lua.lua().safe_script(kFullCollect);

    EXPECT_EQ(runLua<int>(lua.lua(), "return makePushed().value"), 5);
    EXPECT_EQ(runLua<int>(lua.lua(), "return PushedFirst.new(9).value"), 9);
}

#ifndef IR_RELEASE
TEST(LuaUsertypeRegistrationTest, RegisterTypeAfterRawNewUsertypeAsserts) {
    IRScript::LuaScript lua;
    lua.lua().new_usertype<RawFirst>(
        "RawFirst",
        sol::constructors<RawFirst(int)>(),
        "value",
        &RawFirst::value_
    );

    EXPECT_THROW(
        (lua.registerType<RawFirst, RawFirst(int)>("RawFirst", "value", &RawFirst::value_)),
        std::runtime_error
    );
}
#endif

} // namespace
