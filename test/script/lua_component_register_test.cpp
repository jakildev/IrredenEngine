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
    // Numeric defaults need a fractional component for the inference to
    // resolve to float. LuaJIT (Lua 5.1 base) has no integer subtype, so
    // a whole-number literal written as `0.0` is indistinguishable from
    // `0` at the C API level — both round-trip through sol2 as int. Use
    // explicit `{type="float", default=N}` for whole-number floats.
    auto result = lua.safe_script(
        "local C = IRComponent.register('Velocity', { x = 0.5, y = 1.5, z = -3.14 })\n"
        "return C.fields.x.type, C.fields.y.type, C.fields.z.type"
    );
    ASSERT_TRUE(result.valid());
    auto [x, y, z] = result.get<std::tuple<std::string, std::string, std::string>>();
    EXPECT_EQ(x, "float");
    EXPECT_EQ(y, "float");
    EXPECT_EQ(z, "float");
}

TEST_F(LuaComponentTest, WholeNumberLiteralsAlwaysInferAsInt32UnderLuaJIT) {
    // Companion to FloatDefaultsInferAsFloat documenting the LuaJIT
    // limitation: a whole-number literal written with a decimal point
    // (`0.0`) infers as int32 because Lua 5.1's number type has no
    // integer subtype distinction. Tests authoring this contract
    // explicitly so a future regression (e.g. accidental switch to a
    // dual-num LuaJIT build) is loud.
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('VelocityWhole', { x = 0.0, y = 1.0, z = -3.0 })\n"
        "return C.fields.x.type, C.fields.y.type, C.fields.z.type"
    );
    ASSERT_TRUE(result.valid());
    auto [x, y, z] = result.get<std::tuple<std::string, std::string, std::string>>();
    EXPECT_EQ(x, "int32");
    EXPECT_EQ(y, "int32");
    EXPECT_EQ(z, "int32");
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

// ---- #1368: packed vec3 / ivec3 field kinds (G1a) -------------------------

TEST_F(LuaComponentTest, Vec3AndIvec3FieldsStoreAsNativeColumnsAndRoundTrip) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script(
                       "C_Vec = IRComponent.register('VecBody', {\n"
                       "    pos = { type = 'vec3', default = { 1.5, 2.5, 3.5 } },\n"
                       "    cell = { type = 'ivec3', default = { x = 4, y = 5, z = 6 } },\n"
                       "})"
    )
                    .valid());
    const IREntity::ComponentId componentId = m_entity_manager.getComponentTypeByName("VecBody");

    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, componentId);

    auto [data, row] = m_entity_manager.getComponentDataAndRow(e, componentId);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);

    // Packed fields materialise as real IRMath::vec3 / IRMath::ivec3 columns —
    // byte-identical to a hand-written C++ component (G1a / Q3).
    const auto &posCol = typed->columnAt(typed->findFieldIndex("pos"));
    const auto *vec3Col = std::get_if<std::vector<IRMath::vec3>>(&posCol);
    ASSERT_NE(vec3Col, nullptr);
    EXPECT_FLOAT_EQ((*vec3Col)[row].x, 1.5f);
    EXPECT_FLOAT_EQ((*vec3Col)[row].y, 2.5f);
    EXPECT_FLOAT_EQ((*vec3Col)[row].z, 3.5f);

    const auto &cellCol = typed->columnAt(typed->findFieldIndex("cell"));
    const auto *ivec3Col = std::get_if<std::vector<IRMath::ivec3>>(&cellCol);
    ASSERT_NE(ivec3Col, nullptr);
    EXPECT_EQ((*ivec3Col)[row], IRMath::ivec3(4, 5, 6)); // named { x, y, z } default

    // write→read round-trip through the sol::object accessors: writeFieldAt
    // accepts an { x, y, z } table; readFieldAt returns one.
    typed->writeFieldAt(
        row,
        typed->findFieldIndex("pos"),
        sol::make_object(lua, lua.create_table_with("x", 10.0, "y", 20.0, "z", 30.0))
    );
    EXPECT_FLOAT_EQ((*vec3Col)[row].x, 10.0f);

    sol::object readBack = typed->readFieldAt(row, typed->findFieldIndex("pos"), lua);
    ASSERT_TRUE(readBack.is<sol::table>());
    sol::table t = readBack.as<sol::table>();
    EXPECT_FLOAT_EQ(t["x"].get<float>(), 10.0f);
    EXPECT_FLOAT_EQ(t["y"].get<float>(), 20.0f);
    EXPECT_FLOAT_EQ(t["z"].get<float>(), 30.0f);
}

