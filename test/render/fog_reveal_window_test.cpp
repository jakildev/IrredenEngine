#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/rotation_mode.hpp>
#include <irreden/render/active_canvas.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_fog_reveal_settings.hpp>
#include <irreden/render/components/component_fog_revealed.hpp>
#include <irreden/render/fog_reveal_systems.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/system/components/component_static.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using IRComponents::C_CanvasFogOfWar;
using IRComponents::C_FogRevealed;
using IRComponents::C_FogRevealSettings;
using IRComponents::C_RotationMode;
using IRComponents::C_Settled;
using IRComponents::C_Static;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FogOverride;
using IRComponents::RotationMode;

constexpr std::size_t kSubjectCount = 16;
constexpr std::uint32_t kWindowPeriod = 4;
constexpr IRMath::vec3 kOutside{40.0f, 40.0f, 0.0f};

struct DirectWindowFixture {
    DirectWindowFixture() {
        system_.activePool_ = &pool_;
        system_.fogAttached_ = true;
        system_.settings_.windowPeriod_ = kWindowPeriod;
        system_.pending_.reset(kSubjectCount * kWindowPeriod);
        system_.stamped_.reset(kSubjectCount * kWindowPeriod);
        system_.evaluatedByWorker_.assign(1, 0u);
        outside_.translation_ = kOutside;
        for (std::size_t i = 0; i < kSubjectCount; ++i) {
            entities_[i] = static_cast<IREntity::EntityId>(i)
                           << IRSystem::kFogRevealWindowEntityShift;
            revealed_[i] = C_FogRevealed{1.0f, true};
        }
    }

    void runUpdate(std::uint64_t counter) {
        system_.frameCounter_ = counter;
        for (std::size_t i = 0; i < kSubjectCount; ++i) {
            system_.tick(entities_[i], revealed_[i], outside_, sets_[i]);
        }
    }

    std::size_t hiddenCount() const {
        std::size_t hidden = 0;
        for (const C_FogRevealed &revealed : revealed_) {
            hidden += revealed.shown_ ? 0u : 1u;
        }
        return hidden;
    }

    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system_;
    C_VoxelPool pool_{IRMath::ivec3(2, 2, 2)};
    C_WorldTransform outside_{};
    std::array<IREntity::EntityId, kSubjectCount> entities_{};
    std::array<C_FogRevealed, kSubjectCount> revealed_{};
    std::array<C_VoxelSetNew, kSubjectCount> sets_{};
};

TEST(FogRevealWindowTest, DistinctBlocksAreAllVisitedWithinTheWindow) {
    DirectWindowFixture fixture;

    fixture.runUpdate(0);
    EXPECT_GT(fixture.hiddenCount(), 0u);
    EXPECT_LT(fixture.hiddenCount(), kSubjectCount);
    for (std::uint64_t update = 1; update < kWindowPeriod; ++update) {
        fixture.runUpdate(update);
    }

    EXPECT_EQ(fixture.hiddenCount(), kSubjectCount);
}

TEST(FogRevealWindowTest, WindowTakesPrecedenceOverTheStridedPeriod) {
    DirectWindowFixture fixture;
    fixture.system_.settings_.staggerPeriod_ = kWindowPeriod;

    for (std::uint64_t update = 0; update < kWindowPeriod; ++update) {
        fixture.runUpdate(update);
    }

    EXPECT_EQ(fixture.hiddenCount(), kSubjectCount);
}

TEST(FogRevealWindowTest, WindowOneRetainsTheStridedSchedule) {
    DirectWindowFixture fixture;
    fixture.system_.settings_.windowPeriod_ = 1;
    fixture.system_.settings_.staggerPeriod_ = kWindowPeriod;
    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{1.0f, true};
    C_VoxelSetNew voxelSet{};

    for (std::uint64_t update = 0; update < kWindowPeriod - 1; ++update) {
        fixture.system_.frameCounter_ = update;
        fixture.system_.tick(entity, revealed, fixture.outside_, voxelSet);
        EXPECT_TRUE(revealed.shown_);
    }
    fixture.system_.frameCounter_ = kWindowPeriod - 1;
    fixture.system_.tick(entity, revealed, fixture.outside_, voxelSet);

    EXPECT_FALSE(revealed.shown_);
}

