#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_component_data.hpp>
#include <irreden/script/lua_script.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <variant>
#include <vector>

// #2286: EVAL-mode coverage for IREntity.deferredCreate / deferredDestroy —
// the Lua binding for structural entity changes queued from inside a system
// tick. The create's archetype insert drains at flushStructuralChanges (a
// group boundary, exercised here via executePipeline); the destroy drains at
// destroyMarkedEntities (pipeline end, called manually since these tests do
// not spin a full World). See docs/design/lua-driven-ecs.md §G4.

namespace {

class LuaDeferredEntityTest : public testing::Test {
  protected:
    LuaDeferredEntityTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        // LuaEntity is bound per-creation in production; tests bind a minimal
        // version so the addLuaComponent / setLuaField setup path resolves.
        m_lua.lua().new_usertype<IRScript::LuaEntity>(
            "LuaEntity",
            sol::constructors<IRScript::LuaEntity(IREntity::EntityId)>(),
            "entity",
            &IRScript::LuaEntity::entity
        );
    }

    // Count live entities carrying `componentId` across all archetype nodes.
    int countWith(IREntity::ComponentId componentId) {
        int total = 0;
        for (auto *node : IREntity::queryArchetypeNodesSimple({componentId})) {
            total += node->length_;
        }
        return total;
    }

    // Read an int32 field of the single row of the sole entity carrying
    // `componentId` (helper for the single-spawn assertions).
    std::vector<std::int32_t>
    readInt32Column(IREntity::ComponentId componentId, const std::string &fieldName) {
        std::vector<std::int32_t> values;
        for (auto *node : IREntity::queryArchetypeNodesSimple({componentId})) {
            auto *typed = static_cast<IRScript::IComponentDataLuaTyped *>(
                node->components_.at(componentId).get()
            );
            const int idx = typed->findFieldIndex(fieldName);
            const auto *col = std::get_if<std::vector<std::int32_t>>(&typed->columnAt(idx));
            if (col) {
                values.insert(values.end(), col->begin(), col->end());
            }
        }
        return values;
    }

    // m_lua first so its sol::state outlives sol::function-bearing columns
    // held by the EntityManager (matches lua_system_register_test.cpp).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// A deferredCreate reserves the id immediately but does not materialize the
// entity until flushStructuralChanges — matching the C++ deferred-create
// contract (no mid-tick archetype mutation).
TEST_F(LuaDeferredEntityTest, DeferredCreateMaterializesOnFlush) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("Widget = IRComponent.register('Widget', { hp = 5 })").valid());
    const IREntity::ComponentId widgetId = m_entity_manager.getComponentTypeByName("Widget");
    ASSERT_GT(widgetId, 0u);

    auto result = lua.safe_script(
        "return IREntity.deferredCreate({ { Widget, { hp = 42 } } })",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto eid = result.get<IREntity::EntityId>();

    // Reserved but not yet inserted — the archetype insert is queued.
    EXPECT_FALSE(m_entity_manager.entityExists(eid));
    EXPECT_EQ(countWith(widgetId), 0);

    m_entity_manager.flushStructuralChanges();

    EXPECT_TRUE(m_entity_manager.entityExists(eid));
    ASSERT_EQ(countWith(widgetId), 1);
    const auto hps = readInt32Column(widgetId, "hp");
    ASSERT_EQ(hps.size(), 1u);
    EXPECT_EQ(hps[0], 42);
}

// deferredCreate with no component list yields a bare (empty-archetype)
// entity — still reserved-then-flushed.
TEST_F(LuaDeferredEntityTest, DeferredCreateWithoutComponentsIsBareEntity) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script("return IREntity.deferredCreate()", sol::script_pass_on_error);
    ASSERT_TRUE(result.valid()) << sol::error{result}.what();
    const auto eid = result.get<IREntity::EntityId>();

    EXPECT_FALSE(m_entity_manager.entityExists(eid));
    m_entity_manager.flushStructuralChanges();
    EXPECT_TRUE(m_entity_manager.entityExists(eid));
}

