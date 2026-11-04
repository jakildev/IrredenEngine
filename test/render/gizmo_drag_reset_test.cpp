#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_gizmo_drag.hpp>

namespace {

// GIZMO_DRAG caches the dragged handle's id (`dragHandle_`) and its
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

TEST(GizmoDragTransformTest, RotatedRootArrowUsesItsVisibleWorldAxis) {
    IRComponents::C_WorldTransform anchor;
    anchor.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kPi * 0.5f);
    const auto axis = IRPrefab::Gizmo::translationAxisWorld(IRMath::vec3(1.0f, 0.0f, 0.0f), anchor);
    const auto delta =
        IRPrefab::Gizmo::translationDeltaInParent(IRMath::vec3(2.0f, 3.0f, 0.0f), axis, {});
    EXPECT_NEAR(axis.x, 0.0f, 0.0001f);
    EXPECT_NEAR(axis.y, 1.0f, 0.0001f);
    EXPECT_NEAR(delta.x, 0.0f, 0.0001f);
    EXPECT_NEAR(delta.y, 3.0f, 0.0001f);
}

TEST(GizmoDragTransformTest, RotatedScaledParentConvertsWorldDragIntoLocalTranslation) {
    IRComponents::C_WorldTransform parent;
    parent.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kPi * 0.5f);
    parent.scale_ = IRMath::vec3(2.0f, 3.0f, 4.0f);
    IRComponents::C_WorldTransform anchor = parent;
    const auto axis = IRPrefab::Gizmo::translationAxisWorld(IRMath::vec3(1.0f, 0.0f, 0.0f), anchor);
    const auto delta =
        IRPrefab::Gizmo::translationDeltaInParent(IRMath::vec3(0.0f, 6.0f, 0.0f), axis, parent);
    EXPECT_NEAR(delta.x, 3.0f, 0.0001f);
    EXPECT_NEAR(delta.y, 0.0f, 0.0001f);
    EXPECT_NEAR(delta.z, 0.0f, 0.0001f);
    const auto worldDelta = IRMath::rotateVectorByQuat(parent.scale_ * delta, parent.rotation_);
    EXPECT_NEAR(worldDelta.x, 0.0f, 0.0001f);
    EXPECT_NEAR(worldDelta.y, 6.0f, 0.0001f);
}

TEST(GizmoDragTransformTest, ChildRotationAndReflectedParentKeepTheDrawnArrowDirection) {
    IRComponents::C_WorldTransform parent;
    parent.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kPi * 0.5f);
    parent.scale_ = IRMath::vec3(-2.0f, 3.0f, 4.0f);
    IRComponents::C_WorldTransform anchor = parent;
    anchor.rotation_ = IRMath::quatMul(parent.rotation_, parent.rotation_);
    const auto axis = IRPrefab::Gizmo::translationAxisWorld(IRMath::vec3(1.0f, 0.0f, 0.0f), anchor);
    EXPECT_NEAR(axis.x, 1.0f, 0.0001f);
    const auto delta =
        IRPrefab::Gizmo::translationDeltaInParent(IRMath::vec3(6.0f, 0.0f, 0.0f), axis, parent);
    const auto worldDelta = IRMath::rotateVectorByQuat(parent.scale_ * delta, parent.rotation_);
    EXPECT_NEAR(worldDelta.x, 6.0f, 0.0001f);
    EXPECT_NEAR(worldDelta.y, 0.0f, 0.0001f);
    EXPECT_NEAR(delta.y, -2.0f, 0.0001f);
}

TEST(GizmoDragTransformTest, ZeroScaleDoesNotProduceAnInvalidTranslation) {
    IRComponents::C_WorldTransform parent;
    parent.scale_ = IRMath::vec3(1.0f, 0.0f, 1.0f);
    EXPECT_EQ(
        IRPrefab::Gizmo::translationDeltaInParent(
            IRMath::vec3(1.0f),
            IRMath::vec3(1.0f, 0.0f, 0.0f),
            parent
        ),
        IRMath::vec3(0.0f)
    );
    EXPECT_EQ(
        IRPrefab::Gizmo::translationAxisWorld(IRMath::vec3(0.0f, 1.0f, 0.0f), parent),
        IRMath::vec3(0.0f)
    );
}

} // namespace
