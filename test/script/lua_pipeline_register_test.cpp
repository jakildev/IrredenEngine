#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_3d_lua.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_script.hpp>
#include <irreden/update/systems/system_lifetime.hpp>

namespace {

using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::FieldBindingId;
using IRComponents::kInvalidFieldId;
using IRComponents::TransformKind;
using IREntity::EntityId;

// Owns the EntityManager + SystemManager + LuaScript needed to exercise
// the T-102 Lua bindings end-to-end. Order matters — Lua state must be
// destroyed last so any captured sol::function references in the
// SystemManager's dynamic-system bodies finish lua_unref while the
// lua_State is still open. See modifier_lua_test.cpp for the same
// ordering rationale.
class LuaPipelineRegisterTest : public testing::Test {
  protected:
    LuaPipelineRegisterTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// ---- IRTime / IRSystem.SystemName enum ------------------------------------

TEST_F(LuaPipelineRegisterTest, IRTimeEventsBound) {
    auto &lua = m_lua.lua();
    EXPECT_EQ(lua.script("return IRTime.UPDATE").get<int>(), static_cast<int>(IRTime::UPDATE));
    EXPECT_EQ(lua.script("return IRTime.RENDER").get<int>(), static_cast<int>(IRTime::RENDER));
    EXPECT_EQ(lua.script("return IRTime.INPUT").get<int>(), static_cast<int>(IRTime::INPUT));
    EXPECT_EQ(lua.script("return IRTime.START").get<int>(), static_cast<int>(IRTime::START));
    EXPECT_EQ(lua.script("return IRTime.END").get<int>(), static_cast<int>(IRTime::END));
}

TEST_F(LuaPipelineRegisterTest, SystemNameEnumBound) {
    auto &lua = m_lua.lua();
    EXPECT_EQ(
        lua.script("return IRSystem.SystemName.LIFETIME").get<int>(),
        static_cast<int>(IRSystem::LIFETIME)
    );
    EXPECT_EQ(
        lua.script("return IRSystem.SystemName.MODIFIER_DECAY").get<int>(),
        static_cast<int>(IRSystem::MODIFIER_DECAY)
    );
    EXPECT_EQ(
        lua.script("return IRSystem.SystemName.FRAMEBUFFER_TO_SCREEN").get<int>(),
        static_cast<int>(IRSystem::FRAMEBUFFER_TO_SCREEN)
    );
}

// ---- IRComponent.C_Name handle (C++ component handle exposure) -----------

TEST_F(LuaPipelineRegisterTest, CppComponentHandleAvailableAfterRegisterType) {
    m_lua.registerTypeFromTraits<IRComponents::C_Position3D>();
    auto &lua = m_lua.lua();
    // Handle table should exist with a non-zero componentId.
    auto tableResult = lua.safe_script(
        "return type(IRComponent.C_Position3D) == 'table'",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(tableResult.valid()) << tableResult.get<sol::error>().what();
    EXPECT_TRUE(tableResult.get<bool>());

    auto idResult = lua.safe_script(
        "return (IRComponent.C_Position3D.componentId or 0) ~= 0",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(idResult.valid());
    EXPECT_TRUE(idResult.get<bool>());

    // The handle should be usable directly in a components list.
    auto sysResult = lua.safe_script(
        R"(
        local sysId = IRSystem.registerSystem({
            name = "HandleTestSys",
            components = { IRComponent.C_Position3D },
            tick = function(arch) end,
        })
        return type(sysId) == "number"
    )",
        sol::script_pass_on_error
    );
    if (!sysResult.valid()) {
        sol::error err = sysResult;
        FAIL() << err.what();
    }
    EXPECT_TRUE(sysResult.get<bool>());
}

// ---- registerPrefabSystem + IRSystem.systemId -----------------------------

TEST_F(LuaPipelineRegisterTest, SystemIdReturnsCachedPrefabSystemId) {
    const IRSystem::SystemId expected = m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    auto &lua = m_lua.lua();
    const auto actual =
        lua.script("return IRSystem.systemId(IRSystem.SystemName.LIFETIME)").get<lua_Integer>();
    EXPECT_EQ(static_cast<IRSystem::SystemId>(actual), expected);
}

TEST_F(LuaPipelineRegisterTest, SystemIdRaisesForUnregisteredName) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return IRSystem.systemId(IRSystem.SystemName.LIFETIME)",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaPipelineRegisterTest, RegisterPrefabSystemIsIdempotent) {
    const auto first = m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    const auto second = m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    EXPECT_EQ(first, second);
}

// ---- registerPipeline: mixed prefab + Lua system ---------------------------

TEST_F(LuaPipelineRegisterTest, RegisterPipelineAcceptsMixedSystemIds) {
    m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    auto &lua = m_lua.lua();
    // Use a Lua-defined component so the test fixture doesn't need a
    // creation-style lua_component_pack with `C_Position3D` registered.
    auto result = lua.safe_script(
        R"(
        local Marker = IRComponent.register("PipelineMarker", { count = 0 })
        local luaSysId = IRSystem.registerSystem({
            name = "PipelineTestLuaSys",
            components = { Marker },
            tick = function(arch) end,
        })
        IRSystem.registerPipeline(IRTime.UPDATE, {
            IRSystem.systemId(IRSystem.SystemName.LIFETIME),
            luaSysId,
        })
        return true
    )",
        sol::script_pass_on_error
    );
    // Expectation: pipeline registration succeeds; we don't assert a
    // particular execution order here because the systemmanager-side
    // execution test would require driving the game loop. The end-to-end
    // demo (creations/demos/lua_pipeline_demo) covers loop integration.
    if (!result.valid()) {
        sol::error err = result;
        FAIL() << err.what();
    } else {
        EXPECT_TRUE(result.get<bool>());
    }
}

// ---- IRModifier.Transform enum --------------------------------------------

TEST_F(LuaPipelineRegisterTest, ModifierTransformEnumBound) {
    auto &lua = m_lua.lua();
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.ADD").get<int>(),
        static_cast<int>(TransformKind::ADD)
    );
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.MULTIPLY").get<int>(),
        static_cast<int>(TransformKind::MULTIPLY)
    );
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.SET").get<int>(),
        static_cast<int>(TransformKind::SET)
    );
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.CLAMP_MIN").get<int>(),
        static_cast<int>(TransformKind::CLAMP_MIN)
    );
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.CLAMP_MAX").get<int>(),
        static_cast<int>(TransformKind::CLAMP_MAX)
    );
    EXPECT_EQ(
        lua.script("return IRModifier.Transform.OVERRIDE").get<int>(),
        static_cast<int>(TransformKind::OVERRIDE)
    );
}

