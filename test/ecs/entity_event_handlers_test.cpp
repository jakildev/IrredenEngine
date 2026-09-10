#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/input/components/component_entity_event_handlers.hpp>
#include <irreden/input/systems/system_entity_hover_detect.hpp>
#include <irreden/script/lua_script.hpp>

#include <sol/sol.hpp>

namespace {

using IRComponents::C_EntityEventHandlers;

// Covers the #2582 migration of the Lua entity-event registry from a
// process-lifetime Meyers singleton to the singleton component
// C_EntityEventHandlers, and the lifetime contract that replaced #2572's
// manual engine-tail clear(): the sol::protected_function refs now live in an
// archetype column that dies at destroyAllEntities(), which the World runs
// (via World::end()) while the Lua VM is still open.
//
// Member order is load-bearing (test/CLAUDE.md, Lua-seam pattern): m_lua is
// declared FIRST so it is destroyed LAST, after the EntityManager has torn
// down the component column — the refs are unref'd against a still-open
// lua_State, exactly as World's m_lua-before-managers order guarantees in
// production.
class EntityEventHandlersTest : public testing::Test {
  protected:
    void TearDown() override {
        // Drop the singleton row (and with it every registered handler) even
        // if an assertion aborts the body first. This is the same ordering
        // World::end() gives production; without it a surviving sol ref would
        // outlive the fixture's lua_State and luaL_unref into a closed VM.
        m_entityManager.destroyAllEntities();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
};

// The accessor materializes the singleton row on first call. This is the
// mechanism the World ctor seeds ahead of time so no later call is structural.
TEST_F(EntityEventHandlersTest, AccessorMaterializesTheSingletonRow) {
    ASSERT_EQ(IREntity::singletonEntityOrNull<C_EntityEventHandlers>(), IREntity::kNullEntity)
        << "fixture must start with no registry row, or the create assertion is vacuous";

    IRSystem::getEntityEventHandlers();

    EXPECT_NE(IREntity::singletonEntityOrNull<C_EntityEventHandlers>(), IREntity::kNullEntity);
}

// Register one handler through each of the four add* paths, then clear(): every
// handler vector must go from non-empty back to empty. The pre-clear size
// assertions are the positive control — they prove the registrations landed, so
// the post-clear emptiness is a real drop rather than a vacuous already-empty
// pass.
TEST_F(EntityEventHandlersTest, ClearEmptiesAllFourHandlerVectors) {
    auto &handlers = IRSystem::getEntityEventHandlers();

    ASSERT_TRUE(
        m_lua.lua().safe_script("noopHandler = function() end", sol::script_pass_on_error).valid()
    );
    sol::protected_function fn = m_lua.lua()["noopHandler"];

    handlers.addOnHovered(fn);
    handlers.addOnUnhovered(fn);
    handlers.addOnClicked(fn);
    handlers.addOnRightClick(fn);

    EXPECT_EQ(handlers.onHovered_.size(), 1u);
    EXPECT_EQ(handlers.onUnhovered_.size(), 1u);
    EXPECT_EQ(handlers.onClicked_.size(), 1u);
    EXPECT_EQ(handlers.onRightClick_.size(), 1u);

    handlers.clear();

    EXPECT_TRUE(handlers.onHovered_.empty());
    EXPECT_TRUE(handlers.onUnhovered_.empty());
    EXPECT_TRUE(handlers.onClicked_.empty());
    EXPECT_TRUE(handlers.onRightClick_.empty());
}

// A registered handler must observably run — a size assertion alone would pass
// against a registry that stored the function and never called it.
TEST_F(EntityEventHandlersTest, RegisteredHandlerObservablyFires) {
    ASSERT_TRUE(m_lua.lua()
                    .safe_script(
                        "clickCount = 0\n"
                        "clickedId = -1\n"
                        "onClick = function(id, button) clickCount = clickCount + 1 "
                        "clickedId = id end",
                        sol::script_pass_on_error
                    )
                    .valid());

    auto &handlers = IRSystem::getEntityEventHandlers();
    handlers.addOnClicked(m_lua.lua()["onClick"]);
    ASSERT_EQ(handlers.onClicked_.size(), 1u);
    ASSERT_EQ(m_lua.lua()["clickCount"].get<int>(), 0) << "counter must start at zero";

    handlers.fireClicked(4242u, 0);

    EXPECT_EQ(m_lua.lua()["clickCount"].get<int>(), 1);
    EXPECT_EQ(m_lua.lua()["clickedId"].get<std::uint64_t>(), 4242u)
        << "the entity id must reach the Lua handler as its first argument";
}

// destroyAllEntities() takes the registry row with it — the refs-die-while-the-
// VM-is-open lock that replaced #2572's static-destructor hazard. The fixture
// tearing down clean afterwards is the other half of the assertion.
TEST_F(EntityEventHandlersTest, DestroyAllEntitiesDropsTheRegistry) {
    ASSERT_TRUE(
        m_lua.lua().safe_script("noopHandler = function() end", sol::script_pass_on_error).valid()
    );
    auto &handlers = IRSystem::getEntityEventHandlers();
    handlers.addOnHovered(m_lua.lua()["noopHandler"]);
    ASSERT_NE(IREntity::singletonEntityOrNull<C_EntityEventHandlers>(), IREntity::kNullEntity);

    m_entityManager.destroyAllEntities();

    EXPECT_EQ(IREntity::singletonOrNull<C_EntityEventHandlers>(), nullptr);
    EXPECT_EQ(IREntity::singletonEntityOrNull<C_EntityEventHandlers>(), IREntity::kNullEntity);
}

// The registry is world-scoped, not scene-scoped: resetGameplay() preserves
// singleton entities, so handlers registered before a scene transition are
// still registered after it. Identical to the process static's behaviour, and
// the reason input/CLAUDE.md documents the survives-reset / dies-at-teardown
// split.
TEST_F(EntityEventHandlersTest, RegistrySurvivesResetGameplay) {
    ASSERT_TRUE(
        m_lua.lua().safe_script("noopHandler = function() end", sol::script_pass_on_error).valid()
    );
    IRSystem::getEntityEventHandlers().addOnHovered(m_lua.lua()["noopHandler"]);

    IREntity::resetGameplay();

    auto *handlers = IREntity::singletonOrNull<C_EntityEventHandlers>();
    ASSERT_NE(handlers, nullptr);
    EXPECT_EQ(handlers->onHovered_.size(), 1u);
}

} // namespace
