#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/entity_anchor.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_rebuild_detached_voxels.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include <irreden/voxel/voxel_pool_api.hpp>

// `C_VoxelPool`'s derived cull caches depend on the allocated prefix,
// per-voxel alpha, and per-voxel global position. Every producer that rewrites
// one of those inputs invalidates the affected chunks.
//
// The cardinal and world-AABB consumers have independent pending state. A
// pending range is admitted conservatively until its owning cache rebuilds;
// clean chunks remain cached. These tests assert derived bounds and visibility;
// static-pool and locality cases also reject unconditional full recomputes.

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::EntityAnchor;
using IRMath::CardinalIndex;
using IRMath::Color;
using IRMath::IsoBounds2D;
using IRMath::ivec3;
using IRMath::vec2;
using IRMath::vec3;

constexpr int kChunk = IRRender::kVoxelChunkSize; // 256 slots
// The partial-chunk case below allocates 300 slots expecting a full chunk
// plus a 44-slot tail; that shape is only interesting at this granularity.
static_assert(kChunk == 256, "these fixtures are sized against a 256-slot chunk");

void notifyPositionsRewritten(C_VoxelPool &pool, std::size_t start, std::size_t count) {
    pool.markCullBoundsDirty(start, count);
}

void seedSlot(C_VoxelPool &pool, std::size_t slot, vec3 worldPos, bool live) {
    pool.getPositionGlobals()[slot].pos_ = worldPos;
    pool.getColors()[slot].color_ = live ? Color{200, 120, 60, 255} : Color{0, 0, 0, 0};
}

IsoBounds2D viewportAround(vec3 worldPos, float halfExtent = 0.25f) {
    const vec2 iso = IRMath::pos3DtoPos2DIso(worldPos);
    return IsoBounds2D{iso - vec2(halfExtent), iso + vec2(halfExtent)};
}

// The from-scratch oracle deliberately bypasses incremental invalidation and
// includes `minDepth_`, so a merely shifted but otherwise stale bound cannot pass.
IRComponents::ChunkBounds oracleChunkBounds(const C_VoxelPool &pool, int chunk, CardinalIndex ci) {
    IRComponents::ChunkBounds bounds;
    const int begin = chunk * kChunk;
    const int end = IRMath::min(begin + kChunk, pool.getLiveVoxelCount());
    for (int i = begin; i < end; ++i) {
        if (pool.getColors()[i].color_.alpha_ == 0) {
            continue;
        }
        vec3 pos = pool.getPositionGlobals()[i].pos_;
        if (ci != CardinalIndex::k0) {
            pos = IRMath::rotateCardinalZ(pos, ci);
        }
        bounds.expand(IRMath::pos3DtoPos2DIso(pos));
        bounds.minDepth_ =
            IRMath::min(bounds.minDepth_, static_cast<float>(IRMath::pos3DtoDistance(pos)));
    }
    return bounds;
}

void expectBoundsMatchOracle(C_VoxelPool &pool, int chunk, CardinalIndex ci) {
    const IRComponents::ChunkBounds want = oracleChunkBounds(pool, chunk, ci);
    const IRComponents::ChunkBounds &got = pool.getChunkBounds()[static_cast<std::size_t>(chunk)];
    EXPECT_FLOAT_EQ(got.isoMin_.x, want.isoMin_.x) << "chunk " << chunk << " isoMin_.x";
    EXPECT_FLOAT_EQ(got.isoMin_.y, want.isoMin_.y) << "chunk " << chunk << " isoMin_.y";
    EXPECT_FLOAT_EQ(got.isoMax_.x, want.isoMax_.x) << "chunk " << chunk << " isoMax_.x";
    EXPECT_FLOAT_EQ(got.isoMax_.y, want.isoMax_.y) << "chunk " << chunk << " isoMax_.y";
    EXPECT_FLOAT_EQ(got.minDepth_, want.minDepth_) << "chunk " << chunk << " minDepth_";
}

