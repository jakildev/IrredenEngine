#include <gtest/gtest.h>

#include <irreden/common/modifier_lua.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_script.hpp>

namespace {

using IRComponents::C_Modifiers;
using IRComponents::C_LambdaModifiers;
using IRComponents::FieldBindingId;
using IRComponents::TransformKind;
using IREntity::EntityId;

// Fixture: owns an EntityManager (sets the global g_entityManager) and a
// LuaScript with the modifier namespace pre-bound.
class ModifierLuaTest : public testing::Test {
  protected:
    ModifierLuaTest()
        : m_lua{}
        , m_entity_manager{} {
        IRScript::bindModifierNamespace(m_lua);
    }

    // m_lua must be declared first so it is destroyed last: sol::function
    // references inside LambdaModifier::fn_ call lua_unref during EntityManager
    // cleanup, which requires the lua_State to still be open.
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

// ---- Enum surface -----------------------------------------------------------

TEST_F(ModifierLuaTest, EnumValuesMatchCpp) {
    auto &lua = m_lua.lua();
    EXPECT_EQ(lua.script("return ir.modifier.ADD").get<int>(),
              static_cast<int>(TransformKind::ADD));
    EXPECT_EQ(lua.script("return ir.modifier.MULTIPLY").get<int>(),
              static_cast<int>(TransformKind::MULTIPLY));
    EXPECT_EQ(lua.script("return ir.modifier.SET").get<int>(),
              static_cast<int>(TransformKind::SET));
    EXPECT_EQ(lua.script("return ir.modifier.CLAMP_MIN").get<int>(),
              static_cast<int>(TransformKind::CLAMP_MIN));
    EXPECT_EQ(lua.script("return ir.modifier.CLAMP_MAX").get<int>(),
              static_cast<int>(TransformKind::CLAMP_MAX));
    EXPECT_EQ(lua.script("return ir.modifier.OVERRIDE").get<int>(),
              static_cast<int>(TransformKind::OVERRIDE));
}

// ---- registerField ----------------------------------------------------------

TEST_F(ModifierLuaTest, RegisterFieldReturnsNonzeroId) {
    auto &lua = m_lua.lua();
    int id = lua.script("return ir.modifier.registerField('lua.test.speed')").get<int>();
    EXPECT_GT(id, 0);
}

TEST_F(ModifierLuaTest, RegisterFieldIdsAreUnique) {
    auto &lua = m_lua.lua();
    int idA = lua.script("return ir.modifier.registerField('lua.test.uniq.a')").get<int>();
    int idB = lua.script("return ir.modifier.registerField('lua.test.uniq.b')").get<int>();
    EXPECT_NE(idA, idB);
}

// ---- push / applyToField per TransformKind ----------------------------------

class ModifierLuaApplyTest : public ModifierLuaTest {
  protected:
    ModifierLuaApplyTest() {
        m_field_id = IRPrefab::Modifier::registerField("lua.apply.test");
        m_entity = IREntity::createEntity(C_Modifiers{});
        m_lua.lua()["testEntity"] = static_cast<lua_Integer>(m_entity);
        m_lua.lua()["testField"]  = static_cast<lua_Integer>(m_field_id);
    }

    FieldBindingId m_field_id = 0;
    EntityId m_entity = 0;
};

TEST_F(ModifierLuaApplyTest, AddModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.ADD,
            param  = 5.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 10.0f);
    EXPECT_FLOAT_EQ(result, 15.0f);
}

TEST_F(ModifierLuaApplyTest, MultiplyModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.MULTIPLY,
            param  = 3.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 4.0f);
    EXPECT_FLOAT_EQ(result, 12.0f);
}

TEST_F(ModifierLuaApplyTest, SetModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.SET,
            param  = 99.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 1.0f);
    EXPECT_FLOAT_EQ(result, 99.0f);
}

TEST_F(ModifierLuaApplyTest, ClampMinModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.CLAMP_MIN,
            param  = 20.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 5.0f);
    EXPECT_FLOAT_EQ(result, 20.0f);
}

TEST_F(ModifierLuaApplyTest, ClampMaxModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.CLAMP_MAX,
            param  = 8.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 100.0f);
    EXPECT_FLOAT_EQ(result, 8.0f);
}

TEST_F(ModifierLuaApplyTest, OverrideModifierApplied) {
    m_lua.lua().script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.OVERRIDE,
            param  = 42.0,
            source = testEntity,
        })
    )");
    float result = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 1.0f);
    EXPECT_FLOAT_EQ(result, 42.0f);
}

