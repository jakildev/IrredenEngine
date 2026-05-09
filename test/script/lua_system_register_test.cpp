#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/script/lua_archetype_view.hpp>
#include <irreden/script/lua_binding_traits.hpp>
#include <irreden/script/lua_component_data.hpp>
#include <irreden/script/lua_script.hpp>

#include <cstdint>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace {

// Test-only components with sol2 bindings using member pointers, so the
// Lua side can both read and write fields directly. Most of the engine's
// existing `*_lua.hpp` bindings expose getters as lambdas (read-only); a
// production migration to sol::property pairs is out of scope for T-101.
// Using member-pointer bindings here is the cleanest way to verify the
// shared-ComponentId-space round-trip claim from the plan.

struct TestPos {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct TestVel {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Tag — used to demonstrate that a Lua-defined component participates
// in archetype filtering through the same ComponentId space as the C++
// types above.
struct LuaSysTestMarker {};

} // namespace

namespace IRScript {
template <> inline constexpr bool kHasLuaBinding<TestPos> = true;
template <> inline constexpr bool kHasLuaBinding<TestVel> = true;

template <> inline void bindLuaType<TestPos>(LuaScript &script) {
    script.registerType<TestPos, TestPos(), TestPos(float, float, float)>(
        "TestPos",
        "x",
        &TestPos::x,
        "y",
        &TestPos::y,
        "z",
        &TestPos::z
    );
}

template <> inline void bindLuaType<TestVel>(LuaScript &script) {
    script.registerType<TestVel, TestVel(), TestVel(float, float, float)>(
        "TestVel",
        "x",
        &TestVel::x,
        "y",
        &TestVel::y,
        "z",
        &TestVel::z
    );
}
} // namespace IRScript

namespace {

class LuaSystemRegisterTest : public testing::Test {
  protected:
    LuaSystemRegisterTest()
        : m_lua{}
        , m_entity_manager{}
        , m_system_manager{} {
        m_lua.bindLuaDrivenEcs();
        m_lua.registerTypeFromTraits<TestPos>();
        m_lua.registerTypeFromTraits<TestVel>();

        // LuaEntity is bound by each creation's lua_bindings.cpp;
        // tests bind a minimal version locally so the dynamic-add
        // path (`IREntity.addLuaComponent(LuaEntity.new(id), ...)`)
        // is exercised end-to-end.
        m_lua.lua().new_usertype<IRScript::LuaEntity>(
            "LuaEntity",
            sol::constructors<IRScript::LuaEntity(IREntity::EntityId)>(),
            "entity",
            &IRScript::LuaEntity::entity
        );
    }

    // m_lua first so its sol::state outlives sol::function-bearing
    // columns held by the EntityManager (matches modifier_lua_test.cpp).
    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

// ---- Acceptance criterion 1 -------------------------------------------------
//
// Lua system iterates a mix of C++-defined components alongside a
// Lua-defined tag, and writes back to a C++ component. C++ side reads
// the change.

TEST_F(LuaSystemRegisterTest, MixedCppAndLuaDefinedComponentsRoundTrip) {
    auto &lua = m_lua.lua();

    // Tag the entity with a Lua-defined "Marker" so the system's
    // archetype filter has to resolve a name from the runtime
    // ComponentId space.
    ASSERT_TRUE(lua.safe_script("Marker = IRComponent.register('Marker', { dummy = 0 })").valid());

    auto entityIncluded =
        IREntity::createEntity(TestPos{1.0f, 2.0f, 3.0f}, TestVel{10.0f, 20.0f, 30.0f});
    auto entityExcluded =
        IREntity::createEntity(TestPos{0.0f, 0.0f, 0.0f}, TestVel{0.0f, 0.0f, 0.0f});

    lua["entityIncluded"] = static_cast<lua_Integer>(entityIncluded);

    auto registerResult = lua.safe_script(
        R"(
        IREntity.addLuaComponent(LuaEntity.new(entityIncluded), Marker)
        sysId = IRSystem.registerSystem({
            name = 'MoveByVelocity',
            components = { 'TestPos', 'TestVel', 'Marker' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local pos = arch.TestPos:at(i)
                    local vel = arch.TestVel:at(i)
                    arch.TestPos:setAt(i, TestPos.new(pos.x + vel.x, pos.y + vel.y, pos.z + vel.z))
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    IRSystem::SystemId sysId = registerResult.get<lua_Integer>();
    EXPECT_LT(sysId, m_system_manager.getSystemCount());

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // Included entity moved by velocity — Lua wrote, C++ reads.
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityIncluded).x, 11.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityIncluded).y, 22.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityIncluded).z, 33.0f);

    // Untagged entity untouched — Marker filter held.
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityExcluded).x, 0.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityExcluded).y, 0.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityExcluded).z, 0.0f);
}