C_VoxelPool makeTwoChunkPool(vec3 nearPose = vec3(2, 2, 2), vec3 farPose = vec3(80, 80, 0)) {
    C_VoxelPool pool(ivec3(16, 16, 4)); // 1024 slots = 4 chunks of capacity
    pool.allocateVoxels(2 * kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), nearPose, true);
        seedSlot(pool, static_cast<std::size_t>(kChunk + i), farPose, true);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(2 * kChunk));
    return pool;
}

class ChunkBoundsEvictionTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

TEST_F(ChunkBoundsEvictionTest, ContinuousYawViewportHasNoDepthCutoff) {
    C_VoxelPool pool = makeTwoChunkPool();
    const IsoBounds2D viewport{vec2(-4.0f), vec2(4.0f)};
    for (int degrees = 0; degrees < 360; degrees += 5) {
        const float yaw = IRMath::kTwoPi * static_cast<float>(degrees) / 360.0f;
        const auto viewToWorld = IRMath::rotate(IRMath::mat4(1.0f), yaw, vec3(0, 0, 1));
        for (float depth : {-10000.0f, -100.0f, 0.0f, 100.0f, 10000.0f}) {
            SCOPED_TRACE(::testing::Message() << "yaw=" << degrees << " depth=" << depth);
            const vec3 onRay = vec3(viewToWorld * IRMath::vec4(vec3(depth), 1.0f));
            const vec3 offRay =
                vec3(viewToWorld * IRMath::vec4(vec3(depth) + vec3(40, 0, 0), 1.0f));
            for (int i = 0; i < kChunk; ++i) {
                // Continuous rasterization uses the cell center minus half a cell.
                seedSlot(pool, i, onRay + vec3(0.5f), true);
                seedSlot(pool, kChunk + i, offRay + vec3(0.5f), true);
            }
            pool.markCullBoundsDirty(0, 2 * kChunk);
            const auto &mask =
                IRSystem::buildChunkVisibilityMask(pool, viewport, CardinalIndex::k0, true, yaw);
            ASSERT_EQ(mask.size(), 2u);
            EXPECT_EQ(mask[0], 1u);
            EXPECT_EQ(mask[1], 0u);
        }
    }
}

TEST_F(ChunkBoundsEvictionTest, StaticChunkReentersViewportAcrossFullYawTurn) {
    C_VoxelPool pool = makeTwoChunkPool(vec3(80.5f, 0.5f, 0.5f));
    const vec2 target = IRMath::pos3DtoPos2DIsoYawed(vec3(80, 0, 0), IRMath::kHalfPi);
    const IsoBounds2D viewport{target - vec2(4), target + vec2(4)};
    int visibleCount = 0;
    int hiddenCount = 0;
    for (int degrees = 0; degrees <= 360; ++degrees) {
        const float yaw = IRMath::kTwoPi * static_cast<float>(degrees) / 360.0f;
        const auto &mask =
            IRSystem::buildChunkVisibilityMask(pool, viewport, CardinalIndex::k0, true, yaw);
        const vec2 projected = IRMath::pos3DtoPos2DIsoYawed(vec3(80, 0, 0), yaw);
        const bool expected = viewport.contains(projected);
        EXPECT_EQ(mask[0], expected ? 1u : 0u) << "yaw=" << degrees;
        visibleCount += expected ? 1 : 0;
        hiddenCount += expected ? 0 : 1;
    }
    EXPECT_GT(visibleCount, 0);
    EXPECT_GT(hiddenCount, 0);
    const auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.worldChunks_, 2u);
    EXPECT_EQ(counters.worldSlots_, 2u * kChunk);
}

TEST_F(ChunkBoundsEvictionTest, CardinalPositionRewrite) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(140, -60, 7);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds before = pool.getChunkBounds()[0];
    ASSERT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(nearPose)));

    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    notifyPositionsRewritten(pool, 0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    const IRComponents::ChunkBounds &after = pool.getChunkBounds()[0];
    EXPECT_NE(after.isoMin_.x, before.isoMin_.x);
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(movedPose)))
        << "chunk 0 must be visible at the pose its voxels actually occupy";
    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(nearPose)))
        << "chunk 0 must not still report visible at the pose it left";

    expectBoundsMatchOracle(pool, 1, CardinalIndex::k0);
}

