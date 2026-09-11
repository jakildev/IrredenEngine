// The two prefab headless-introspection probes — `HelpOverlay::systemOrNull()`
// and `SettingsMenu::systemOrNull()` — resolve their system through
// `IRSystem::findSystem` and must read a clean negative when the creation never
// registered it. `findSystem`'s miss answer is `IRSystem::kNullSystemId`, not
// `IREntity::kNullEntity`: #2540 moved it off 0 precisely because 0 is a
// legitimate id (the first system registered in a process gets it).
//
// Comparing against the wrong sentinel breaks the probe in BOTH directions, so
// each prefab is covered by a pair of arms:
//
//   * absent system  — `kNullEntity` (0) never equals the `kNullSystemId` miss
//     answer, so the guard falls through to `getSystemParams(kNullSystemId)`,
//     which trips `IR_ASSERT(system < m_nextSystemId)` in debug and indexes
//     `std::vector m_systemParams` out of bounds under `IR_RELEASE`.
//   * system registered as id 0 — the guard matches a *live* id and reports the
//     registered system as absent, a silent false negative no assert catches.
//
// The id-0 arm is not incidental: each fixture builds a fresh `SystemManager`
// whose `m_nextSystemId` starts at 0, so the first system it registers lands on
// exactly the id the wrong sentinel aliases.
//
// GL-free by construction: registering a system only records its archetype and
// params, and the probes are two hash lookups. No context, no backend, so this
// runs on every host rather than skipping on the Metal ones.

#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/help_overlay.hpp>
#include <irreden/render/settings_menu.hpp>
#include <irreden/render/systems/system_perf_stats_overlay.hpp>

namespace {

struct C_PrefabProbeSibling {};

class PrefabSystemProbeTest : public testing::Test {
  protected:
    PrefabSystemProbeTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    void expectMainThreadRegistration(IRSystem::SystemId target, const char *targetName) {
        const IRSystem::SystemAccess targetAccess = m_system_manager.getSystemAccess(target);
        EXPECT_TRUE(targetAccess.mainThreadOnly_);
        EXPECT_EQ(m_system_manager.getSystemName(target), targetName);

        const IRSystem::SystemId sibling = IRSystem::createSystem<C_PrefabProbeSibling>(
            "PrefabProbeSibling",
            [](C_PrefabProbeSibling &) {}
        );
        const IRSystem::SystemAccess accesses[]{
            targetAccess,
            m_system_manager.getSystemAccess(sibling),
        };
        EXPECT_EQ(
            IRSystem::findPipelineGroupConflict(accesses, 2).kind_,
            IRSystem::GroupConflictKind::MAIN_THREAD_IN_GROUP
        );

        m_system_manager.registerPipelineGroups(IRTime::Events::RENDER, {{target}});
        EXPECT_NO_THROW(m_system_manager.validateAllPipelineGroups());

        m_system_manager.registerPipelineGroups(IRTime::Events::RENDER, {{target, sibling}});
        EXPECT_THROW(m_system_manager.validateAllPipelineGroups(), std::runtime_error);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(PrefabSystemProbeTest, HelpOverlayProbeReadsAbsentWhenTheSystemIsNotRegistered) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::HELP_OVERLAY), IRSystem::kNullSystemId)
        << "fixture precondition: nothing registered HELP_OVERLAY";

    EXPECT_EQ(IRPrefab::HelpOverlay::systemOrNull(), nullptr);
    EXPECT_EQ(IRPrefab::HelpOverlay::builtText(), std::string{});
    EXPECT_EQ(IRPrefab::HelpOverlay::lastGlyphCommandCount(), 0);
}

TEST_F(PrefabSystemProbeTest, SettingsMenuProbeReadsAbsentWhenTheSystemIsNotRegistered) {
    ASSERT_EQ(IRSystem::findSystem(IRSystem::SETTINGS_MENU), IRSystem::kNullSystemId)
        << "fixture precondition: nothing registered SETTINGS_MENU";

    EXPECT_EQ(IRPrefab::SettingsMenu::systemOrNull(), nullptr);
    EXPECT_EQ(IRPrefab::SettingsMenu::liveRowCount(), 0);
    EXPECT_EQ(IRPrefab::SettingsMenu::quitButton(), IREntity::kNullEntity);
}

// Positive control for the pair above, and the arm that pins the sentinel: a
// probe hardcoded to `return nullptr` would pass both absent arms, and a probe
// comparing against `IREntity::kNullEntity` fails here specifically.
TEST_F(PrefabSystemProbeTest, HelpOverlayProbeResolvesTheSystemRegisteredAsIdZero) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::HELP_OVERLAY>();
    ASSERT_EQ(id, IRSystem::SystemId{0})
        << "arm precondition: the first system in a fresh manager must take id "
           "0 — the id `IREntity::kNullEntity` aliases";

    const auto *system = IRPrefab::HelpOverlay::systemOrNull();
    EXPECT_NE(system, nullptr) << "a live system registered as id 0 must not read as absent";
    EXPECT_EQ(system, IRSystem::getSystemParams<IRSystem::System<IRSystem::HELP_OVERLAY>>(id));
}

TEST_F(PrefabSystemProbeTest, SettingsMenuProbeResolvesTheSystemRegisteredAsIdZero) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    ASSERT_EQ(id, IRSystem::SystemId{0})
        << "arm precondition: the first system in a fresh manager must take id "
           "0 — the id `IREntity::kNullEntity` aliases";

    const auto *system = IRPrefab::SettingsMenu::systemOrNull();
    EXPECT_NE(system, nullptr) << "a live system registered as id 0 must not read as absent";
    EXPECT_EQ(system, IRSystem::getSystemParams<IRSystem::System<IRSystem::SETTINGS_MENU>>(id));
}

TEST_F(PrefabSystemProbeTest, PerfStatsOverlayRegistrationRequiresMainThread) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::PERF_STATS_OVERLAY>();
    expectMainThreadRegistration(id, "PerfStatsOverlay");
}

TEST_F(PrefabSystemProbeTest, SettingsMenuRegistrationRequiresMainThread) {
    const IRSystem::SystemId id = IRSystem::createSystem<IRSystem::SETTINGS_MENU>();
    expectMainThreadRegistration(id, "SettingsMenu");
}

} // namespace
