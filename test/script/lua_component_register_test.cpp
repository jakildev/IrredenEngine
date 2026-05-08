#include <gtest/gtest.h>

#include <irreden/common/modifier.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/script/lua_component_data.hpp>
#include <irreden/script/lua_script.hpp>

#include <cstdint>
#include <string>
#include <variant>
#include <vector>

namespace {

// LuaScript first so its sol::state outlives sol::function-bearing
// columns held by the EntityManager (mirrors modifier_lua_test.cpp).
class LuaComponentTest : public testing::Test {
  protected:
    LuaComponentTest()
        : m_lua{}
        , m_entity_manager{} {
        m_lua.bindLuaDrivenEcs();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
};

// ---- Type inference --------------------------------------------------------

TEST_F(LuaComponentTest, IntegerDefaultsInferAsInt32) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "C_Hp = IRComponent.register('Hp', { current = 100, max = 100 })\n"
        "return C_Hp.componentId, C_Hp.fields.current.type, C_Hp.fields.max.type"
    );
    ASSERT_TRUE(result.valid());
    auto [id, currentTy, maxTy] = result.get<std::tuple<lua_Integer, std::string, std::string>>();
    EXPECT_GT(id, 0);
    EXPECT_EQ(currentTy, "int32");
    EXPECT_EQ(maxTy, "int32");
}

TEST_F(LuaComponentTest, FloatDefaultsInferAsFloat) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('Velocity', { x = 0.0, y = 0.0, z = 0.0 })\n"
        "return C.fields.x.type, C.fields.y.type, C.fields.z.type"
    );
    ASSERT_TRUE(result.valid());
    auto [x, y, z] = result.get<std::tuple<std::string, std::string, std::string>>();
    EXPECT_EQ(x, "float");
    EXPECT_EQ(y, "float");
    EXPECT_EQ(z, "float");
}

TEST_F(LuaComponentTest, MixedDefaultsInferPerField) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('Mixed', { tag = 'hello', alive = true, count = 42, weight "
        "= 3.5 })\n"
        "return C.fields.tag.type, C.fields.alive.type, C.fields.count.type, C.fields.weight.type"
    );
    ASSERT_TRUE(result.valid());
    auto [tag, alive, count, weight] =
        result.get<std::tuple<std::string, std::string, std::string, std::string>>();
    EXPECT_EQ(tag, "string");
    EXPECT_EQ(alive, "bool");
    EXPECT_EQ(count, "int32");
    EXPECT_EQ(weight, "float");
}

TEST_F(LuaComponentTest, ExplicitTypeFormForcesFloatOverIntegerDefault) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('HpForced', {\n"
        "    current = { type = 'float', default = 100 },\n"
        "})\n"
        "return C.fields.current.type"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<std::string>(), "float");
}

TEST_F(LuaComponentTest, ExplicitTableFormAcceptsOpaqueTablePayload) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('TagPayload', {\n"
        "    payload = { type = 'table', default = {} },\n"
        "})\n"
        "return C.fields.payload.type"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<std::string>(), "table");
}

TEST_F(LuaComponentTest, NestedTableShortFormFailsAtRegistration) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('Bad', { stuff = { 1, 2, 3 } })",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(result.valid());
}

TEST_F(LuaComponentTest, UnknownExplicitTypeTagFailsWithFieldName) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('Bad', {\n"
        "    weirdField = { type = 'quaternion', default = 0 }\n"
        "})",
        sol::script_pass_on_error
    );
    ASSERT_FALSE(result.valid());
    sol::error err = result;
    const std::string msg = err.what();
    EXPECT_NE(msg.find("weirdField"), std::string::npos);
    EXPECT_NE(msg.find("quaternion"), std::string::npos);
}

// ---- Identity rule ---------------------------------------------------------