TEST_F(ChunkBoundsEvictionTest, CardinalPositionRewriteMovesVisibilityMask) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(140, -60, 7);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);

    const IsoBounds2D atNear = viewportAround(nearPose);
    EXPECT_EQ(IRSystem::buildChunkVisibilityMask(pool, atNear)[0], 1u);

    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    notifyPositionsRewritten(pool, 0, static_cast<std::size_t>(kChunk));

    EXPECT_EQ(IRSystem::buildChunkVisibilityMask(pool, atNear)[0], 0u)
        << "the mask must stop lighting a chunk whose voxels left the viewport";
    EXPECT_EQ(IRSystem::buildChunkVisibilityMask(pool, viewportAround(movedPose))[0], 1u)
        << "the mask must light the chunk at the pose its voxels moved to";
}

TEST_F(ChunkBoundsEvictionTest, AlphaOnlyPoseSwap) {
    const vec3 poseA(2, 2, 2);
    const vec3 poseB(90, 40, 3);
    C_VoxelPool pool(ivec3(16, 16, 4));
    pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk / 2; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), poseA, true);
        seedSlot(pool, static_cast<std::size_t>(kChunk / 2 + i), poseB, false);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    ASSERT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(poseA)));
    ASSERT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(poseB)));

    for (int i = 0; i < kChunk / 2; ++i) {
        pool.getColors()[static_cast<std::size_t>(i)].color_ = Color{0, 0, 0, 0};
        pool.getColors()[static_cast<std::size_t>(kChunk / 2 + i)].color_ =
            Color{200, 120, 60, 255};
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(poseB)))
        << "the chunk must cover the pose that just became live";
    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(poseA)))
        << "the chunk must shrink off the pose that just went dark";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);

    for (int i = 0; i < kChunk / 2; ++i) {
        pool.getColors()[static_cast<std::size_t>(i)].color_ = Color{200, 120, 60, 255};
        pool.getColors()[static_cast<std::size_t>(kChunk / 2 + i)].color_ = Color{0, 0, 0, 0};
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(poseA)));
    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(poseB)));
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

TEST_F(ChunkBoundsEvictionTest, SingleBitActivationGrowsChunkBounds) {
    const vec3 nearPose(2, 2, 2);
    const vec3 outlier(400, 25, 11);
    C_VoxelPool pool(ivec3(16, 16, 4));
    pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), nearPose, i != 7);
    }
    seedSlot(pool, 7, outlier, false);
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    ASSERT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(outlier)));

    pool.getColors()[7].color_ = Color{200, 120, 60, 255};
    pool.setActiveBit(7);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(outlier)));
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

TEST_F(ChunkBoundsEvictionTest, HiddenEditThenReveal) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 4)});
    const IREntity::EntityId object = IREntity::createEntity(
        C_VoxelSetNew{ivec3(4, 4, 4), Color{200, 100, 50, 255}, true, canvas}
    );
    C_VoxelSetNew &set = IREntity::getComponent<C_VoxelSetNew>(object);
    C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    ASSERT_EQ(set.numVoxels_, 64);

    const vec3 hidingPose(3, 3, 1);
    const vec3 revealedPose(220, -14, 5);
    for (int i = 0; i < set.numVoxels_; ++i) {
        pool.getPositionGlobals()[set.voxelStartIdx_ + static_cast<std::size_t>(i)].pos_ =
            hidingPose;
    }
    pool.getPositionGlobals()[set.voxelStartIdx_].pos_ = revealedPose;
    set.deactivateAll();
    set.changeVoxelColor(ivec3(0, 0, 0), Color{10, 250, 10, 255}); // slot 0 only
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    ASSERT_TRUE(pool.isRangeVisible(set.voxelStartIdx_, 64, viewportAround(revealedPose)));

    set.visible_ = false;
    IRPrefab::VoxelPool::markRangeInactive(set.voxelStartIdx_, set.numVoxels_, canvas);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    const vec3 editedPose(-310, 48, 2);
    pool.getPositionGlobals()[set.voxelStartIdx_ + 5].pos_ = editedPose;
    set.editVoxels([](int index, C_Voxel &voxel, vec3) {
        if (index == 0) {
            voxel.deactivate();
        } else if (index == 5) {
            voxel.color_ = Color{10, 250, 10, 255};
        }
    });

    set.visible_ = true;
    IRPrefab::VoxelPool::resyncRangeFromColors(set.voxelStartIdx_, set.numVoxels_, canvas);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(set.voxelStartIdx_, 64, viewportAround(editedPose)))
        << "an edit applied while hidden must reach the cull bounds on reveal";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

