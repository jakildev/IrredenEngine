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
using IRSystem::ENTITY_HOVER_DETECT;

// #2582 post-review addendum. previousHoveredEntity_ is an event *payload*,
// not a cached handle with a lazy-respawn guard: when resetGameplay() destroys
// the hovered entity, the next hover transition would hand the destroyed id to
// every Lua onEntityUnhovered handler. Moving the state off the function-local
// static onto a System<N> member does NOT close this — systems are never
// destroyed, so the member survives resetGameplay identically
// (engine/entity/CLAUDE.md §"Scene-transition reset"). The pre-destroy hook
// registered in System<ENTITY_HOVER_DETECT>::create() is what closes it, and
// it does so by *suppressing* the unhover rather than delivering it against a
// corpse.
//
// beginTick() is not drivable here (it resolves the hovered entity through
// IRRender/IRInput manager globals that need a GL context), so these drive
// applyHoverTransition — the half that carries the state machine.
//
// Member order is load-bearing: m_lua is declared FIRST so it outlives the
// EntityManager's teardown of the handler column (test/CLAUDE.md Lua-seam).
class HoverDetectResetTest : public testing::Test {
  protected:
    void SetUp() override {
        m_systemId = IRSystem::createSystem<ENTITY_HOVER_DETECT>();
        m_params =
            m_systemManager.getSystemParams<IRSystem::System<ENTITY_HOVER_DETECT>>(m_systemId);
        ASSERT_NE(m_params, nullptr);

        ASSERT_TRUE(m_lua.lua()
                        .safe_script(
                            "unhoveredIds = {}\n"
                            "hoveredIds = {}\n"
                            "onUnhovered = function(id) "
                            "unhoveredIds[#unhoveredIds + 1] = id end\n"
                            "onHovered = function(id) hoveredIds[#hoveredIds + 1] = id end",
                            sol::script_pass_on_error
                        )
                        .valid());
        auto &handlers = IRSystem::getEntityEventHandlers();
        handlers.addOnUnhovered(m_lua.lua()["onUnhovered"]);
        handlers.addOnHovered(m_lua.lua()["onHovered"]);
    }

    void TearDown() override {
        m_entityManager.destroyAllEntities();
    }

    std::size_t unhoveredCount() {
        return m_lua.lua()["unhoveredIds"].get<sol::table>().size();
    }

    std::size_t hoveredCount() {
        return m_lua.lua()["hoveredIds"].get<sol::table>().size();
    }

    IRScript::LuaScript m_lua;
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IRSystem::SystemId m_systemId{};
    IRSystem::System<ENTITY_HOVER_DETECT> *m_params{nullptr};
};

// The lane the addendum names: hover an entity, resetGameplay() destroys it,
// the next transition must fire NO unhover for the dead id.
//
// Positive control: with the pre-destroy hook reverted, previousHoveredEntity_
// still holds the destroyed id at the third step and fireUnhovered delivers it
// — unhoveredCount() reads 1 and this test fails.
TEST_F(HoverDetectResetTest, ResetGameplayFiresNoUnhoverForTheDestroyedEntity) {
    const auto hovered = IREntity::createEntity();
    ASSERT_NE(hovered, IREntity::kNullEntity);

    m_params->applyHoverTransition(hovered);
    ASSERT_EQ(m_params->previousHoveredEntity_, hovered);
    ASSERT_EQ(hoveredCount(), 1u) << "the hover-enter must land, or the unhover arm is vacuous";
    ASSERT_EQ(unhoveredCount(), 0u);

    IREntity::resetGameplay();

    EXPECT_EQ(m_params->previousHoveredEntity_, IREntity::kNullEntity)
        << "the pre-destroy hook must null the id an unhover would be fired with";

    m_params->applyHoverTransition(IREntity::kNullEntity);

    EXPECT_EQ(unhoveredCount(), 0u)
        << "the unhover for a destroyed entity is suppressed, not delivered against a corpse";
}

// The hook must null on an exact match only — an unrelated destruction must
// leave a live hover intact, or every scene-local entity death would silently
// swallow the next real unhover.
TEST_F(HoverDetectResetTest, UnrelatedDestructionLeavesTheHoveredIdIntact) {
    const auto hovered = IREntity::createEntity();
    const auto other = IREntity::createEntity();
    m_params->applyHoverTransition(hovered);
    ASSERT_EQ(m_params->previousHoveredEntity_, hovered);

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the immediate
    // destroy path, and this test needs that ordering guarantee.
    m_entityManager.destroyEntity(other);

    EXPECT_EQ(m_params->previousHoveredEntity_, hovered);

    m_params->applyHoverTransition(IREntity::kNullEntity);

    EXPECT_EQ(unhoveredCount(), 1u) << "a real unhover must still be delivered after the hook ran";
}

// With no registry row the transition is a no-op on the handler side but must
// still track state — the singletonOrNull guard replaces the old static's
// always-present empty vectors, and must not skip the bookkeeping.
TEST_F(HoverDetectResetTest, TransitionTracksStateWithNoRegistryRow) {
    const auto hovered = IREntity::createEntity();
    m_entityManager.destroyAllEntities();
    ASSERT_EQ(IREntity::singletonOrNull<C_EntityEventHandlers>(), nullptr);
    m_params->previousHoveredEntity_ = IREntity::kNullEntity;

    m_params->applyHoverTransition(hovered);

    EXPECT_EQ(m_params->previousHoveredEntity_, hovered)
        << "a missing registry must not stop the state machine from advancing";
}

} // namespace