// ---- registerField + fieldId ----------------------------------------------

TEST_F(LuaPipelineRegisterTest, RegisterFieldRoundTripsByName) {
    auto &lua = m_lua.lua();
    auto registered =
        lua.script("return IRModifier.registerField('lua.pipeline.test.alpha')").get<lua_Integer>();
    EXPECT_GT(registered, 0);

    auto looked =
        lua.script("return IRModifier.fieldId('lua.pipeline.test.alpha')").get<lua_Integer>();
    EXPECT_EQ(looked, registered);

    auto missing = lua.script("return IRModifier.fieldId('lua.pipeline.test.never.registered')")
                       .get<lua_Integer>();
    EXPECT_EQ(missing, static_cast<lua_Integer>(kInvalidFieldId));
}

// ---- IRModifier.add against a Lua-typed component (the plan's claim) ------

TEST_F(LuaPipelineRegisterTest, AddMultiplyHalvesResolvedFieldFromLua) {
    // Use a test-unique component name so the global field registry
    // (process-lifetime singleton; each `IRComponent.register` call
    // appends to it without dedup) doesn't accumulate duplicate
    // "Hp.current" entries across the test suite. The plan's
    // verification example uses `Hp.current`; this test runs the same
    // mechanism under `T102Hp.current` to keep cross-test isolation.
    auto &lua = m_lua.lua();
    auto setup = lua.safe_script(
        R"(
        local Hp = IRComponent.register("T102Hp", { current = 100 })
        return Hp.fields.current.bindingId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << "T102Hp registration failed";
    const auto fieldId = static_cast<FieldBindingId>(setup.get<lua_Integer>());
    EXPECT_NE(fieldId, kInvalidFieldId);

    const EntityId entity = IREntity::createEntity(C_Modifiers{});
    lua["g_entity"] = static_cast<lua_Integer>(entity);

    // Lua applies the canonical `IRModifier.add(entity, "T102Hp.current",
    // { transform = MULTIPLY, value = 0.5 })` (plan's verification with
    // a test-unique field name).
    auto add = lua.safe_script(
        R"(
        IRModifier.add(g_entity, "T102Hp.current", {
            transform = IRModifier.Transform.MULTIPLY,
            value = 0.5,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(add.valid()) << "IRModifier.add failed";

    // applyToField shares the resolver evaluator with the pipeline; if
    // base 100 composes to 50 here, C_ResolvedFields would carry the
    // same value after the resolver tick (the plan's "halved value"
    // claim). Verifying the composition without driving the loop keeps
    // this an isolated unit test; the demo creation covers the loop
    // integration.
    const float resolved = IRPrefab::Modifier::applyToField(entity, fieldId, 100.0f);
    EXPECT_FLOAT_EQ(resolved, 50.0f);

    // The Lua-side `IRModifier.applyToField` mirrors the C++ surface and
    // should report the same composed value.
    auto fromLua = lua.script("return IRModifier.applyToField(g_entity, 'T102Hp.current', 100.0)")
                       .get<float>();
    EXPECT_FLOAT_EQ(fromLua, 50.0f);
}

TEST_F(LuaPipelineRegisterTest, AddRejectsUnknownFieldName) {
    auto &lua = m_lua.lua();
    const EntityId entity = IREntity::createEntity(C_Modifiers{});
    lua["g_entity"] = static_cast<lua_Integer>(entity);
    auto result = lua.safe_script(
        R"(
        IRModifier.add(g_entity, "no.such.field", {
            transform = IRModifier.Transform.ADD,
            value = 1.0,
        })
    )",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
}

} // namespace