TEST_F(ChunkBoundsEvictionTest, StaticPoolStillCachesCardinalBounds) {
    C_VoxelPool pool = makeTwoChunkPool();
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds before = pool.getChunkBounds()[0];

    // Rewrites without notification must not bypass the cache contract.
    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = vec3(999, 999, 9);
    }
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    const IRComponents::ChunkBounds &after = pool.getChunkBounds()[0];
    EXPECT_FLOAT_EQ(after.isoMin_.x, before.isoMin_.x);
    EXPECT_FLOAT_EQ(after.isoMax_.y, before.isoMax_.y);
    EXPECT_FLOAT_EQ(after.minDepth_, before.minDepth_);
}

TEST_F(ChunkBoundsEvictionTest, CacheLocalityOneNotifiedChunkVisitsOneChunk) {
    C_VoxelPool pool(ivec3(64, 64, 64)); // 262,144 slots
    pool.allocateVoxels(1024 * kChunk);
    for (int i = 0; i < 1024 * kChunk; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), vec3(i % 64, (i / 64) % 64, i / 4096), true);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(1024 * kChunk));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1024u) << "the seeding rebuild covers every chunk";

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 0u);
    EXPECT_EQ(counters.cardinalSlots_, 0u);

    pool.markCullBoundsDirty(0, 1);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(kChunk));

    // Duplicate and overlapping notifications coalesce into one chunk visit.
    pool.markCullBoundsDirty(0, 1);
    pool.markCullBoundsDirty(0, 1);
    pool.markCullBoundsDirty(0, static_cast<std::size_t>(kChunk));
    pool.markCullBoundsDirty(3, 17);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(kChunk));

    pool.markCullBoundsDirty(500, 0);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 0u);

    // This range crosses both a mask word and a chunk boundary.
    pool.markCullBoundsDirty(kChunk - 3, 70); // spans chunks 0 and 1
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 2u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(2 * kChunk));

    // Bounds are projected under the cardinal index, so an index change
    // invalidates every chunk.
    pool.rebuildChunkBounds(CardinalIndex::k90, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1024u);
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k90);
    expectBoundsMatchOracle(pool, 1023, CardinalIndex::k90);
}

TEST_F(ChunkBoundsEvictionTest, PartialAndEmptyChunksVisitOnlyAllocatedSlots) {
    C_VoxelPool pool(ivec3(16, 16, 4)); // 1,024 slots of capacity
    pool.allocateVoxels(300);           // chunk 0 full, chunk 1 holds 44 slots
    for (int i = 0; i < 300; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), vec3(i % 8, 1, 2), true);
    }
    pool.resyncActiveMaskFromColors(0, 300);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 2u);
    EXPECT_EQ(counters.cardinalSlots_, 300u) << "the partial tail chunk stops at the prefix";

    for (int i = kChunk; i < 300; ++i) {
        pool.getColors()[static_cast<std::size_t>(i)].color_ = Color{0, 0, 0, 0};
    }
    pool.resyncActiveMaskFromColors(kChunk, 44);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds &tail = pool.getChunkBounds()[1];
    EXPECT_GT(tail.isoMin_.x, tail.isoMax_.x) << "an empty chunk keeps the inverted sentinel";
    EXPECT_EQ(IRSystem::buildChunkVisibilityMask(pool, viewportAround(vec3(3, 1, 2)))[1], 0u);
}

