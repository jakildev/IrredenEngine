#include <gtest/gtest.h>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_local_transform_lua.hpp>
#include <irreden/common/components/component_modifiers.hpp>
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
    m_lua.registerTypeFromTraits<IRComponents::C_LocalTransform>();
    auto &lua = m_lua.lua();
    // Handle table should exist with a non-zero componentId.
    auto tableResult = lua.safe_script(
        "return type(IRComponent.C_LocalTransform) == 'table'",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(tableResult.valid()) << tableResult.get<sol::error>().what();
    EXPECT_TRUE(tableResult.get<bool>());

    auto idResult = lua.safe_script(
        "return (IRComponent.C_LocalTransform.componentId or 0) ~= 0",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(idResult.valid());
    EXPECT_TRUE(idResult.get<bool>());

    // The handle should be usable directly in a components list.
    auto sysResult = lua.safe_script(
        R"(
        local sysId = IRSystem.registerSystem({
            name = "HandleTestSys",
            components = { IRComponent.C_LocalTransform },
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
    // creation-style lua_component_pack with `C_LocalTransform` registered.
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

// ---- IRModifier vec3 surface ----------------------------------------------

TEST_F(LuaPipelineRegisterTest, AddVec3ComposesPerAxisFromLua) {
    auto &lua = m_lua.lua();
    const EntityId entity = IREntity::createEntity(C_Modifiers{});
    lua["g_entity"] = static_cast<lua_Integer>(entity);

    auto setup = lua.safe_script(
        R"(
        local field = IRModifier.registerFieldVec3("lua.test.bob_offset")
        IRModifier.addVec3(g_entity, field, {
            transform = IRModifier.Transform.ADD,
            value = { x = 1.0, y = 2.0, z = 3.0 },
        })
        return field
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << "addVec3 setup failed";
    const auto fieldId = static_cast<FieldBindingId>(setup.get<lua_Integer>());

    const IRMath::vec3 base{10.0f, 10.0f, 10.0f};
    const IRMath::vec3 resolved = IRPrefab::Modifier::applyToFieldVec3(entity, fieldId, base);
    EXPECT_FLOAT_EQ(resolved.x, 11.0f);
    EXPECT_FLOAT_EQ(resolved.y, 12.0f);
    EXPECT_FLOAT_EQ(resolved.z, 13.0f);
}

TEST_F(LuaPipelineRegisterTest, AddVec3AgainstScalarFieldIsNoOp) {
    auto &lua = m_lua.lua();
    const EntityId entity = IREntity::createEntity(C_Modifiers{});
    lua["g_entity"] = static_cast<lua_Integer>(entity);

    auto result = lua.safe_script(
        R"(
        local scalarField = IRModifier.registerField("lua.test.cross_typed_scalar")
        IRModifier.addVec3(g_entity, scalarField, {
            transform = IRModifier.Transform.ADD,
            value = { x = 1.0, y = 2.0, z = 3.0 },
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << "addVec3 raised on scalar-typed field";

    // Neither vector should have an entry — the wrong-type push was a no-op.
    const auto &c = IREntity::getComponent<C_Modifiers>(entity);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
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

// ---- #1540: IRSystem.appendSystem / insertSystemBefore/After --------------
//
// The gap these close: a runtime whose C++ pipeline is built before
// main.lua runs (the midi runtime) had no way for Lua to add a system
// without `registerPipeline` REPLACING — and double-creating — the
// already-registered C++ systems. appendSystem composes onto the live
// pipeline instead.

TEST_F(LuaPipelineRegisterTest, AppendSystemComposesOntoPreBuiltPipeline) {
    // A C++ prefab system is registered into UPDATE *before* the script
    // runs (simulating initSystems()); a Lua-authored system then joins
    // via IRSystem.appendSystem. The C++ system must survive.
    const IRSystem::SystemId lifetime = m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    auto &lua = m_lua.lua();

    auto result = lua.safe_script(
        R"(
        -- "Pre-built C++ pipeline" (one prefab system).
        IRSystem.registerPipeline(IRTime.UPDATE, {
            IRSystem.systemId(IRSystem.SystemName.LIFETIME),
        })
        -- Lua-authored system appended afterward.
        local Marker = IRComponent.register("AppendMarker", { dummy = 0 })
        local luaSys = IRSystem.registerSystem({
            name = "AppendedLuaSys",
            components = { Marker },
            tick = function(arch) end,
        })
        IRSystem.appendSystem(IRTime.UPDATE, luaSys)
        return luaSys
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto luaSys = static_cast<IRSystem::SystemId>(result.get<lua_Integer>());

    // The pre-built C++ system survived AND the Lua system was appended.
    const auto &groups = m_system_manager.getPipelineGroups(IRTime::Events::UPDATE);
    ASSERT_EQ(groups.size(), 2u);
    ASSERT_EQ(groups[0].size(), 1u);
    ASSERT_EQ(groups[1].size(), 1u);
    EXPECT_EQ(groups[0][0], lifetime);
    EXPECT_EQ(groups[1][0], luaSys);
}

TEST_F(LuaPipelineRegisterTest, AppendedLuaSystemTicksAlongsidePreBuiltPipeline) {
    // Prove the appended Lua system actually RUNS in the live pipeline —
    // not just that the group structure looks right. A Lua global is
    // bumped once per archetype tick of the appended system.
    m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    auto &lua = m_lua.lua();
    lua["g_appendTickCount"] = 0;

    auto setup = lua.safe_script(
        R"(
        Marker = IRComponent.register("AppendTickMarker", { dummy = 0 })
        IRSystem.registerPipeline(IRTime.UPDATE, {
            IRSystem.systemId(IRSystem.SystemName.LIFETIME),
        })
        local sys = IRSystem.registerSystem({
            name = "AppendTickSys",
            components = { Marker },
            tick = function(arch)
                g_appendTickCount = g_appendTickCount + 1
            end,
        })
        IRSystem.appendSystem(IRTime.UPDATE, sys)
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(setup.valid()) << sol::error{setup}.what();

    // An entity in the Marker archetype so the appended system body fires.
    const EntityId entity = IREntity::createEntity(C_Modifiers{});
    lua["g_entity"] = static_cast<lua_Integer>(entity);
    auto attach = lua.safe_script(
        "IREntity.addLuaComponent(LuaEntity.new(g_entity), Marker)",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(attach.valid()) << sol::error{attach}.what();

    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_EQ(lua["g_appendTickCount"].get<int>(), 1);
}

TEST_F(LuaPipelineRegisterTest, InsertSystemBeforeViaLuaPlacesGroup) {
    const IRSystem::SystemId lifetime = m_lua.registerPrefabSystem<IRSystem::LIFETIME>();
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRSystem.registerPipeline(IRTime.UPDATE, {
            IRSystem.systemId(IRSystem.SystemName.LIFETIME),
        })
        local Marker = IRComponent.register("InsertMarker", { dummy = 0 })
        local luaSys = IRSystem.registerSystem({
            name = "InsertedLuaSys",
            components = { Marker },
            tick = function(arch) end,
        })
        IRSystem.insertSystemBefore(IRTime.UPDATE, luaSys,
            IRSystem.systemId(IRSystem.SystemName.LIFETIME))
        return luaSys
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto luaSys = static_cast<IRSystem::SystemId>(result.get<lua_Integer>());

    const auto &groups = m_system_manager.getPipelineGroups(IRTime::Events::UPDATE);
    ASSERT_EQ(groups.size(), 2u);
    EXPECT_EQ(groups[0][0], luaSys); // inserted before the prefab system
    EXPECT_EQ(groups[1][0], lifetime);
}

TEST_F(LuaPipelineRegisterTest, AppendSystemRejectsInvalidEvent) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script("IRSystem.appendSystem(999, 0)", sol::script_pass_on_error);
    EXPECT_FALSE(result.valid());
}

} // namespace
