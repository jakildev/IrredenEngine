#include <gtest/gtest.h>

#include "common/allocation_counter.hpp"

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>
#include <irreden/render/detached_revoxelize.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <cstddef>
#include <utility>
#include <vector>

// CPU half of the detached re-voxelize cell groups: the per-group seed scan,
// the dest window each group's fill dispatches over, and the descriptor the
// kernel reads. The GPU half is covered by the canvas_stress `sharedparts`
// render references.

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_DetachedRevoxelizeBuffer;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRComponents::RevoxelizeGroupSeed;
using IRComponents::VoxelCellGroup;
using IRMath::ivec3;
using IRMath::vec3;
using IRMath::vec4;

constexpr IRMath::Color kColor{90, 180, 220, 255};

// ---- destWindowBase ------------------------------------------------------

// A single centered set — translation 0, lattice phase equal to its own
// anchor — must keep the window the kernel used before groups existed:
// [-center, +center], shifted one cell up on each anchored (-0.5) axis.
TEST(RevoxelizeDestWindow, UntranslatedGroupKeepsTheSingleSetWindow) {
    constexpr int kCenter = 9;
    for (int mask = 0; mask < 8; ++mask) {
        const vec3 anchor(
            (mask & 1) != 0 ? -0.5f : 0.0f,
            (mask & 2) != 0 ? -0.5f : 0.0f,
            (mask & 4) != 0 ? -0.5f : 0.0f
        );
        const ivec3 base =
            IRPrefab::DetachedRevoxelize::destWindowBase(vec3(0.0f), anchor, kCenter);
        const ivec3 expected =
            ivec3(-kCenter) + ivec3((mask & 1) != 0, (mask & 2) != 0, (mask & 4) != 0);
        EXPECT_EQ(base, expected) << "anchor mask " << mask;
    }
}

TEST(RevoxelizeDestWindow, WindowFollowsTheGroupTranslation) {
    const ivec3 base =
        IRPrefab::DetachedRevoxelize::destWindowBase(vec3(3.25f, -2.5f, 7.0f), vec3(0.0f), 4);
    EXPECT_EQ(base, ivec3(0, -6, 3));
}

// The window must hold every dest cell whose lattice point lies within the
// group's radius of its translation, for sub-cell translations and either
// phase — a cell outside it is never dispatched, which reads as a clipped
// solid.
TEST(RevoxelizeDestWindow, WindowCoversEveryCellInsideTheRadius) {
    constexpr int kCenter = 5;
    constexpr int kSide = 2 * kCenter + 1;
    for (const float phase : {0.0f, -0.5f}) {
        for (int step = -40; step <= 40; ++step) {
            const float translation = static_cast<float>(step) * 0.125f;
            const int base = IRPrefab::DetachedRevoxelize::destWindowBase(
                                 vec3(translation),
                                 vec3(phase),
                                 kCenter
            )
                                 .x;
            int covered = 0;
            for (int cell = -64; cell <= 64; ++cell) {
                const float point = static_cast<float>(cell) + phase - translation;
                if (IRMath::abs(point) > static_cast<float>(kCenter)) {
                    continue;
                }
                ++covered;
                EXPECT_GE(cell, base) << "translation " << translation << " phase " << phase;
                EXPECT_LT(cell, base + kSide)
                    << "translation " << translation << " phase " << phase;
            }
            EXPECT_GT(covered, 0);
        }
    }
}

// ---- groupParams ---------------------------------------------------------