TEST_F(LuaComponentTest, DuplicateRegistrationFails) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("IRComponent.register('Dup', { x = 0 })").valid());
    auto result =
        lua.safe_script("IRComponent.register('Dup', { x = 1 })", sol::script_pass_on_error);
    ASSERT_FALSE(result.valid());
    sol::error err = result;
    const std::string msg = err.what();
    EXPECT_NE(msg.find("Dup"), std::string::npos);
}

// ---- Native storage --------------------------------------------------------

TEST_F(LuaComponentTest, AttachToHundredEntitiesReadsBackAsRealInt32Column) {
    auto &lua = m_lua.lua();
    auto registerResult = lua.safe_script(
        "C_Hp = IRComponent.register('Hp', { current = 100, max = 100 })\n"
        "return C_Hp.componentId"
    );
    ASSERT_TRUE(registerResult.valid());
    const IREntity::ComponentId componentId = registerResult.get<lua_Integer>();
    EXPECT_GT(componentId, 0u);

    // Attach to 100 entities. The smoke contract is "column is a real
    // int32 vector after 100 attaches"; the attach call site itself is
    // exercised by OverridesAtAttachTimeWriteThroughToColumn.
    std::vector<IREntity::EntityId> entities;
    entities.reserve(100);
    for (int i = 0; i < 100; ++i) {
        entities.push_back(IREntity::createEntity());
        m_entity_manager.addComponentDynamic(entities.back(), componentId);
    }

    int sampledColumns = 0;
    int totalRows = 0;
    auto nodes = IREntity::queryArchetypeNodesSimple({componentId});
    ASSERT_FALSE(nodes.empty()) << "Hp archetype node missing after attach";
    for (auto *node : nodes) {
        auto *data = node->components_.at(componentId).get();
        auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
        ASSERT_NE(typed, nullptr);
        ASSERT_EQ(typed->schema().size(), 2u);

        // Field order is Lua-hash-iteration-order — not stable across
        // runs. Look up by name; the contract is that both fields are
        // INT32 columns with default 100.
        const int currentIdx = typed->findFieldIndex("current");
        const int maxIdx = typed->findFieldIndex("max");
        ASSERT_GE(currentIdx, 0);
        ASSERT_GE(maxIdx, 0);
        EXPECT_EQ(typed->schema()[currentIdx].type_, IRScript::LuaFieldType::INT32);
        EXPECT_EQ(typed->schema()[maxIdx].type_, IRScript::LuaFieldType::INT32);

        const auto *currentCol =
            std::get_if<std::vector<std::int32_t>>(&typed->columnAt(currentIdx));
        const auto *maxCol = std::get_if<std::vector<std::int32_t>>(&typed->columnAt(maxIdx));
        ASSERT_NE(currentCol, nullptr) << "current column is not std::vector<int32_t>";
        ASSERT_NE(maxCol, nullptr) << "max column is not std::vector<int32_t>";
        ASSERT_EQ(currentCol->size(), maxCol->size());
        for (std::size_t i = 0; i < currentCol->size(); ++i) {
            EXPECT_EQ((*currentCol)[i], 100);
            EXPECT_EQ((*maxCol)[i], 100);
        }
        sampledColumns++;
        totalRows += static_cast<int>(currentCol->size());
    }
    EXPECT_EQ(sampledColumns, static_cast<int>(nodes.size()));
    EXPECT_EQ(totalRows, 100);
}

TEST_F(LuaComponentTest, OverridesAtAttachTimeWriteThroughToColumn) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(
        lua.safe_script("C_Hp = IRComponent.register('HpOverride', { current = 100, max = 100 })")
            .valid()
    );
    const IREntity::ComponentId componentId = m_entity_manager.getComponentTypeByName("HpOverride");
    ASSERT_GT(componentId, 0u);

    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, componentId);

    auto [data, row] = m_entity_manager.getComponentDataAndRow(e, componentId);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
    const auto &col = typed->columnAt(typed->findFieldIndex("current"));
    const auto *intCol = std::get_if<std::vector<std::int32_t>>(&col);
    ASSERT_NE(intCol, nullptr);
    EXPECT_EQ((*intCol)[row], 100);
}