TEST_F(LuaComponentTest, Vec3FieldsAreNotModifierTargetable) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('VecModTarget', {\n"
        "    pos = { type = 'vec3', default = { 0, 0, 0 } },\n"
        "})\n"
        "return C.fields.pos.bindingId"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<lua_Integer>(), static_cast<lua_Integer>(IRComponents::kInvalidFieldId));
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

// ---- Field index + index-style accessors -----------------------------------

TEST_F(LuaComponentTest, FieldHandleExposesIndex) {
    auto &lua = m_lua.lua();
    // field.index must be a non-negative integer, distinct across all fields.
    auto result = lua.safe_script(
        "local C = IRComponent.register('IdxComp', { hp = 100, mp = 50, name = 'hero' })\n"
        "return C.fields.hp.index, C.fields.mp.index, C.fields.name.index"
    );
    ASSERT_TRUE(result.valid());
    auto [hpIdx, mpIdx, nameIdx] = result.get<std::tuple<lua_Integer, lua_Integer, lua_Integer>>();
    EXPECT_GE(hpIdx, 0);
    EXPECT_GE(mpIdx, 0);
    EXPECT_GE(nameIdx, 0);
    EXPECT_NE(hpIdx, mpIdx);
    EXPECT_NE(hpIdx, nameIdx);
    EXPECT_NE(mpIdx, nameIdx);
}

TEST_F(LuaComponentTest, FieldIndexMatchesFindFieldIndex) {
    auto &lua = m_lua.lua();
    // The index in the Lua handle must equal findFieldIndex's result for the same name.
    ASSERT_TRUE(lua.safe_script(
                       "C_Score = IRComponent.register('Score', { value = 0 })\n"
                       "scoreIdx = C_Score.fields.value.index"
    )
                    .valid());
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Score");
    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, cid);

    auto [data, row] = m_entity_manager.getComponentDataAndRow(e, cid);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
    const int cppIdx = typed->findFieldIndex("value");
    ASSERT_GE(cppIdx, 0);

    auto idxResult = lua.safe_script("return scoreIdx");
    ASSERT_TRUE(idxResult.valid());
    EXPECT_EQ(idxResult.get<lua_Integer>(), static_cast<lua_Integer>(cppIdx));
}

TEST_F(LuaComponentTest, IndexStyleSetGetRoundTrip) {
    // Verify readFieldAt / writeFieldAt work correctly at the C++ level using
    // the index exposed via the Lua handle — the same index getLuaField /
    // setLuaField will use at runtime.
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script(
                       "C_Energy = IRComponent.register('Energy', { level = 0 })\n"
                       "energyIdx = C_Energy.fields.level.index"
    )
                    .valid());
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Energy");
    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, cid);

    auto [data, row] = m_entity_manager.getComponentDataAndRow(e, cid);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);

    auto idxResult = lua.safe_script("return energyIdx");
    ASSERT_TRUE(idxResult.valid());
    const int fieldIdx = static_cast<int>(idxResult.get<lua_Integer>());

    typed->writeFieldAt(row, fieldIdx, sol::make_object(lua, 42));
    sol::object readBack = typed->readFieldAt(row, fieldIdx, lua);
    ASSERT_TRUE(readBack.is<int>());
    EXPECT_EQ(readBack.as<int>(), 42);
}