TEST(RevoxelizeGroupParams, PacksTheSeedAndThePose) {
    RevoxelizeGroupSeed seed{};
    seed.gridMin_ = ivec3(-1, -2, -3);
    seed.gridDims_ = ivec3(4, 5, 6);
    seed.gridWordBase_ = 360;
    seed.anchor_ = vec3(-0.5f, 0.0f, -0.5f);
    seed.destCenter_ = 4;
    seed.destSide_ = 9;
    seed.destSlotBase_ = 729;
    const vec4 rotation = IRMath::quatAxisAngle(vec3(0.0f, 1.0f, 0.0f), 0.4f);
    const vec3 translation(6.0f, -1.5f, 2.25f);
    const vec3 phase(-0.5f, -0.5f, 0.0f);

    const IRRender::RevoxelizeGroupParams params =
        IRPrefab::DetachedRevoxelize::groupParams(seed, rotation, translation, phase);

    EXPECT_EQ(params.rotation_, rotation);
    EXPECT_EQ(vec3(params.destOffset_), phase - translation);
    EXPECT_EQ(vec3(params.anchor_), seed.anchor_);
    EXPECT_EQ(
        ivec3(params.destBase_),
        IRPrefab::DetachedRevoxelize::destWindowBase(translation, phase, 4)
    );
    EXPECT_EQ(params.destBase_.w, 9);
    EXPECT_EQ(ivec3(params.srcGridMin_), seed.gridMin_);
    EXPECT_EQ(params.srcGridMin_.w, 729);
    EXPECT_EQ(ivec3(params.srcGridDims_), seed.gridDims_);
    EXPECT_EQ(params.srcGridDims_.w, 360);
}

// A dest cell at the group's translation maps to the source origin, whatever
// the rotation: the point the kernel rotates is `cell + destOffset`.
TEST(RevoxelizeGroupParams, DestOffsetCentersTheGroupOnItsTranslation) {
    RevoxelizeGroupSeed seed{};
    const vec3 translation(4.0f, -3.0f, 1.0f);
    const vec3 phase(0.0f);

    const IRRender::RevoxelizeGroupParams params =
        IRPrefab::DetachedRevoxelize::groupParams(seed, vec4(0, 0, 0, 1), translation, phase);

    EXPECT_EQ(vec3(4.0f, -3.0f, 1.0f) + vec3(params.destOffset_), vec3(0.0f));
}

// ---- seed scan + span bookkeeping ---------------------------------------

class RevoxelizeGroupSeeds : public testing::Test {
  protected:
    RevoxelizeGroupSeeds()
        : m_canvas{
              IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)}, C_CanvasLocalRotation{})
          } {}

    C_VoxelPool &pool() {
        return IREntity::getComponent<C_VoxelPool>(m_canvas);
    }

    const C_VoxelSetNew &makeSet(ivec3 size, IRMath::Color color = kColor) {
        const IREntity::EntityId entity =
            IREntity::createEntity(C_VoxelSetNew{size, color, EntityAnchor::CENTER, m_canvas});
        return IREntity::getComponent<C_VoxelSetNew>(entity);
    }

    // Stand-in for seedResidentLocals' group bookkeeping, without the GPU upload.
    static void seedSpans(
        C_DetachedRevoxelizeBuffer &buffer,
        const std::vector<std::pair<std::size_t, std::size_t>> &spans
    ) {
        buffer.groups_.clear();
        for (const auto &[start, count] : spans) {
            RevoxelizeGroupSeed seed{};
            seed.spanStart_ = start;
            seed.spanCount_ = count;
            buffer.groups_.push_back(seed);
        }
    }

    IREntity::EntityManager m_entity_manager;
    IREntity::EntityId m_canvas;
};

class DetachedRevoxelizeFogCarrierTest : public RevoxelizeGroupSeeds {};