TEST_F(ChunkBoundsEvictionTest, AllocationReuseAndDeallocationInvalidate) {
    C_VoxelPool pool(ivec3(16, 16, 4));
    const auto first = pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, first.startIndex_ + static_cast<std::size_t>(i), vec3(2, 2, 2), true);
    }
    pool.resyncActiveMaskFromColors(first.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    ASSERT_TRUE(pool.isRangeVisible(first.startIndex_, kChunk, viewportAround(vec3(2, 2, 2))));

    const auto second = pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, second.startIndex_ + static_cast<std::size_t>(i), vec3(60, 12, 4), true);
    }
    pool.resyncActiveMaskFromColors(second.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(second.startIndex_, kChunk, viewportAround(vec3(60, 12, 4))));

    pool.deallocateVoxels(second.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_FALSE(pool.isRangeVisible(second.startIndex_, kChunk, viewportAround(vec3(60, 12, 4))));

    const auto reused = pool.allocateVoxels(kChunk);
    EXPECT_EQ(reused.startIndex_, second.startIndex_) << "the free span is reused, not grown past";
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, reused.startIndex_ + static_cast<std::size_t>(i), vec3(-40, 33, 6), true);
    }
    pool.resyncActiveMaskFromColors(reused.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(reused.startIndex_, kChunk, viewportAround(vec3(-40, 33, 6))));
}

TEST_F(ChunkBoundsEvictionTest, SharedChunkRecomputesFromEverySlot) {
    C_VoxelPool pool(ivec3(16, 16, 4));
    pool.allocateVoxels(kChunk);
    const vec3 setAPose(2, 2, 2);
    const vec3 setBPose(70, 9, 1);
    for (int i = 0; i < kChunk / 2; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), setAPose, true);
        seedSlot(pool, static_cast<std::size_t>(kChunk / 2 + i), setBPose, true);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    const vec3 setAMoved(-120, 55, 8);
    for (int i = 0; i < kChunk / 2; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = setAMoved;
    }
    notifyPositionsRewritten(pool, 0, static_cast<std::size_t>(kChunk / 2));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(setAMoved)));
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(setBPose)))
        << "the untouched set sharing this chunk must survive its neighbour's move";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

TEST_F(ChunkBoundsEvictionTest, CardinalAndWorldConsumersArePendingIndependently) {
    C_VoxelPool pool = makeTwoChunkPool();
    pool.rebuildChunkBounds(CardinalIndex::k0, true, 0.35f); // yaw branch → world cache
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f); // cardinal cache
    (void)pool.consumeCullRebuildCounters();

    pool.markCullBoundsDirty(0, 1);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.worldChunks_, 0u) << "the cardinal pass must not touch the world cache";

    pool.rebuildChunkBounds(CardinalIndex::k0, true, 0.35f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.worldChunks_, 1u)
        << "the cardinal consumer must not have cleared the world cache's pending work";
}

TEST_F(ChunkBoundsEvictionTest, CardinalYawCardinalRoundTripAndAllCardinals) {
    C_VoxelPool pool = makeTwoChunkPool();
    for (const CardinalIndex ci :
         {CardinalIndex::k0, CardinalIndex::k90, CardinalIndex::k180, CardinalIndex::k270}) {
        pool.rebuildChunkBounds(ci, false, 0.0f);
        expectBoundsMatchOracle(pool, 0, ci);
        expectBoundsMatchOracle(pool, 1, ci);
    }

    pool.rebuildChunkBounds(CardinalIndex::k270, true, 0.4f); // a yawing frame
    (void)pool.consumeCullRebuildCounters();
    pool.rebuildChunkBounds(CardinalIndex::k270, false, 0.0f);
    const auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 2u)
        << "returning from continuous yaw must re-derive every cardinal chunk";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k270);
}

TEST_F(ChunkBoundsEvictionTest, EditsAfterUploadQueueSaturationStillInvalidate) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(150, -70, 9);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    for (std::size_t i = 0; i < C_VoxelPool::kMaxPendingPositionRanges + 16; ++i) {
        pool.queuePositionRange(static_cast<std::size_t>(kChunk), 1);
    }
    ASSERT_EQ(pool.getPendingPositionRanges().size(), C_VoxelPool::kMaxPendingPositionRanges);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    pool.queuePositionRange(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(movedPose)))
        << "a saturated upload queue must not swallow the cull invalidation";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

