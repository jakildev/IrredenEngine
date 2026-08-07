#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/systems/system_settings_menu.hpp>

namespace {

// #2880: SETTINGS_MENU caches its built widgets' ids (`panel_`,
// `controlsLabel_`, `quitButton_`, plus each Row's `control_`/`label_`) in
// System<N> members. None of those entities is C_Persistent, so
// IREntity::resetGameplay() taken with the menu open destroys them while
// `C_SettingsMenuState::open_` survives as a preserved singleton — without
// the pre-destroy hook the next endTick would take the `applyEdits()` branch
// (open_ && built_) and reach getComponent through dead widget ids.
class SettingsMenuResetTest : public testing::Test {
  protected:
    SettingsMenuResetTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(SettingsMenuResetTest, PanelDangleIsClearedAcrossResetGameplayAndRowsAreDropped) {
    const auto sysId = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(sysId);
    ASSERT_NE(params, nullptr);

    // Stand in for buildMenu()'s spawns without needing a GUI canvas — any
    // non-persistent entities dangle identically.
    params->panel_ = IREntity::createEntity();
    params->controlsLabel_ = IREntity::createEntity();
    params->quitButton_ = IREntity::createEntity();
    IRSystem::System<IRSystem::SETTINGS_MENU>::Row row;
    row.control_ = IREntity::createEntity();
    row.label_ = IREntity::createEntity();
    params->rows_.push_back(row);
    params->built_ = true;
    ASSERT_NE(params->panel_, IREntity::kNullEntity);

    IREntity::resetGameplay();

    EXPECT_EQ(params->panel_, IREntity::kNullEntity)
        << "pre-destroy hook must null panel_ so the next endTick's `!built_` "
           "branch rebuilds instead of reaching applyEdits() through a dead id";
    EXPECT_EQ(params->controlsLabel_, IREntity::kNullEntity);
    EXPECT_EQ(params->quitButton_, IREntity::kNullEntity);
    EXPECT_TRUE(params->rows_.empty())
        << "rows_ must be cleared so the next buildMenu() doesn't append onto "
           "stale Row entries destroyMenu() never got to clear";
    EXPECT_FALSE(
        params->built_
    ) << "built_ must drop to false so the next endTick takes the rebuild "
         "branch rather than staying wedged in applyEdits()";
}

TEST_F(SettingsMenuResetTest, UnrelatedEntityDestructionLeavesPanelIntact) {
    const auto sysId = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(sysId);
    ASSERT_NE(params, nullptr);

    const auto panel = IREntity::createEntity();
    params->panel_ = panel;
    params->built_ = true;
    const auto other = IREntity::createEntity();

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the
    // immediate destroy path, and this test needs that ordering guarantee.
    m_entity_manager.destroyEntity(other);

    EXPECT_EQ(params->panel_, panel)
        << "the hook must only reset menu state on an exact panel_ match, not "
           "on any destruction";
    EXPECT_TRUE(params->built_);
}

} // namespace