TEST_F(DetachedRevoxelizeFogCarrierTest, ManagedPoolReseedsWithNormalizedReservedWords) {
    const C_VoxelSetNew &first = makeSet(ivec3(2, 2, 2));
    C_DetachedRevoxelizeBuffer buffer{};
    seedSpans(buffer, {{0u, static_cast<std::size_t>(pool().getLiveVoxelCount())}});
    buffer.seededContentGeneration_ = pool().getContentGeneration();
    ASSERT_TRUE(
        IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(
            buffer,
            pool(),
            pool().getLiveVoxelCount()
        )
    );

    pool().setFogCarrierPolicy(C_VoxelPool::FogCarrierPolicy::BODY, 255);
    EXPECT_FALSE(
        IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(
            buffer,
            pool(),
            pool().getLiveVoxelCount()
        )
    );
    const std::uint32_t exemptCarrier = IRComponents::VoxelReserved::kFogBody |
                                        (255u << IRComponents::VoxelReserved::kFogBodyFactorShift);
    for (const IRComponents::C_Voxel &voxel : first.voxels_) {
        EXPECT_EQ(voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask, exemptCarrier);
    }

    const C_VoxelSetNew &attached = makeSet(ivec3(2, 2, 2), IRMath::Color{220, 80, 60, 255});
    for (const IRComponents::C_Voxel &voxel : attached.voxels_) {
        EXPECT_EQ(voxel.reserved_ & IRComponents::VoxelReserved::kFogCarrierMask, exemptCarrier);
    }
}

TEST_F(RevoxelizeGroupSeeds, EachSpanIsScannedInItsOwnFrame) {
    const std::size_t evenStart = makeSet(ivec3(4, 4, 4)).voxelStartIdx_;
    const std::size_t oddStart = makeSet(ivec3(3, 3, 3)).voxelStartIdx_;
    std::vector<ivec3> cells;

    const RevoxelizeGroupSeed even =
        IRPrefab::DetachedRevoxelize::detail::scanGroupSpan(pool(), evenStart, 64, cells);
    EXPECT_EQ(cells.size(), 64u);
    // Even-sized centered axes author at half-integers: locals -1.5..1.5 land
    // on cells -1..2 with a -0.5 anchor.
    EXPECT_EQ(even.anchor_, vec3(-0.5f));
    EXPECT_EQ(even.gridMin_, ivec3(-1));
    EXPECT_EQ(even.gridDims_, ivec3(4));
    // Farthest corner |(1.5, 1.5, 1.5)| = 2.598 -> a 7-cell cube.
    EXPECT_EQ(even.destCenter_, 3);
    EXPECT_EQ(even.destSide_, 7);

    const RevoxelizeGroupSeed odd =
        IRPrefab::DetachedRevoxelize::detail::scanGroupSpan(pool(), oddStart, 27, cells);
    EXPECT_EQ(cells.size(), 27u);
    EXPECT_EQ(odd.anchor_, vec3(0.0f));
    EXPECT_EQ(odd.gridMin_, ivec3(-1));
    EXPECT_EQ(odd.gridDims_, ivec3(3));
    EXPECT_EQ(odd.destCenter_, 2);
    EXPECT_EQ(odd.destSide_, 5);
    EXPECT_EQ(odd.spanStart_, oddStart);
    EXPECT_EQ(odd.spanCount_, 27u);
}

TEST_F(RevoxelizeGroupSeeds, UngroupedPoolIsOneImplicitSpanOverTheLivePrefix) {
    makeSet(ivec3(2, 2, 2));
    std::vector<std::pair<std::size_t, std::size_t>> spans;

    IRPrefab::DetachedRevoxelize::detail::collectGroupSpans(pool(), 8, spans);

    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0], (std::pair<std::size_t, std::size_t>{0u, 8u}));
}

TEST_F(RevoxelizeGroupSeeds, GroupedPoolSpansAreExactlyThePostedGroups) {
    const std::size_t first = makeSet(ivec3(2, 2, 2)).voxelStartIdx_;
    const std::size_t second = makeSet(ivec3(3, 3, 3)).voxelStartIdx_;
    pool().postCellGroup(VoxelCellGroup{second, 27});
    std::vector<std::pair<std::size_t, std::size_t>> spans;

    IRPrefab::DetachedRevoxelize::detail::collectGroupSpans(pool(), 35, spans);

    // The unposted first set is not resampled: every set of a grouped pool
    // must be a posted part.
    ASSERT_EQ(spans.size(), 1u);
    EXPECT_EQ(spans[0].first, second);
    EXPECT_NE(spans[0].first, first);

    // Once the last group leaves, a grouped pool resamples nothing rather
    // than falling back to the implicit whole-prefix span over freed slots.
    pool().clearCellGroups();
    IRPrefab::DetachedRevoxelize::detail::collectGroupSpans(pool(), 35, spans);
    EXPECT_TRUE(spans.empty());
}