// ---- Field-binding registry visibility -------------------------------------

TEST_F(LuaComponentTest, ScalarFieldsAutoRegisterIntoFieldBindingRegistry) {
    auto &lua = m_lua.lua();
    const std::size_t before = IRPrefab::Modifier::detail::globalFieldRegistry().fieldCount();

    auto result = lua.safe_script(
        "C_Hp = IRComponent.register('FieldRegHp', { current = 100, max = 100 })\n"
        "return C_Hp.fields.current.bindingId, C_Hp.fields.max.bindingId"
    );
    ASSERT_TRUE(result.valid());
    auto [currentId, maxId] = result.get<std::tuple<lua_Integer, lua_Integer>>();
    EXPECT_GT(currentId, 0);
    EXPECT_GT(maxId, 0);
    EXPECT_NE(currentId, maxId);

    const std::size_t after = IRPrefab::Modifier::detail::globalFieldRegistry().fieldCount();
    EXPECT_EQ(after - before, 2u);

    auto &registry = IRPrefab::Modifier::detail::globalFieldRegistry();
    EXPECT_STREQ(
        registry.fieldName(static_cast<IRComponents::FieldBindingId>(currentId)),
        "FieldRegHp.current"
    );
    EXPECT_STREQ(
        registry.fieldName(static_cast<IRComponents::FieldBindingId>(maxId)),
        "FieldRegHp.max"
    );
}

TEST_F(LuaComponentTest, NonScalarFieldsHaveInvalidBindingId) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('NonScalar', {\n"
        "    label = 'x',\n"
        "    payload = { type = 'table', default = {} },\n"
        "})\n"
        "return C.fields.label.bindingId, C.fields.payload.bindingId"
    );
    ASSERT_TRUE(result.valid());
    auto [labelId, payloadId] = result.get<std::tuple<lua_Integer, lua_Integer>>();
    EXPECT_EQ(labelId, static_cast<lua_Integer>(IRComponents::kInvalidFieldId));
    EXPECT_EQ(payloadId, static_cast<lua_Integer>(IRComponents::kInvalidFieldId));
}

// ---- Archetype-move correctness -------------------------------------------

struct ArchetypeMoveMarker {};

TEST_F(LuaComponentTest, ArchetypeMovePreservesLuaTypedColumnValues) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(
        lua.safe_script("C_Hp = IRComponent.register('HpMove', { current = 100, max = 100 })")
            .valid()
    );
    const IREntity::ComponentId componentId = m_entity_manager.getComponentTypeByName("HpMove");

    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, componentId);

    {
        auto [data, row] = m_entity_manager.getComponentDataAndRow(e, componentId);
        auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
        const sol::object writeVal = sol::make_object(lua, 75);
        typed->writeFieldAt(row, typed->findFieldIndex("current"), writeVal);
    }

    // Trigger an archetype move by adding an unrelated component.
    IREntity::setComponent(e, ArchetypeMoveMarker{});

    auto [data, row] = m_entity_manager.getComponentDataAndRow(e, componentId);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
    const auto &col = typed->columnAt(typed->findFieldIndex("current"));
    const auto *intCol = std::get_if<std::vector<std::int32_t>>(&col);
    ASSERT_NE(intCol, nullptr);
    EXPECT_EQ((*intCol)[row], 75);
}

TEST_F(LuaComponentTest, RemoveLuaComponentDropsFromArchetype) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(
        lua.safe_script("C_Hp = IRComponent.register('HpRm', { current = 100, max = 100 })").valid()
    );
    const IREntity::ComponentId componentId = m_entity_manager.getComponentTypeByName("HpRm");

    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, componentId);
    EXPECT_TRUE(m_entity_manager.hasComponent(e, componentId));

    m_entity_manager.removeComponentDynamic(e, componentId);
    EXPECT_FALSE(m_entity_manager.hasComponent(e, componentId));
}

} // namespace
