#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_gizmo_drag.hpp>

namespace {

// #2880: GIZMO_DRAG caches the dragged handle's id (`dragHandle_`) and its
// anchor's id (`dragAnchor_`) in System<N> members. Neither entity is
// C_Persistent, so IREntity::resetGameplay() mid-drag destroys both; without
// the pre-destroy hook the cached ids would dangle and the next tick's
// `dragHandle_ == id` match (or applyDrag()'s getComponent on dragAnchor_)
// would reach through a dead id instead of the drag aborting cleanly.
class GizmoDragResetTest : public testing::Test {
  protected:
    GizmoDragResetTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(GizmoDragResetTest, DragHandleAndAnchorDangleIsClearedAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::GIZMO_DRAG>();
    auto *params = m_system_manager.getSystemParams<IRSystem::System<IRSystem::GIZMO_DRAG>>(sysId);
    ASSERT_NE(params, nullptr);

    // Stand in for a live drag's press-time capture without needing real
    // mouse input — any non-persistent entities dangle identically.
    params->dragHandle_ = IREntity::createEntity();
    params->dragAnchor_ = IREntity::createEntity();
    ASSERT_NE(params->dragHandle_, IREntity::kNullEntity);
    ASSERT_NE(params->dragAnchor_, IREntity::kNullEntity);

    IREntity::resetGameplay();

    EXPECT_EQ(params->dragHandle_, IREntity::kNullEntity)
        << "pre-destroy hook must null dragHandle_ so the next tick's `dragHandle_ "
           "== id` check stops matching a dead id";
    EXPECT_EQ(params->dragAnchor_, IREntity::kNullEntity)
        << "pre-destroy hook must null dragAnchor_ so applyDrag() cannot reach "
           "getComponent through a dead anchor id";
}

TEST_F(GizmoDragResetTest, UnrelatedEntityDestructionLeavesDragStateIntact) {
    const auto sysId = IRSystem::createSystem<IRSystem::GIZMO_DRAG>();
    auto *params = m_system_manager.getSystemParams<IRSystem::System<IRSystem::GIZMO_DRAG>>(sysId);
    ASSERT_NE(params, nullptr);

    const auto handle = IREntity::createEntity();
    const auto anchor = IREntity::createEntity();
    params->dragHandle_ = handle;
    params->dragAnchor_ = anchor;
    const auto other = IREntity::createEntity();

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the
    // immediate destroy path, and this test needs that ordering guarantee.
    m_entity_manager.destroyEntity(other);

    EXPECT_EQ(params->dragHandle_, handle)
        << "the hook must only clear drag state on an exact dragHandle_/dragAnchor_ "
           "match, not on any destruction";
    EXPECT_EQ(params->dragAnchor_, anchor)
        << "the hook must only clear drag state on an exact dragHandle_/dragAnchor_ "
           "match, not on any destruction";
}

} // namespace