TEST_F(RevoxelizeGroupSeeds, ReseedGateTracksTheSeededSpanSet) {
    const std::size_t first = makeSet(ivec3(2, 2, 2)).voxelStartIdx_;
    const std::size_t second = makeSet(ivec3(3, 3, 3)).voxelStartIdx_;
    const int liveCount = pool().getLiveVoxelCount();
    C_DetachedRevoxelizeBuffer buffer{};
    const auto seededFromSpans = [&] {
        return IRPrefab::DetachedRevoxelize::detail::seededFromSpans(buffer, pool(), liveCount);
    };

    // An ungrouped pool is seeded from one implicit span over its live prefix.
    EXPECT_FALSE(seededFromSpans());
    seedSpans(buffer, {{0u, static_cast<std::size_t>(liveCount)}});
    EXPECT_TRUE(seededFromSpans());

    pool().postCellGroup(VoxelCellGroup{first, 8});
    pool().postCellGroup(VoxelCellGroup{second, 27});
    EXPECT_FALSE(seededFromSpans());
    seedSpans(buffer, {{first, 8u}, {second, 27u}});
    EXPECT_TRUE(seededFromSpans());

    // A part leaving, and a different part taking a span of another size, both
    // change the set.
    pool().clearCellGroups();
    pool().postCellGroup(VoxelCellGroup{first, 8});
    EXPECT_FALSE(seededFromSpans());
    pool().postCellGroup(VoxelCellGroup{second, 64});
    EXPECT_FALSE(seededFromSpans());
}

// syncResidentBuffers() runs the re-seed gate for every re-voxelize canvas
// every RENDER frame, so a steady canvas must pass it without allocating —
// with its one implicit group and with several hosted groups.
TEST_F(RevoxelizeGroupSeeds, SteadyReseedGateDoesNotAllocate) {
    C_DetachedRevoxelizeBuffer buffer{};
    const auto seedNow = [&] {
        const int liveCount = pool().getLiveVoxelCount();
        std::vector<std::pair<std::size_t, std::size_t>> spans;
        IRPrefab::DetachedRevoxelize::detail::collectGroupSpans(pool(), liveCount, spans);
        seedSpans(buffer, spans);
        buffer.seededContentGeneration_ = pool().getContentGeneration();
        return liveCount;
    };
    // The component lookup allocates; only the gate is counted.
    const auto countedFrames = [&](int liveCount, bool &current) {
        const C_VoxelPool &steadyPool = pool();
        const IRTest::AllocationCounter counter;
        current = true;
        for (int frame = 0; frame < 8; ++frame) {
            current =
                current &&
                IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, steadyPool, liveCount);
        }
        return counter.allocations();
    };

    makeSet(ivec3(2, 2, 2));
    bool current = false;
    const std::size_t implicitGroup = countedFrames(seedNow(), current);
    EXPECT_TRUE(current);
    EXPECT_EQ(buffer.groups_.size(), 1u);
    EXPECT_EQ(implicitGroup, 0u);

    for (const ivec3 size : {ivec3(2, 2, 2), ivec3(3, 3, 3), ivec3(4, 4, 4)}) {
        const C_VoxelSetNew &set = makeSet(size);
        pool().postCellGroup(
            VoxelCellGroup{set.voxelStartIdx_, static_cast<std::size_t>(size.x * size.y * size.z)}
        );
    }
    const std::size_t hostedGroups = countedFrames(seedNow(), current);
    EXPECT_TRUE(current);
    EXPECT_EQ(buffer.groups_.size(), 3u);
    EXPECT_EQ(hostedGroups, 0u);
}

