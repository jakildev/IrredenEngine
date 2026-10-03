#include <gtest/gtest.h>

#include "common/allocation_counter.hpp"

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/canvas_residency.hpp>
#include <irreden/render/components/component_canvas_residency.hpp>
#include <irreden/render/components/component_canvas_residency_settings.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/systems/system_canvas_residency.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <algorithm>
#include <vector>

// Canvas residency: which entities hold an entity canvas, decided against the
// camera's interest region under a live-canvas budget.
//
// Headless: with no render manager the system reads the interest region from
// the shared cull viewport the fixture seeds, and a demotion runs end to end
// (releasing a canvas needs no GPU). A promotion allocates a canvas, so the
// system-level tests observe the staged candidate list instead and the pure
// quota/selection functions carry the rest.

namespace {

using IRComponents::C_CanvasResidency;
using IRComponents::C_CanvasResidencySettings;
using IRComponents::C_EntityCanvas;
using IRComponents::C_LocalTransform;
using IRComponents::C_RotationMode;
using IRComponents::C_WorldTransform;
using IRComponents::RotationMode;
using IRMath::IsoBounds2D;
using IRMath::vec2;
using IRMath::vec3;
using IRPrefab::CanvasResidency::classify;
using IRPrefab::CanvasResidency::PromotionCandidate;
using IRPrefab::CanvasResidency::promotionQuota;
using IRPrefab::CanvasResidency::selectNearest;
using IRPrefab::CanvasResidency::Verdict;

// ---- classify ------------------------------------------------------------

const IsoBounds2D kPromote{vec2(-100.0f, -50.0f), vec2(100.0f, 50.0f)};
const IsoBounds2D kDemote{vec2(-140.0f, -90.0f), vec2(140.0f, 90.0f)};

TEST(CanvasResidencyPolicy, GridEntityInsideThePromoteRegionIsPromoted) {
    EXPECT_EQ(classify(false, vec2(10.0f, -20.0f), kPromote, kDemote), Verdict::PROMOTE);
}

TEST(CanvasResidencyPolicy, ResidentEntityOutsideTheDemoteRegionIsDemoted) {
    EXPECT_EQ(classify(true, vec2(200.0f, 0.0f), kPromote, kDemote), Verdict::DEMOTE);
}

// Between the two regions an entity keeps whatever it has. Both arms matter:
// without the band a resident entity here would be demoted, and a GRID one
// promoted, on alternate frames.
TEST(CanvasResidencyPolicy, HysteresisBandKeepsBothStates) {
    const vec2 inBand(120.0f, 0.0f);
    ASSERT_FALSE(kPromote.contains(inBand));
    ASSERT_TRUE(kDemote.contains(inBand));
    EXPECT_EQ(classify(true, inBand, kPromote, kDemote), Verdict::KEEP);
    EXPECT_EQ(classify(false, inBand, kPromote, kDemote), Verdict::KEEP);
}

TEST(CanvasResidencyPolicy, SettledStatesAreKept) {
    EXPECT_EQ(classify(true, vec2(0.0f), kPromote, kDemote), Verdict::KEEP);
    EXPECT_EQ(classify(false, vec2(500.0f, 0.0f), kPromote, kDemote), Verdict::KEEP);
}

// ---- promotionQuota ------------------------------------------------------

C_CanvasResidencySettings settingsWith(int budget, int perFrame) {
    C_CanvasResidencySettings settings{};
    settings.liveCanvasBudget_ = budget;
    settings.promotionsPerFrame_ = perFrame;
    return settings;
}

TEST(CanvasResidencyPolicy, QuotaIsBoundedByThePerFramePromotions) {
    EXPECT_EQ(promotionQuota(settingsWith(100, 4), 10, 0, 30), 4);
}

TEST(CanvasResidencyPolicy, QuotaIsBoundedByTheRemainingBudget) {
    EXPECT_EQ(promotionQuota(settingsWith(12, 8), 10, 0, 30), 2);
}

TEST(CanvasResidencyPolicy, QuotaIsZeroAtTheBudget) {
    EXPECT_EQ(promotionQuota(settingsWith(16, 8), 16, 0, 5), 0);
    EXPECT_EQ(promotionQuota(settingsWith(16, 8), 20, 0, 5), 0)
        << "canvases the policy does not manage can already exceed the budget";
}

TEST(CanvasResidencyPolicy, ThisFramesDemotionsFreeBudgetForPromotions) {
    EXPECT_EQ(promotionQuota(settingsWith(16, 8), 16, 3, 5), 3);
}

TEST(CanvasResidencyPolicy, QuotaNeverExceedsTheCandidates) {
    EXPECT_EQ(promotionQuota(settingsWith(100, 8), 0, 0, 2), 2);
}

// ---- selectNearest -------------------------------------------------------

TEST(CanvasResidencyPolicy, NearestCandidatesComeFirst) {
    std::vector<PromotionCandidate> candidates{
        {11, 400.0f},
        {12, 25.0f},
        {13, 900.0f},
        {14, 100.0f},
    };

    selectNearest(candidates, 2);

    EXPECT_EQ(candidates[0].entity_, 12u);
    EXPECT_EQ(candidates[1].entity_, 14u);
}

TEST(CanvasResidencyPolicy, EntityIdBreaksDistanceTies) {
    std::vector<PromotionCandidate> candidates{{30, 4.0f}, {10, 4.0f}, {20, 4.0f}};

    selectNearest(candidates, 3);

    EXPECT_EQ(candidates[0].entity_, 10u);
    EXPECT_EQ(candidates[1].entity_, 20u);
    EXPECT_EQ(candidates[2].entity_, 30u);
}

// ---- the system ----------------------------------------------------------

class CanvasResidencySystem : public testing::Test {
  protected:
    using Residency = IRSystem::System<IRSystem::CANVAS_RESIDENCY>;

