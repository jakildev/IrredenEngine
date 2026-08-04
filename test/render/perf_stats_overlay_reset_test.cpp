#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>

namespace {

// #2681: PERF_STATS_OVERLAY caches its text entity's id in a System<N>
// member (`textEntity_`). The entity is not C_Persistent, so
// IREntity::resetGameplay() destroys it; without the pre-destroy hook the
// cached id would dangle and the next endTick's lazy-respawn check
// (`textEntity_ == kNullEntity`) would never fire, reaching through a dead
// id instead of rebuilding.
class PerfStatsOverlayResetTest : public testing::Test {
  protected:
    PerfStatsOverlayResetTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PerfStatsOverlayResetTest, TextEntityDangleIsClearedAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::PERF_STATS_OVERLAY>>(sysId);
    ASSERT_NE(params, nullptr);

    // Stand in for createTextEntity()'s lazy spawn without needing a GUI
    // canvas — any non-persistent entity dangles identically.
    params->textEntity_ = IREntity::createEntity();
    ASSERT_NE(params->textEntity_, IREntity::kNullEntity);

    IREntity::resetGameplay();

    EXPECT_EQ(params->textEntity_, IREntity::kNullEntity)
        << "pre-destroy hook must null the cached id so endTick's lazy-respawn "
           "guard rebuilds the entity instead of reaching through a dead id";
}

TEST_F(PerfStatsOverlayResetTest, UnrelatedEntityDestructionLeavesTextEntityIntact) {
    const auto sysId = IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::PERF_STATS_OVERLAY>>(sysId);
    ASSERT_NE(params, nullptr);

    const auto textEntity = IREntity::createEntity();
    params->textEntity_ = textEntity;
    const auto other = IREntity::createEntity();

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the
    // immediate destroy path, and this test needs that ordering guarantee.
    m_entity_manager.destroyEntity(other);

    EXPECT_EQ(params->textEntity_, textEntity)
        << "the hook must only null the cached id on an exact match, not on any destruction";
}

} // namespace