TEST(FogRevealWindowTest, OverrideIsImmediateOutsideTheActiveWindow) {
    DirectWindowFixture fixture;
    fixture.system_.frameCounter_ = 1;
    fixture.revealed_[0].override_ = FogOverride::FORCE_HIDDEN;

    fixture.system_
        .tick(fixture.entities_[0], fixture.revealed_[0], fixture.outside_, fixture.sets_[0]);

    EXPECT_FALSE(fixture.revealed_[0].shown_);
}

TEST(FogRevealWindowTest, GateDisagreementIsRepairedWithinTheWindow) {
    DirectWindowFixture fixture;
    fixture.revealed_[0] = C_FogRevealed{0.0f, false};
    ASSERT_TRUE(fixture.sets_[0].visible_);

    for (std::uint64_t update = 1; update <= kWindowPeriod; ++update) {
        fixture.system_.frameCounter_ = update;
        fixture.system_
            .tick(fixture.entities_[0], fixture.revealed_[0], fixture.outside_, fixture.sets_[0]);
        fixture.system_.endTick();
    }

    EXPECT_FALSE(fixture.sets_[0].visible_);
}

class FogRevealSettledWindowTest : public testing::Test {
  protected:
    FogRevealSettledWindowTest()
        : m_entityManager{}
        , m_fog{C_CanvasFogOfWar::HeadlessInit{}}
        , m_entities(kWindowPeriod)
        , m_revealed(kWindowPeriod)
        , m_transforms(kWindowPeriod)
        , m_sets(kWindowPeriod) {
        m_type = IREntity::getArchetype<C_Static, C_Settled>();
        m_system.activePool_ = &m_pool;
        m_system.fogAttached_ = true;
        m_system.settings_.staggerPeriod_ = 16;
        m_system.settings_.windowPeriod_ = kWindowPeriod;
        m_system.pending_.reset(kWindowPeriod * kWindowPeriod);
        m_system.stamped_.reset(kWindowPeriod * kWindowPeriod);
        m_system.evaluatedByWorker_.assign(1, 0u);
        for (std::uint32_t block = 0; block < kWindowPeriod; ++block) {
            m_entities[block] = static_cast<IREntity::EntityId>(block)
                                << IRSystem::kFogRevealWindowEntityShift;
            m_revealed[block] = C_FogRevealed{1.0f, true};
        }
    }

    void begin(std::uint64_t counter) {
        m_system.frameCounter_ = counter;
        const std::uint32_t effectiveCadence = m_system.settings_.windowPeriod_ > 1u
                                                   ? m_system.settings_.windowPeriod_
                                                   : m_system.settings_.staggerPeriod_;
        m_system.gate_.begin(
            IREntity::kNullEntity,
            &m_pool,
            &m_fog,
            m_system.settings_,
            effectiveCadence,
            true
        );
    }

    void walk() {
        m_system.tick(m_type, m_entities, m_revealed, m_transforms, m_sets);
    }

    std::size_t hiddenCount() const {
        std::size_t hidden = 0;
        for (const C_FogRevealed &revealed : m_revealed) {
            hidden += revealed.shown_ ? 0u : 1u;
        }
        return hidden;
    }

    IREntity::EntityManager m_entityManager;
    C_CanvasFogOfWar m_fog;
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> m_system;
    C_VoxelPool m_pool{IRMath::ivec3(2, 2, 2)};
    IREntity::Archetype m_type;
    std::vector<IREntity::EntityId> m_entities;
    std::vector<C_FogRevealed> m_revealed;
    std::vector<C_WorldTransform> m_transforms;
    std::vector<C_VoxelSetNew> m_sets;
};