// ---- Acceptance criterion 2 -------------------------------------------------
//
// Body fires once per matching archetype per tick — not once per entity.

TEST_F(LuaSystemRegisterTest, BodyFiresOncePerArchetypeNotPerEntity) {
    auto &lua = m_lua.lua();

    // Three entities in a single archetype (TestPos + TestVel).
    for (int i = 0; i < 3; ++i) {
        IREntity::createEntity(TestPos{}, TestVel{});
    }

    lua["bodyCallCount"] = 0;
    auto registerResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'Counter',
            components = { 'TestPos', 'TestVel' },
            tick = function(arch)
                bodyCallCount = bodyCallCount + 1
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(registerResult.valid()) << sol::error{registerResult}.what();
    IRSystem::SystemId sysId = registerResult.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // One archetype matched, one body invocation — even though three
    // entities share that archetype.
    EXPECT_EQ(lua["bodyCallCount"].get<int>(), 1);
}

// ---- Acceptance criterion 3 -------------------------------------------------
//
// Unbound C++ component name fails fast at registerSystem, with an
// error pointing at the lua_component_pack.

TEST_F(LuaSystemRegisterTest, UnboundComponentNameFailsAtRegisterTime) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        R"(
        IRSystem.registerSystem({
            name = 'BogusSys',
            components = { 'C_NotBoundAnywhere' },
            tick = function(arch) end,
        })
    )",
        sol::script_pass_on_error
    );
    ASSERT_FALSE(result.valid());
    sol::error err = result;
    const std::string msg = err.what();
    EXPECT_NE(msg.find("C_NotBoundAnywhere"), std::string::npos);
    EXPECT_NE(msg.find("lua_component_pack"), std::string::npos);
}

// ---- Acceptance criterion 4 -------------------------------------------------
//
// registerSystem returns a SystemId that Lua can hold; SystemManager
// can run it through the pipeline machinery just like a templated system.

TEST_F(LuaSystemRegisterTest, ReturnsSystemIdUsableInPipeline) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "return IRSystem.registerSystem({ name = 'Ret', components = { 'TestPos' }, "
        "tick = function() end })"
    );
    ASSERT_TRUE(result.valid());
    const lua_Integer sysId = result.get<lua_Integer>();
    // SystemIds start at 0 in a fresh SystemManager, so the meaningful
    // round-trip check is "the returned id is in range and the
    // pipeline accepts it" rather than "id > 0".
    EXPECT_GE(sysId, 0);
    EXPECT_LT(sysId, m_system_manager.getSystemCount());

    m_system_manager.registerPipeline(
        IRTime::Events::UPDATE,
        {static_cast<IRSystem::SystemId>(sysId)}
    );
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_STREQ(m_system_manager.getSystemName(sysId).c_str(), "Ret");
}

// ---- Lua-only iteration -----------------------------------------------------
//
// Lua-defined component columns can be iterated, mutated, and read back
// through `LuaTypedColumnView` without crossing back to C++ between
// per-row operations.