    CanvasResidencySystem() {
        IREntity::setName(IREntity::createEntity(), "camera");
        m_residency = IRSystem::createSystem<IRSystem::CANVAS_RESIDENCY>();
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(), m_residency}
        );
        IRRender::updateCullViewport(vec2(0.0f), vec2(1.0f), IRMath::ivec2(320, 180));
    }

    ~CanvasResidencySystem() override {
        // The cull viewport is process-wide state other suites read.
        IRRender::updateCullViewport(vec2(0.0f), vec2(1.0f), IRMath::ivec2(0));
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    Residency &system() {
        return *IRSystem::getSystemParams<Residency>(m_residency);
    }

    // A managed entity at @p world. Resident ones carry the canvas wrapper the
    // mode implies; its null canvas id keeps the release GPU-free.
    static IREntity::EntityId makeManaged(vec3 world, bool resident) {
        if (resident) {
            return IREntity::createEntity(
                C_LocalTransform{world},
                C_RotationMode{RotationMode::DETACHED_REVOXELIZE},
                C_EntityCanvas{},
                C_CanvasResidency{}
            );
        }
        return IREntity::createEntity(
            C_LocalTransform{world},
            C_RotationMode{RotationMode::GRID},
            C_CanvasResidency{}
        );
    }

    // First world position along +x/-y whose iso projection satisfies @p want.
    template <typename Predicate> static vec3 findWorld(Predicate &&want) {
        for (int step = 0; step < 4096; ++step) {
            const vec3 world(static_cast<float>(step), -static_cast<float>(step), 0.0f);
            if (want(IRMath::pos3DtoPos2DIsoYawed(world, 0.0f))) {
                return world;
            }
        }
        ADD_FAILURE() << "no world position satisfies the predicate";
        return vec3(0.0f);
    }

    static RotationMode modeOf(IREntity::EntityId entity) {
        return IREntity::getComponent<C_RotationMode>(entity).mode_;
    }

    static bool hasCanvas(IREntity::EntityId entity) {
        return IREntity::getComponentOptional<C_EntityCanvas>(entity).has_value();
    }

    bool isPromotionCandidate(IREntity::EntityId entity) {
        const auto &candidates = system().promotions_;
        return std::any_of(candidates.begin(), candidates.end(), [entity](const auto &candidate) {
            return candidate.entity_ == entity;
        });
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::SystemId m_residency = IRSystem::kNullSystemId;
};

TEST_F(CanvasResidencySystem, ResidentEntityOutsideTheRegionReleasesItsCanvas) {
    tick(); // resolve the regions
    const vec3 outside = findWorld([&](vec2 iso) { return !system().demoteRegion_.contains(iso); });
    const IREntity::EntityId farAway = makeManaged(outside, /*resident=*/true);
    const IREntity::EntityId nearby = makeManaged(vec3(0.0f), /*resident=*/true);

    tick();

    EXPECT_EQ(modeOf(farAway), RotationMode::GRID);
    EXPECT_FALSE(hasCanvas(farAway));
    EXPECT_EQ(modeOf(nearby), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(hasCanvas(nearby));
}

TEST_F(CanvasResidencySystem, EntityInTheHysteresisBandKeepsItsState) {
    tick();
    const vec3 inBand = findWorld([&](vec2 iso) {
        return !system().promoteRegion_.contains(iso) && system().demoteRegion_.contains(iso);
    });
    const IREntity::EntityId resident = makeManaged(inBand, /*resident=*/true);
    const IREntity::EntityId grid = makeManaged(inBand, /*resident=*/false);

    tick();

    EXPECT_EQ(modeOf(resident), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(hasCanvas(resident));
    EXPECT_FALSE(isPromotionCandidate(grid));
}

TEST_F(CanvasResidencySystem, GridEntityInsideTheRegionBecomesAPromotionCandidate) {
    tick();
    const IREntity::EntityId inside = makeManaged(vec3(0.0f), /*resident=*/false);
    const vec3 outside = findWorld([&](vec2 iso) { return !system().demoteRegion_.contains(iso); });
    const IREntity::EntityId farAway = makeManaged(outside, /*resident=*/false);

    tick();

    EXPECT_TRUE(isPromotionCandidate(inside));
    EXPECT_FALSE(isPromotionCandidate(farAway));
}

TEST_F(CanvasResidencySystem, NarrowDemoteMarginIsWidenedToThePromoteMargin) {
    IRPrefab::CanvasResidency::settings().promoteMarginIso_ = 40;
    IRPrefab::CanvasResidency::settings().demoteMarginIso_ = 10;

    tick();

    EXPECT_EQ(system().settings_.demoteMarginIso_, 40);
    EXPECT_EQ(system().demoteRegion_.min_, system().promoteRegion_.min_);
    EXPECT_EQ(system().demoteRegion_.max_, system().promoteRegion_.max_);
}

TEST_F(CanvasResidencySystem, NothingSwitchesBeforeAViewportExists) {
    IRRender::updateCullViewport(vec2(0.0f), vec2(1.0f), IRMath::ivec2(0));
    const IREntity::EntityId resident =
        makeManaged(vec3(5000.0f, -5000.0f, 0.0f), /*resident=*/true);

    tick();

    EXPECT_EQ(modeOf(resident), RotationMode::DETACHED_REVOXELIZE);
    EXPECT_TRUE(hasCanvas(resident));
}

TEST_F(CanvasResidencySystem, CandidateTicksAllocateNothingPastLastFramesHighWater) {
    tick();
    const vec3 outside = findWorld([&](vec2 iso) { return !system().demoteRegion_.contains(iso); });
    struct Row {
        IREntity::EntityId entity_;
        C_WorldTransform world_;
        C_RotationMode mode_;
    };
    std::vector<Row> rows;
    const auto addCandidates = [&](int count) {
        for (int i = 0; i < count; ++i) {
            rows.push_back(
                Row{makeManaged(vec3(0.0f), /*resident=*/false),
                    C_WorldTransform{vec3(0.0f), IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
                    C_RotationMode{RotationMode::GRID}}
            );
            rows.push_back(
                Row{makeManaged(outside, /*resident=*/true),
                    C_WorldTransform{outside, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f), vec3(1.0f)},
                    C_RotationMode{RotationMode::DETACHED_REVOXELIZE}}
            );
        }
    };
    // One frame of the system alone; only the per-entity ticks are counted.
    const C_CanvasResidency residency{};
    const auto countedTicks = [&]() {
        system().beginTick();
        const IRTest::AllocationCounter counter;
        for (const Row &row : rows) {
            system().tick(row.entity_, residency, row.world_, row.mode_);
        }
        return counter.allocations();
    };

    addCandidates(1);
    countedTicks();
    addCandidates(32);
    const std::size_t grownFrame = countedTicks();

    EXPECT_EQ(grownFrame, 0u);
    EXPECT_EQ(system().promotions_.size(), 33u);
    EXPECT_EQ(system().demotions_.size(), 33u);
}

} // namespace