// Cull invalidation and binding-5 upload are independent channels. A
// GPU-transform-indirected set can owe a cull-cache recompute while the GPU
// prepass owns its position upload, and grid identity frames need invalidation
// even when no restore upload is required.
TEST_F(ChunkBoundsEvictionTest, BoundsInvalidationIsIndependentOfUploadQueue) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(210, -30, 4);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    pool.clearPendingPositionRanges();
    (void)pool.consumeCullRebuildCounters();

    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    pool.markCullBoundsDirty(0, static_cast<std::size_t>(kChunk));
    EXPECT_TRUE(pool.getPendingPositionRanges().empty())
        << "invalidation must not enqueue a binding-5 upload";

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u) << "the bounds must still re-derive";
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(movedPose)));
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

// UPDATE movers query visibility before the render pipeline rebuilds bounds, so
// a pending range must be admitted conservatively rather than answered from a
// stale cache.
TEST_F(ChunkBoundsEvictionTest, PendingInvalidationAdmitsUpdateWork) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(170, -80, 10);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    const IsoBounds2D atMoved = viewportAround(movedPose);
    ASSERT_FALSE(pool.isRangeVisible(0, kChunk, atMoved));

    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    notifyPositionsRewritten(pool, 0, static_cast<std::size_t>(kChunk));

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, atMoved));
    EXPECT_FALSE(pool.isRangeVisible(kChunk, kChunk, atMoved));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, atMoved));
    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(nearPose)))
        << "once rebuilt the query is the ordinary overlap answer again";
}

// A static re-voxelize bound owns every chunk's rotation-independent cardinal
// answer. Its early-return rebuild must consume the cardinal pending bits;
// otherwise conservative admission makes every detached range remain visible.

TEST_F(ChunkBoundsEvictionTest, StaticBoundPoolCullsAfterAllocationArmedThePendingBits) {
    C_VoxelPool pool(ivec3(16, 16, 4));
    pool.allocateVoxels(kChunk); // arms kCullPendingCardinal for chunk 0
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), vec3(0, 0, 0), true);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.setStaticReVoxelizeBound(vec3(2, 2, 2));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(vec3(500, 500, 0))))
        << "allocation's pending bits must not outlive the static bound's rebuild";
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(vec3(0, 0, 0))));
}

class DetachedCullBoundTest : public ::testing::Test {
  protected:
    // System registration reaches the EntityManager, so it must outlive the
    // SystemManager.
    IREntity::EntityManager m_entityManager;
    IRSystem::SystemManager m_systemManager;

    DetachedCullBoundTest()
        : m_entityManager{}
        , m_systemManager{} {
        m_systemManager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::REBUILD_DETACHED_VOXELS>()}
        );
    }

    IREntity::EntityId makeDetachedCanvas(ivec3 size = ivec3(4, 4, 4)) {
        const IREntity::EntityId canvas =
            IREntity::createEntity(C_VoxelPool{ivec3(16, 16, 16)}, C_CanvasLocalRotation{});
        auto &rotation = IREntity::getComponent<C_CanvasLocalRotation>(canvas);
        rotation.rotation_ = IRMath::vec4{0.0f, 0.0f, 0.0f, 1.0f}; // identity, not the sentinel
        rotation.reVoxelize_ = true;
        IREntity::createEntity(
            C_VoxelSetNew{size, Color{200, 120, 60, 255}, EntityAnchor::CENTER, canvas}
        );
        m_systemManager.executePipeline(IRTime::Events::UPDATE); // seeds the bound
        return canvas;
    }
};

TEST_F(DetachedCullBoundTest, OffViewportDetachedRangeIsCulledAfterStaticRebuild) {
    const IREntity::EntityId canvas = makeDetachedCanvas();
    C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    ASSERT_TRUE(pool.hasStaticReVoxelizeBound()) << "the tick must have reached the seed";
    const std::size_t count = static_cast<std::size_t>(pool.getLiveVoxelCount());
    ASSERT_GT(count, 0u);

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_FALSE(pool.isRangeVisible(0, count, viewportAround(vec3(400, -400, 0))))
        << "a detached pool whose bound was just rebuilt must answer the ordinary "
           "overlap question, not admit unconditionally";
    EXPECT_TRUE(pool.isRangeVisible(0, count, viewportAround(vec3(0, 0, 0))))
        << "the conservative origin-centered bound must still cover the solid";
}