TEST_F(LuaSystemRegisterTest, LuaOnlyComponentColumnsRoundTrip) {
    auto &lua = m_lua.lua();
    auto setup = lua.safe_script(R"(
        Hp = IRComponent.register('Hp', { current = 100, max = 100 })
        Tag = IRComponent.register('Tag', { kind = 0 })
    )");
    ASSERT_TRUE(setup.valid());

    const IREntity::ComponentId hpId = m_entity_manager.getComponentTypeByName("Hp");
    const IREntity::ComponentId tagId = m_entity_manager.getComponentTypeByName("Tag");
    ASSERT_GT(hpId, 0u);
    ASSERT_GT(tagId, 0u);

    for (int i = 0; i < 4; ++i) {
        auto e = IREntity::createEntity();
        m_entity_manager.addComponentDynamic(e, hpId);
        m_entity_manager.addComponentDynamic(e, tagId);
    }

    auto regResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'Damage',
            components = { 'Hp', 'Tag' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local cur = arch.Hp:getField(i, 'current')
                    arch.Hp:setField(i, 'current', cur - 10)
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(regResult.valid()) << sol::error{regResult}.what();
    const IRSystem::SystemId sysId = regResult.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    // Each entity dropped from 100 to 90 in one tick.
    auto nodes = IREntity::queryArchetypeNodesSimple({hpId, tagId});
    int totalRows = 0;
    for (auto *node : nodes) {
        auto *typed =
            static_cast<IRScript::IComponentDataLuaTyped *>(node->components_.at(hpId).get());
        const int currentIdx = typed->findFieldIndex("current");
        const auto *col = std::get_if<std::vector<std::int32_t>>(&typed->columnAt(currentIdx));
        ASSERT_NE(col, nullptr);
        for (auto v : *col) {
            EXPECT_EQ(v, 90);
            ++totalRows;
        }
    }
    EXPECT_EQ(totalRows, 4);
}

// ---- entityAt provides id access without copying the column --------------

TEST_F(LuaSystemRegisterTest, EntityAtSurfacesEntityIdsToLua) {
    auto &lua = m_lua.lua();
    auto e0 = IREntity::createEntity(TestPos{}, TestVel{});
    auto e1 = IREntity::createEntity(TestPos{}, TestVel{});

    lua["seenIds"] = lua.create_table();
    auto regResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'CollectIds',
            components = { 'TestPos', 'TestVel' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    table.insert(seenIds, arch.entityAt(i))
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(regResult.valid()) << sol::error{regResult}.what();
    const IRSystem::SystemId sysId = regResult.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    sol::table seen = lua["seenIds"];
    EXPECT_EQ(seen.size(), 2u);
    // archetype iteration order is not guaranteed; assert membership.
    std::set<IREntity::EntityId> ids;
    for (std::size_t i = 1; i <= seen.size(); ++i) {
        ids.insert(seen.get<IREntity::EntityId>(i));
    }
    EXPECT_TRUE(ids.count(e0));
    EXPECT_TRUE(ids.count(e1));
}

// ---- T-103: replaceSystemBody hot-reload -----------------------------------
//
// `IRSystem.replaceSystemBody(systemId, newFn)` reseats the tick body
// of an already-registered Lua system in place. The system's id,
// archetype filter, and pipeline membership are unchanged; the next
// tick on `systemId` invokes `newFn`.

TEST_F(LuaSystemRegisterTest, ReplaceSystemBodyChangesNextTickBehavior) {
    auto &lua = m_lua.lua();
    auto entity = IREntity::createEntity(TestPos{0.0f, 0.0f, 0.0f}, TestVel{1.0f, 0.0f, 0.0f});
    lua["entity"] = static_cast<lua_Integer>(entity);

    // Register: original body adds vel.x. After one UPDATE, x == 1.0.
    auto regResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'Move',
            components = { 'TestPos', 'TestVel' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local pos = arch.TestPos:at(i)
                    local vel = arch.TestVel:at(i)
                    arch.TestPos:setAt(i, TestPos.new(pos.x + vel.x, pos.y, pos.z))
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(regResult.valid()) << sol::error{regResult}.what();
    const IRSystem::SystemId sysId = regResult.get<lua_Integer>();
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entity).x, 1.0f);

    // Hot-swap: new body adds 100 * vel.x. After next UPDATE, x == 1 + 100.
    auto swapResult = lua.safe_script(
        R"(
        IRSystem.replaceSystemBody(sysId, function(arch)
            for i = 0, arch.length - 1 do
                local pos = arch.TestPos:at(i)
                local vel = arch.TestVel:at(i)
                arch.TestPos:setAt(i, TestPos.new(pos.x + 100.0 * vel.x, pos.y, pos.z))
            end
        end)
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(swapResult.valid()) << sol::error{swapResult}.what();

    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entity).x, 101.0f);
}