// deferredDestroy queues a deletion drained by destroyMarkedEntities, not by
// flushStructuralChanges — so the entity survives a flush and dies on drain.
TEST_F(LuaDeferredEntityTest, DeferredDestroyRemovesOnDrain) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("Widget = IRComponent.register('Widget', { hp = 5 })").valid());
    const IREntity::ComponentId widgetId = m_entity_manager.getComponentTypeByName("Widget");

    const IREntity::EntityId e = IREntity::createEntity();
    m_entity_manager.addComponentDynamic(e, widgetId);
    lua["target"] = static_cast<lua_Integer>(e);

    ASSERT_TRUE(lua.safe_script("IREntity.deferredDestroy(target)").valid());

    // Still present after a structural-change flush (destroy is a separate
    // drain step).
    m_entity_manager.flushStructuralChanges();
    EXPECT_TRUE(m_entity_manager.entityExists(e));
    EXPECT_EQ(countWith(widgetId), 1);

    m_entity_manager.destroyMarkedEntities();
    EXPECT_FALSE(m_entity_manager.entityExists(e));
    EXPECT_EQ(countWith(widgetId), 0);
}

// The core acceptance: a Lua system tick that spawns a child per iterated
// entity AND destroys the iterated entity does not corrupt the archetype
// iteration in progress — because both ops are deferred. After the pipeline
// flush + deletion drain, the drivers are gone and the spawns exist with the
// per-entity payload the tick queued.
TEST_F(LuaDeferredEntityTest, CreateAndDestroyMidTickDoNotCorruptIteration) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("Cell = IRComponent.register('Cell', { n = 0 })").valid());
    ASSERT_TRUE(
        lua.safe_script("Spawned = IRComponent.register('Spawned', { origin = 0 })").valid()
    );
    const IREntity::ComponentId cellId = m_entity_manager.getComponentTypeByName("Cell");
    const IREntity::ComponentId spawnedId = m_entity_manager.getComponentTypeByName("Spawned");
    ASSERT_GT(cellId, 0u);
    ASSERT_GT(spawnedId, 0u);

    // Three driver entities, each Cell.n set to a distinct value the tick
    // copies into the spawned child so we can verify payloads survive.
    const std::vector<std::int32_t> origins{100, 200, 300};
    for (std::int32_t n : origins) {
        const IREntity::EntityId e = IREntity::createEntity();
        lua["_e"] = static_cast<lua_Integer>(e);
        lua["_n"] = static_cast<lua_Integer>(n);
        ASSERT_TRUE(
            lua.safe_script("IREntity.addLuaComponent(LuaEntity.new(_e), Cell, { n = _n })").valid()
        );
    }

    lua["visited"] = 0;
    auto reg = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'SpawnAndReap',
            components = { 'Cell' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    visited = visited + 1
                    IREntity.deferredCreate({
                        { Spawned, { origin = arch.Cell:getField(i, 'n') } }
                    })
                    IREntity.deferredDestroy(arch.entityAt(i))
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(reg.valid()) << sol::error{reg}.what();
    const IRSystem::SystemId sysId = reg.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE); // flushes the spawns
    m_entity_manager.destroyMarkedEntities();                 // drains the reaps

    // The tick saw exactly the three drivers — iteration was not corrupted by
    // the queued structural changes.
    EXPECT_EQ(lua["visited"].get<int>(), 3);

    // Drivers reaped, spawns materialized with their copied payloads.
    EXPECT_EQ(countWith(cellId), 0);
    ASSERT_EQ(countWith(spawnedId), 3);
    auto spawnedOrigins = readInt32Column(spawnedId, "origin");
    std::sort(spawnedOrigins.begin(), spawnedOrigins.end());
    EXPECT_EQ(spawnedOrigins, origins);
}

} // namespace