TEST_F(DetachedCullBoundTest, DetachedEditAdmitsOnceThenReturnsToTheOverlapAnswer) {
    const IREntity::EntityId canvas = makeDetachedCanvas();
    C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    const std::size_t count = static_cast<std::size_t>(pool.getLiveVoxelCount());
    const IsoBounds2D offViewport = viewportAround(vec3(400, -400, 0));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    ASSERT_FALSE(pool.isRangeVisible(0, count, offViewport));

    pool.markCullBoundsDirty(0, count);
    EXPECT_TRUE(pool.isRangeVisible(0, count, offViewport))
        << "a chunk that owes a recompute is admitted, on this branch too";

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_FALSE(pool.isRangeVisible(0, count, offViewport))
        << "the static rebuild must consume what the edit armed";
}

} // namespace

TEST(VoxelUpdateSpans, CoalescesOnlyContiguousRangesWithIdenticalOwnership) {
    using Update = IRSystem::System<IRSystem::UPDATE_VOXEL_SET_CHILDREN>;
    C_VoxelPool first(ivec3(16, 1, 1));
    C_VoxelPool second(ivec3(16, 1, 1));
    std::vector<Update::PendingRange> ranges;
    Update::appendPendingRange(ranges, {&first, 0, 2, true});
    Update::appendPendingRange(ranges, {&first, 2, 3, true});
    Update::appendPendingRange(ranges, {&first, 6, 1, true});
    Update::appendPendingRange(ranges, {&first, 7, 1, false});
    Update::appendPendingRange(ranges, {&second, 8, 1, false});
    Update::appendPendingRange(ranges, {&second, 9, 2, false});
    ASSERT_EQ(ranges.size(), 4u);
    EXPECT_EQ(ranges[0].pool_, &first);
    EXPECT_EQ(ranges[0].startIdx_, 0u);
    EXPECT_EQ(ranges[0].count_, 5u);
    EXPECT_TRUE(ranges[0].upload_);
    EXPECT_EQ(ranges[1].startIdx_, 6u);
    EXPECT_EQ(ranges[1].count_, 1u);
    EXPECT_TRUE(ranges[1].upload_);
    EXPECT_EQ(ranges[2].pool_, &first);
    EXPECT_EQ(ranges[2].startIdx_, 7u);
    EXPECT_EQ(ranges[2].count_, 1u);
    EXPECT_FALSE(ranges[2].upload_);
    EXPECT_EQ(ranges[3].pool_, &second);
    EXPECT_EQ(ranges[3].startIdx_, 8u);
    EXPECT_EQ(ranges[3].count_, 3u);
    EXPECT_FALSE(ranges[3].upload_);
}

TEST(VoxelUpdateSpans, EndTickInvalidatesGpuSpansWithoutUploadingThem) {
    using Update = IRSystem::System<IRSystem::UPDATE_VOXEL_SET_CHILDREN>;
    C_VoxelPool pool(ivec3(kChunk * 2, 1, 1));
    pool.allocateVoxels(kChunk * 2);
    seedSlot(pool, 0, vec3(0), true);
    seedSlot(pool, kChunk, vec3(10), true);
    pool.rebuildChunkBounds(CardinalIndex::k0);
    pool.clearPendingPositionRanges();
    seedSlot(pool, 0, vec3(30), true);
    seedSlot(pool, kChunk, vec3(40), true);
    Update update;
    update.pendingByWorker_.resize(2);
    Update::appendPendingRange(update.pendingByWorker_[0], {&pool, 0, 1, true});
    Update::appendPendingRange(update.pendingByWorker_[1], {&pool, kChunk, 1, false});
    update.endTick();
    ASSERT_EQ(pool.getPendingPositionRanges().size(), 1u);
    EXPECT_EQ(pool.getPendingPositionRanges()[0].first, 0u);
    EXPECT_EQ(pool.getPendingPositionRanges()[0].second, 1u);
    pool.rebuildChunkBounds(CardinalIndex::k0);
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
    expectBoundsMatchOracle(pool, 1, CardinalIndex::k0);
}