TEST_F(LuaSystemRegisterTest, ReplaceSystemBodyKeepsSystemIdAndPipelineIntact) {
    auto &lua = m_lua.lua();
    IREntity::createEntity(TestPos{}, TestVel{});

    auto regResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'Stable',
            components = { 'TestPos', 'TestVel' },
            tick = function(arch) end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(regResult.valid()) << sol::error{regResult}.what();
    const IRSystem::SystemId sysIdBefore = regResult.get<lua_Integer>();
    const IRSystem::SystemId countBefore = m_system_manager.getSystemCount();

    auto swapResult = lua.safe_script(
        "IRSystem.replaceSystemBody(sysId, function(arch) bodyCalled = true end)",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(swapResult.valid()) << sol::error{swapResult}.what();

    // No new system entity created — the swap reuses the existing slot.
    EXPECT_EQ(m_system_manager.getSystemCount(), countBefore);

    // The original SystemId still works through the pipeline.
    lua["bodyCalled"] = false;
    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysIdBefore});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);
    EXPECT_TRUE(lua["bodyCalled"].get<bool>());
    EXPECT_STREQ(m_system_manager.getSystemName(sysIdBefore).c_str(), "Stable");
}

TEST_F(LuaSystemRegisterTest, ReplaceSystemBodyOnUnknownIdRaisesLuaError) {
    auto &lua = m_lua.lua();
    auto result = lua.safe_script(
        "IRSystem.replaceSystemBody(99999, function(arch) end)",
        sol::script_pass_on_error
    );
    ASSERT_FALSE(result.valid());
    sol::error err = result;
    const std::string msg = err.what();
    EXPECT_NE(msg.find("99999"), std::string::npos);
    EXPECT_NE(msg.find("registerSystem"), std::string::npos);
}

// ---- excludes filter --------------------------------------------------------

TEST_F(LuaSystemRegisterTest, ExcludesFilterDropsTaggedArchetype) {
    auto &lua = m_lua.lua();
    ASSERT_TRUE(lua.safe_script("Skip = IRComponent.register('Skip', { dummy = 0 })").valid());

    auto entityKeep = IREntity::createEntity(TestPos{1.0f, 0.0f, 0.0f}, TestVel{1.0f, 0.0f, 0.0f});
    auto entitySkip = IREntity::createEntity(TestPos{1.0f, 0.0f, 0.0f}, TestVel{1.0f, 0.0f, 0.0f});

    lua["entitySkip"] = static_cast<lua_Integer>(entitySkip);
    auto setup = lua.safe_script("IREntity.addLuaComponent(LuaEntity.new(entitySkip), Skip)");
    ASSERT_TRUE(setup.valid());

    auto regResult = lua.safe_script(
        R"(
        sysId = IRSystem.registerSystem({
            name = 'KeepOnly',
            components = { 'TestPos', 'TestVel' },
            excludes = { 'Skip' },
            tick = function(arch)
                for i = 0, arch.length - 1 do
                    local pos = arch.TestPos:at(i)
                    arch.TestPos:setAt(i, TestPos.new(pos.x + 100.0, pos.y, pos.z))
                end
            end,
        })
        return sysId
    )",
        sol::script_pass_on_error
    );
    ASSERT_TRUE(regResult.valid()) << sol::error{regResult}.what();
    const IRSystem::SystemId sysId = regResult.get<lua_Integer>();

    m_system_manager.registerPipeline(IRTime::Events::UPDATE, {sysId});
    m_system_manager.executePipeline(IRTime::Events::UPDATE);

    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entityKeep).x, 101.0f);
    EXPECT_FLOAT_EQ(IREntity::getComponent<TestPos>(entitySkip).x, 1.0f);
}

} // namespace