TEST_F(FogRevealSettledWindowTest, WindowSettingChangeArmsTheBodyGate) {
    m_system.settings_.staggerPeriod_ = 1;
    m_system.settings_.windowPeriod_ = 1;
    begin(0);
    EXPECT_TRUE(m_system.gate_.armed_);
    begin(1);
    ASSERT_FALSE(m_system.gate_.armed_);

    m_system.settings_.windowPeriod_ = kWindowPeriod;
    begin(2);

    EXPECT_TRUE(m_system.gate_.armed_);
    EXPECT_EQ(m_system.gate_.armedDispatchesLeft_, kWindowPeriod - 1);
}

TEST_F(FogRevealSettledWindowTest, FogChangeVisitsEverySettledBlockAtEffectiveCadence) {
    for (std::uint64_t update = 0; update < kWindowPeriod; ++update) {
        begin(update);
        EXPECT_TRUE(m_system.gate_.armed_);
    }
    begin(kWindowPeriod);
    ASSERT_FALSE(m_system.gate_.armed_);

    ++m_fog.revision_;
    for (std::uint64_t update = 0; update < kWindowPeriod; ++update) {
        begin(kWindowPeriod + 1u + update);
        ASSERT_TRUE(m_system.gate_.armed_);
        walk();
    }

    EXPECT_EQ(hiddenCount(), kWindowPeriod);
    EXPECT_EQ(m_system.gate_.armedDispatchesLeft_, 0u);
}

class FogRevealWindowPipelineTest : public testing::Test {
  protected:
    FogRevealWindowPipelineTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_canvas = IREntity::createEntity(
            C_VoxelPool{IRMath::ivec3(32, 32, 8)},
            C_CanvasFogOfWar{C_CanvasFogOfWar::HeadlessInit{}}
        );
        IRRender::setHeadlessActiveCanvasEntity(m_canvas);
        IREntity::getComponent<C_CanvasFogOfWar>(m_canvas)
            .setCell(0, 0, IRComponents::kFogStateVisible);
        m_systemManager.registerPipeline(IRTime::Events::UPDATE, IRPrefab::Fog::revealSystems());
    }

    ~FogRevealWindowPipelineTest() override {
        IRRender::setHeadlessActiveCanvasEntity(IREntity::kNullEntity);
    }

    void runUpdate() {
        m_systemManager.executePipeline(IRTime::Events::UPDATE);
        IREntity::flushStructuralChanges();
    }

    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(FogRevealWindowPipelineTest, SingletonWindowBoundsPipelineVisibilityLatency) {
    const IREntity::EntityId body = IREntity::createEntity(
        C_WorldTransform{IRMath::vec3(0.0f), IRMath::vec4(0, 0, 0, 1), IRMath::vec3(1.0f)},
        C_VoxelSetNew{IRMath::ivec3(2), IRMath::Color{180, 100, 40, 255}, true, m_canvas},
        C_RotationMode{RotationMode::GRID}
    );
    runUpdate();
    ASSERT_TRUE(IREntity::getComponent<C_FogRevealed>(body).shown_);

    IREntity::singleton<C_FogRevealSettings>().windowPeriod_ = kWindowPeriod;
    auto *system = m_systemManager.getSystemParams<IRSystem::System<IRSystem::FOG_REVEAL_EVAL>>(
        IRSystem::findSystem(IRSystem::FOG_REVEAL_EVAL)
    );
    if ((((static_cast<std::uint64_t>(body) >> IRSystem::kFogRevealWindowEntityShift) +
          system->frameCounter_ + 1u) %
         kWindowPeriod) == 0u) {
        runUpdate();
    }

    IREntity::getComponent<C_WorldTransform>(body).translation_ = kOutside;
    runUpdate();
    EXPECT_TRUE(IREntity::getComponent<C_FogRevealed>(body).shown_);

    for (std::uint32_t update = 1; update < kWindowPeriod; ++update) {
        runUpdate();
    }
    EXPECT_FALSE(IREntity::getComponent<C_FogRevealed>(body).shown_);
}

} // namespace