TEST_F(LuaComponentTest, IndexStyleLuaSetGet) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(
        lua.safe_script("C_Mana = IRComponent.register('Mana', { current = 100, max = 200 })")
            .valid()
    );
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Mana");
    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, cid);

    // Register a C++ helper so Lua can get a typed entity handle.
    lua["_makeHandle"] = [e]() -> IRScript::LuaEntity { return IRScript::LuaEntity{e}; };
    auto result = lua.safe_script(
        "local ent = _makeHandle()\n"
        "local idx = C_Mana.fields.current.index\n"
        "IREntity.setLuaField(ent, C_Mana, idx, 77)\n"
        "return IREntity.getLuaField(ent, C_Mana, idx)"
    );
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<int>(), 77);
}

TEST_F(LuaComponentTest, IndexStyleTableStyleAccessorsUnchanged) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("C_Shield = IRComponent.register('Shield', { hp = 50 })").valid());
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Shield");
    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, cid);

    lua["_makeHandle"] = [e]() -> IRScript::LuaEntity { return IRScript::LuaEntity{e}; };
    auto result = lua.safe_script("return IREntity.getLuaComponent(_makeHandle(), C_Shield).hp");
    ASSERT_TRUE(result.valid());
    EXPECT_EQ(result.get<int>(), 50);
}

TEST_F(LuaComponentTest, IndexStyleOutOfRangeRaisesLuaError) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("C_Tag = IRComponent.register('Tag', { label = 'x' })").valid());
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Tag");
    IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, cid);

    lua["_makeHandle"] = [e]() -> IRScript::LuaEntity { return IRScript::LuaEntity{e}; };

    auto getResult = lua.safe_script(
        "IREntity.getLuaField(_makeHandle(), C_Tag, 999)",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(getResult.valid());
    sol::error getErr = getResult;
    EXPECT_NE(std::string(getErr.what()).find("999"), std::string::npos);

    auto setResult = lua.safe_script(
        "IREntity.setLuaField(_makeHandle(), C_Tag, 999, 'oops')",
        sol::script_pass_on_error
    );
    EXPECT_FALSE(setResult.valid());
    sol::error setErr = setResult;
    EXPECT_NE(std::string(setErr.what()).find("999"), std::string::npos);
}

// ---- Singleton (T-162) -----------------------------------------------------

// `LuaEntity` is opaque to Lua under `bindLuaDrivenEcs()` alone (no
// usertype registration). The behavior we care about is "subsequent
// calls land on the same entity", which is observable via field-state
// persistence: write through the first handle, read through a second,
// and a properly cached singleton returns the written value.
TEST_F(LuaComponentTest, SingletonCachesAcrossCalls) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "local C = IRComponent.register('GameRules', { score = 0, level = 1 })\n"
        "local a = IREntity.singleton(C)\n"
        "IREntity.setLuaField(a, C, C.fields.score.index, 42)\n"
        "IREntity.setLuaField(a, C, C.fields.level.index, 7)\n"
        // Re-fetch the singleton; mutations must persist on a second call.
        "local b = IREntity.singleton(C)\n"
        "return IREntity.getLuaField(b, C, C.fields.score.index), "
        "       IREntity.getLuaField(b, C, C.fields.level.index)"
    );
    ASSERT_TRUE(result.valid());
    auto [score, level] = result.get<std::tuple<lua_Integer, lua_Integer>>();
    EXPECT_EQ(score, 42);
    EXPECT_EQ(level, 7);
}

TEST_F(LuaComponentTest, SingletonInteropsWithCppApi) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script(
                       "C_Counter = IRComponent.register('Counter', { value = 0 })\n"
                       "IREntity.setLuaField(IREntity.singleton(C_Counter), C_Counter, "
                       "                     C_Counter.fields.value.index, 17)"
    )
                    .valid());

    // C++ side reaches the same singleton via the typeName lookup.
    const IREntity::ComponentId cid = m_entity_manager.getComponentTypeByName("Counter");
    ASSERT_NE(cid, IREntity::kNullComponent);
    const IREntity::EntityId cppEntity = m_entity_manager.getOrCreateSingletonByComponentId(cid);
    auto [data, row] = m_entity_manager.getComponentDataAndRow(cppEntity, cid);
    ASSERT_NE(data, nullptr);
    auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(data);
    sol::object value = typed->readFieldAt(row, 0, lua);
    EXPECT_EQ(value.as<lua_Integer>(), 17);
}

} // namespace