// ---- applyToField from Lua --------------------------------------------------

TEST_F(ModifierLuaApplyTest, ApplyToFieldReturnedFromLua) {
    auto &lua = m_lua.lua();
    lua.script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.ADD,
            param  = 7.0,
            source = testEntity,
        })
    )");
    float result = lua.script("return ir.modifier.applyToField(testEntity, testField, 3.0)")
                       .get<float>();
    EXPECT_FLOAT_EQ(result, 10.0f);
}

// ---- removeBySource ---------------------------------------------------------

TEST_F(ModifierLuaApplyTest, RemoveBySourceClearsModifiers) {
    auto &lua = m_lua.lua();
    lua.script(R"(
        ir.modifier.push(testEntity, {
            field  = testField,
            kind   = ir.modifier.ADD,
            param  = 100.0,
            source = testEntity,
        })
    )");
    float before = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 0.0f);
    EXPECT_FLOAT_EQ(before, 100.0f);

    lua.script("ir.modifier.removeBySource(testEntity)");

    float after = IRPrefab::Modifier::applyToField(m_entity, m_field_id, 0.0f);
    EXPECT_FLOAT_EQ(after, 0.0f);
}

// ---- pushLambda -------------------------------------------------------------

TEST_F(ModifierLuaApplyTest, PushLambdaApplied) {
    auto &lua = m_lua.lua();
    // Create a fresh entity with both modifier component types.
    EntityId lambdaEntity = IREntity::createEntity(C_Modifiers{}, C_LambdaModifiers{});
    lua["lambdaEntity"] = static_cast<lua_Integer>(lambdaEntity);

    lua.script(R"(
        ir.modifier.pushLambda(lambdaEntity, {
            field  = testField,
            fn     = function(base) return base * 2.0 end,
            source = lambdaEntity,
        })
    )");
    // Lambda modifiers feed C_ResolvedFields through the resolver pipeline,
    // not applyToField. Verify the lambda was stored and is callable.
    auto *lmPtr = IREntity::getComponentOptional<C_LambdaModifiers>(lambdaEntity).value_or(nullptr);
    ASSERT_NE(lmPtr, nullptr);
    ASSERT_EQ(lmPtr->modifiers_.size(), 1u);
    EXPECT_EQ(lmPtr->modifiers_[0].field_, m_field_id);
    EXPECT_EQ(lmPtr->modifiers_[0].source_, lambdaEntity);
    EXPECT_FLOAT_EQ(lmPtr->modifiers_[0].fn_(5.0f), 10.0f);
}

// ---- pushGlobal -------------------------------------------------------------

// Fixture: sets up a standalone globals entity and wires it into the detail
// singleton directly, avoiding registerResolverPipeline() which also creates
// systems and requires SystemManager.
class ModifierLuaGlobalTest : public ModifierLuaTest {
  protected:
    ModifierLuaGlobalTest() {
        m_field_id = IRPrefab::Modifier::registerField("lua.global.smoke");
        m_globals_entity = IREntity::createEntity(IRComponents::C_GlobalModifiers{});
        IRPrefab::Modifier::detail::globalsEntityId() = m_globals_entity;
        m_lua.lua()["testField"] = static_cast<lua_Integer>(m_field_id);
    }

    ~ModifierLuaGlobalTest() {
        IRPrefab::Modifier::detail::globalsEntityId() = IREntity::kNullEntity;
    }

    IRComponents::FieldBindingId m_field_id = 0;
    IREntity::EntityId m_globals_entity = IREntity::kNullEntity;
};

TEST_F(ModifierLuaGlobalTest, PushGlobalAddsModifierToGlobalsEntity) {
    m_lua.lua().script(R"(
        ir.modifier.pushGlobal({
            field  = testField,
            kind   = ir.modifier.ADD,
            param  = 5.0,
            source = 0,
        })
    )");
    auto *g = IREntity::getComponentOptional<IRComponents::C_GlobalModifiers>(m_globals_entity)
                  .value_or(nullptr);
    ASSERT_NE(g, nullptr);
    ASSERT_EQ(g->modifiers_.size(), 1u);
    EXPECT_EQ(g->modifiers_[0].field_, m_field_id);
    EXPECT_FLOAT_EQ(g->modifiers_[0].param_, 5.0f);
    EXPECT_EQ(g->modifiers_[0].kind_, IRComponents::TransformKind::ADD);
}

} // namespace
