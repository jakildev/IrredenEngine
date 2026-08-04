#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_camera_mouse_rotate.hpp>

namespace {

// #2681: CAMERA_MOUSE_ROTATE caches the cursor-pivot marker's id in a
// System<N> member (`pivotIndicator_`). The entity is not C_Persistent, so
// IREntity::resetGameplay() destroys it; without the pre-destroy hook the
// cached id would dangle and the next drag's lazy-respawn check
// (`pivotIndicator_ == kNullEntity`) would never fire, reaching through a
// dead id instead of rebuilding. Mirrors
// perf_stats_overlay_reset_test.cpp's PERF_STATS_OVERLAY coverage.
class CameraMouseRotateResetTest : public testing::Test {
  protected:
    CameraMouseRotateResetTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(CameraMouseRotateResetTest, PivotIndicatorDangleIsClearedAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(sysId);
    ASSERT_NE(params, nullptr);

    // Stand in for IRPrefab::CursorPivot::createIndicator()'s lazy spawn
    // without needing a full drag — any non-persistent entity dangles
    // identically.
    params->pivotIndicator_ = IREntity::createEntity();
    ASSERT_NE(params->pivotIndicator_, IREntity::kNullEntity);

    IREntity::resetGameplay();

    EXPECT_EQ(params->pivotIndicator_, IREntity::kNullEntity)
        << "pre-destroy hook must null the cached id so the next drag's "
           "lazy-respawn guard rebuilds the indicator instead of reaching "
           "through a dead id";
}

TEST_F(CameraMouseRotateResetTest, UnrelatedEntityDestructionLeavesPivotIndicatorIntact) {
    const auto sysId = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(sysId);
    ASSERT_NE(params, nullptr);

    const auto indicator = IREntity::createEntity();
    params->pivotIndicator_ = indicator;
    const auto other = IREntity::createEntity();

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the
    // immediate destroy path, and this test needs that ordering guarantee.
    m_entity_manager.destroyEntity(other);

    EXPECT_EQ(params->pivotIndicator_, indicator)
        << "the hook must only null the cached id on an exact match, not on any destruction";
}

} // namespace