// A part replaced by a same-sized one reuses the freed span: the span set and
// live count are unchanged, but the seeded grid would still hold the departed
// part's voxels.
TEST_F(RevoxelizeGroupSeeds, ReseedGateCatchesASameSizedSetReusingAFreedSpan) {
    makeSet(ivec3(2, 2, 2));
    const IREntity::EntityId departing = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 3, 3), kColor, EntityAnchor::CENTER, m_canvas}
    );
    const std::size_t start = IREntity::getComponent<C_VoxelSetNew>(departing).voxelStartIdx_;
    pool().postCellGroup(VoxelCellGroup{start, 27});
    const int liveCount = pool().getLiveVoxelCount();

    C_DetachedRevoxelizeBuffer buffer{};
    seedSpans(buffer, {{start, 27u}});
    buffer.seededContentGeneration_ = pool().getContentGeneration();
    ASSERT_TRUE(IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, pool(), liveCount));

    IREntity::getEntityManager().destroyEntity(departing);
    const C_VoxelSetNew &arriving = makeSet(ivec3(3, 3, 3), IRMath::Color{240, 60, 30, 255});
    ASSERT_EQ(arriving.voxelStartIdx_, start) << "the freed span must be reused";
    pool().postCellGroup(VoxelCellGroup{start, 27});
    ASSERT_EQ(pool().getLiveVoxelCount(), liveCount);
    ASSERT_TRUE(IRPrefab::DetachedRevoxelize::detail::seededFromSpans(buffer, pool(), liveCount));

    EXPECT_FALSE(IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, pool(), liveCount));
}

// An in-place edit of a hosted part allocates nothing and keeps its span, but
// the seeded grid still holds the old records. Every record mutator must
// invalidate the seed, a hidden set's included; a per-frame position upload
// must not, or a moving part would re-seed every frame.
TEST_F(RevoxelizeGroupSeeds, ReseedGateCatchesAnInPlaceEditOfAHostedPart) {
    const IREntity::EntityId part = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 3, 3), kColor, EntityAnchor::CENTER, m_canvas}
    );
    const std::size_t start = IREntity::getComponent<C_VoxelSetNew>(part).voxelStartIdx_;
    pool().postCellGroup(VoxelCellGroup{start, 27});
    const int liveCount = pool().getLiveVoxelCount();

    C_DetachedRevoxelizeBuffer buffer{};
    seedSpans(buffer, {{start, 27u}});
    const auto seedNow = [&] {
        buffer.seededContentGeneration_ = pool().getContentGeneration();
        ASSERT_TRUE(IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, pool(), liveCount));
    };
    const auto set = [&]() -> C_VoxelSetNew & {
        return IREntity::getComponent<C_VoxelSetNew>(part);
    };
    const auto expectStale = [&](const char *edit) {
        EXPECT_FALSE(IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, pool(), liveCount))
            << edit;
    };

    seedNow();
    pool().queuePositionRange(start, 27);
    EXPECT_TRUE(IRPrefab::DetachedRevoxelize::detail::seedIsCurrent(buffer, pool(), liveCount))
        << "position upload";

    seedNow();
    set().editVoxels([](int, IRComponents::C_Voxel &voxel, vec3) {
        voxel.color_ = IRMath::Color{240, 60, 30, voxel.color_.alpha_};
    });
    expectStale("editVoxels recolor");

    seedNow();
    set().carve([](vec3 localPos) { return localPos.z > 0.0f; });
    expectStale("carve");

    seedNow();
    set().changeVoxelColor(ivec3(1, 1, 1), IRMath::Color{10, 200, 10, 255});
    expectStale("changeVoxelColor");

    seedNow();
    set().changeVoxelPriority(ivec3(1, 1, 1), 2);
    expectStale("changeVoxelPriority");

    seedNow();
    set().changeVoxelPriorityAll(1);
    expectStale("changeVoxelPriorityAll");

    set().visible_ = false;
    seedNow();
    set().changeVoxelColorAll(IRMath::Color{30, 30, 200, 255});
    expectStale("changeVoxelColorAll while hidden");

    seedNow();
    set().activateAll();
    expectStale("activateAll while hidden");
}

} // namespace
