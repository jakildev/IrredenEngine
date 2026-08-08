#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/sim_clock.hpp>
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

// The hook stands in for destroyMenu() on the reset path, and destroyMenu() has
// two responsibilities, not one: forget the widget ids AND restore the sim clock
// it paused. Dropping only the first is unrecoverable rather than merely
// incomplete — `open_` is a preserved singleton, so the very next endTick takes
// the `open_ && !built_` branch and buildMenu() re-saves savedTimeScale_ from a
// clock that is still paused, i.e. 0. From then on every close restores 0.
TEST_F(SettingsMenuResetTest, PausedSimClockIsRestoredAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(sysId);
    ASSERT_NE(params, nullptr);
    params->params_.pauseSimWhileOpen_ = true;

    // A custom rate rather than 1.0, so a hook that restored via IRSim::resume()
    // (hard 1x) would fail this arm too, not just one that restored nothing.
    constexpr float kRunningTimeScale = 0.5f;
    IRSim::setTimeScale(kRunningTimeScale);

    // Stand in for buildMenu()'s pause block and its panel spawn — a GUI canvas
    // isn't needed to reach the hook, only a live non-persistent panel_.
    params->savedTimeScale_ = IRSim::timeScale();
    IRSim::pause();
    params->panel_ = IREntity::createEntity();
    params->built_ = true;
    ASSERT_TRUE(IRSim::isPaused()) << "arm is vacuous unless the clock starts paused";

    IREntity::resetGameplay();

    EXPECT_FALSE(params->built_) << "the hook must have fired at all — without this the "
                                    "time-scale assertion below could pass vacuously";
    EXPECT_FLOAT_EQ(IRSim::timeScale(), kRunningTimeScale)
        << "the hook must take destroyMenu()'s whole tail, restoring the scale the "
           "menu paused; leaving the clock at 0 freezes the sim permanently once "
           "the next buildMenu() re-saves savedTimeScale_ from it";
}

// The clock restore above puts an IRSim call inside a pre-destroy hook, and
// engine/entity/CLAUDE.md §"Pre-destroy hooks" bans reaching the lazy-create
// singleton accessor from one during a bulk teardown: destroyAllEntities()
// walks an unordered snapshot and clears the singleton cache only at the end,
// so a hook firing after C_SimClock's own entity died would mint a replacement
// the snapshot cannot see, which the trailing clear then strands — a row
// forEachComponent still walks with no singletonEntity route back. That
// ordering is hash-order-dependent inside destroyAllEntities, so this arm
// reproduces it deterministically: destroy the clock first, then the panel.
TEST_F(SettingsMenuResetTest, ClockRestoreDoesNotResurrectADestroyedSimClock) {
    const auto sysId = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(sysId);
    ASSERT_NE(params, nullptr);
    params->params_.pauseSimWhileOpen_ = true;

    IRSim::setTimeScale(0.5f);
    params->savedTimeScale_ = IRSim::timeScale();
    IRSim::pause();
    params->panel_ = IREntity::createEntity();
    params->built_ = true;

    const auto clockEntity = IREntity::singletonEntity<IRComponents::C_SimClock>();
    m_entity_manager.destroyEntity(clockEntity);
    m_entity_manager.destroyEntity(params->panel_); // fires the hook

    ASSERT_FALSE(params->built_) << "arm is vacuous unless the hook actually ran";
    int clockRows = 0;
    IREntity::forEachComponent<IRComponents::C_SimClock>([&clockRows](IRComponents::C_SimClock &) {
        ++clockRows;
    });
    EXPECT_EQ(clockRows, 0)
        << "the restore must probe with the no-create accessor; reaching "
           "IRSim::setTimeScale here re-creates the destroyed clock singleton as "
           "an unreachable ghost row";
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
