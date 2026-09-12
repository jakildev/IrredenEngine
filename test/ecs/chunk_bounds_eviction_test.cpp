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
#include <irreden/voxel/voxel_pool_api.hpp>

// #2830 regression guard: `C_VoxelPool`'s two derived cull caches must
// re-derive on EVERY input their recompute reads, at chunk granularity.
//
// The defect: `rebuildChunkBounds`'s cardinal branch reads three inputs
// (allocated prefix length, per-voxel alpha, per-voxel global position) but was
// gated on a flag only allocate/deallocate and a yaw frame ever set. Every
// producer that rewrote positions or alpha IN PLACE evicted only the sibling
// world-AABB cache, so on a cardinal camera — the default, `residualYaw_ == 0`
// — the chunk iso bounds froze at the last alloc/dealloc. Both consumers
// (`buildChunkVisibilityMask` → the chunk-visibility SSBO, and `isRangeVisible`
// → the UPDATE movers' cull gate) then dropped live geometry, and the drop was
// self-latching: the movers stopped advancing the CPU mirror, so the frozen
// bounds never became wrong in a way that recovered.
//
// These tests run headlessly against a real `C_VoxelPool` (no RenderManager)
// and assert the POOL-DERIVED result — chunk bounds, the visibility mask, and
// `isRangeVisible` — rather than voxel alpha or active-mask bits. That
// distinction is the point: the shipping instance of this bug (the #766 bird)
// passed 28/28 CPU occupancy assertions while rendering a blend of two poses,
// because the alpha was correct and only the derived cull state was stale.
//
// The cache is still a cache. `StaticPoolStillCachesCardinalBounds` and the
// `CacheLocality*` cases are the negative controls that fail if the fix is the
// degenerate "recompute unconditionally" one.

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

// The production notification a system performs after rewriting pool positions
// in place. `UPDATE_VOXEL_SET_CHILDREN::endTick` and all three
// `REBUILD_GRID_VOXELS` arms call exactly this.
//
// POSITIVE CONTROL: at 1750ef4ae this line reads `pool.markChunkWorldBoundsDirty()`
// — the only evictor any producer called on master — and the position/alpha
// cases below fail. See the PR body for the recorded failure output.
void notifyPositionsRewritten(C_VoxelPool &pool, std::size_t start, std::size_t count) {
    pool.markCullBoundsDirty(start, count);
}

// Seed one slot: a world position plus an alpha, through the same surfaces the
// live pipeline writes (the global mirror `UPDATE_VOXEL_SET_CHILDREN` fills and
// the color span every producer authors).
void seedSlot(C_VoxelPool &pool, std::size_t slot, vec3 worldPos, bool live) {
    pool.getPositionGlobals()[slot].pos_ = worldPos;
    pool.getColors()[slot].color_ = live ? Color{200, 120, 60, 255} : Color{0, 0, 0, 0};
}

// A viewport tight around one world position's iso projection.
IsoBounds2D viewportAround(vec3 worldPos, float halfExtent = 0.25f) {
    const vec2 iso = IRMath::pos3DtoPos2DIso(worldPos);
    return IsoBounds2D{iso - vec2(halfExtent), iso + vec2(halfExtent)};
}

// A forced-rebuild oracle: the bounds a from-scratch whole-pool recompute would
// produce for the same pose. Used to prove the incremental path lands on the
// exact same values (not merely "moved"), including `minDepth_`.
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

// Two-chunk pool: slots [0, 256) near the origin, slots [256, 512) far away, all
// live. Both chunks are fully allocated, so nothing below reallocates.
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

// ---------------------------------------------------------------------------
// AC1 — the position edge.
// ---------------------------------------------------------------------------

// A cardinal camera, no alloc/dealloc, positions rewritten in place: the chunk
// bounds must follow the voxels. This is the issue's failure scenario reduced to
// its smallest honest form.
TEST_F(ChunkBoundsEvictionTest, CardinalPositionRewrite) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(140, -60, 7);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds before = pool.getChunkBounds()[0];
    ASSERT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(nearPose)));

    // In-place rewrite of chunk 0's globals — no realloc, no alpha change.
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

    // The untouched chunk keeps its bounds — the notification was range-scoped.
    expectBoundsMatchOracle(pool, 1, CardinalIndex::k0);
}

// The same edge through the consumer the render pipeline actually reads.
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

// ---------------------------------------------------------------------------
// AC1/AC2 — the alpha edge. No position write, no realloc, no manual evictor:
// the only production call is the active-mask resync every set-level edit runs.
// ---------------------------------------------------------------------------

TEST_F(ChunkBoundsEvictionTest, AlphaOnlyPoseSwap) {
    const vec3 poseA(2, 2, 2);
    const vec3 poseB(90, 40, 3);
    C_VoxelPool pool(ivec3(16, 16, 4));
    pool.allocateVoxels(kChunk);
    // Half the chunk at pose A and live, half at pose B and dark.
    for (int i = 0; i < kChunk / 2; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), poseA, true);
        seedSlot(pool, static_cast<std::size_t>(kChunk / 2 + i), poseB, false);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    ASSERT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(poseA)));
    ASSERT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(poseB)));

    // Frame step: swap which half is live. Alpha only — the #766 bird's edit.
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

    // And back again — the edge fires in both directions, not just on growth.
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

// A single-bit activation is the narrowest form of the same edge: one voxel,
// far from the chunk's live mass, must grow the bound around itself.
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

// ---------------------------------------------------------------------------
// AC1 — hidden edits. `C_VoxelSetNew::visible_` suppresses the pool's
// active-mask write, not the authored alpha the bounds read, so an edit made
// while hidden must still reach the caches.
// ---------------------------------------------------------------------------

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

    // Hide the set exactly as the fog path does, then edit it while hidden:
    // deactivate the one live voxel and light a different one at another pose.
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

    // Show it again, the way the fog path does.
    set.visible_ = true;
    IRPrefab::VoxelPool::resyncRangeFromColors(set.voxelStartIdx_, set.numVoxels_, canvas);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(set.voxelStartIdx_, 64, viewportAround(editedPose)))
        << "an edit applied while hidden must reach the cull bounds on reveal";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

// ---------------------------------------------------------------------------
// AC3 — the cache is still a cache. These fail on a "recompute unconditionally"
// implementation, and on a widened pending set.
// ---------------------------------------------------------------------------

TEST_F(ChunkBoundsEvictionTest, StaticPoolStillCachesCardinalBounds) {
    C_VoxelPool pool = makeTwoChunkPool();
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds before = pool.getChunkBounds()[0];

    // Rewrite positions WITHOUT notifying: the cache must hold. If this moves,
    // the gate was dropped rather than its trigger set completed.
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
    // A full 1,024-chunk pool, as the criterion specifies.
    C_VoxelPool pool(ivec3(64, 64, 64)); // 262,144 slots
    pool.allocateVoxels(1024 * kChunk);
    for (int i = 0; i < 1024 * kChunk; ++i) {
        seedSlot(pool, static_cast<std::size_t>(i), vec3(i % 64, (i / 64) % 64, i / 4096), true);
    }
    pool.resyncActiveMaskFromColors(0, static_cast<std::size_t>(1024 * kChunk));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1024u) << "the seeding rebuild covers every chunk";

    // A clean same-cardinal call visits nothing at all.
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 0u);
    EXPECT_EQ(counters.cardinalSlots_, 0u);

    // One notified chunk ⇒ exactly that chunk, exactly its 256 slots.
    pool.markCullBoundsDirty(0, 1);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(kChunk));

    // Duplicate and overlapping notifications coalesce — they do not duplicate
    // work. Unchanged counters alone would not prove this; the count does.
    pool.markCullBoundsDirty(0, 1);
    pool.markCullBoundsDirty(0, 1);
    pool.markCullBoundsDirty(0, static_cast<std::size_t>(kChunk));
    pool.markCullBoundsDirty(3, 17);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(kChunk));

    // A zero-length range is a no-op, not a whole-pool eviction.
    pool.markCullBoundsDirty(500, 0);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 0u);

    // An unaligned bulk range crossing both a 32-slot mask word and a 256-slot
    // chunk boundary reaches exactly the chunks it spans.
    pool.markCullBoundsDirty(kChunk - 3, 70); // spans chunks 0 and 1
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 2u);
    EXPECT_EQ(counters.cardinalSlots_, static_cast<std::size_t>(2 * kChunk));

    // A cardinal-index change invalidates every chunk — the bounds are
    // projected under the index, so none of them survive it.
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

    // An all-inactive chunk keeps the inverted sentinel and reads never-visible.
    for (int i = kChunk; i < 300; ++i) {
        pool.getColors()[static_cast<std::size_t>(i)].color_ = Color{0, 0, 0, 0};
    }
    pool.resyncActiveMaskFromColors(kChunk, 44);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const IRComponents::ChunkBounds &tail = pool.getChunkBounds()[1];
    EXPECT_GT(tail.isoMin_.x, tail.isoMax_.x) << "an empty chunk keeps the inverted sentinel";
    EXPECT_EQ(IRSystem::buildChunkVisibilityMask(pool, viewportAround(vec3(3, 1, 2)))[1], 0u);
}

// ---------------------------------------------------------------------------
// AC2 — lifecycle and consumer independence.
// ---------------------------------------------------------------------------

TEST_F(ChunkBoundsEvictionTest, AllocationReuseAndDeallocationInvalidate) {
    C_VoxelPool pool(ivec3(16, 16, 4));
    const auto first = pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, first.startIndex_ + static_cast<std::size_t>(i), vec3(2, 2, 2), true);
    }
    pool.resyncActiveMaskFromColors(first.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    ASSERT_TRUE(pool.isRangeVisible(first.startIndex_, kChunk, viewportAround(vec3(2, 2, 2))));

    // Growth: a second span in a new chunk must appear in the bounds.
    const auto second = pool.allocateVoxels(kChunk);
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, second.startIndex_ + static_cast<std::size_t>(i), vec3(60, 12, 4), true);
    }
    pool.resyncActiveMaskFromColors(second.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(second.startIndex_, kChunk, viewportAround(vec3(60, 12, 4))));

    // Deallocation clears alpha, so the freed chunk must stop reporting visible.
    pool.deallocateVoxels(second.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_FALSE(pool.isRangeVisible(second.startIndex_, kChunk, viewportAround(vec3(60, 12, 4))));

    // Reuse of the freed span re-seeds it at a third pose.
    const auto reused = pool.allocateVoxels(kChunk);
    EXPECT_EQ(reused.startIndex_, second.startIndex_) << "the free span is reused, not grown past";
    for (int i = 0; i < kChunk; ++i) {
        seedSlot(pool, reused.startIndex_ + static_cast<std::size_t>(i), vec3(-40, 33, 6), true);
    }
    pool.resyncActiveMaskFromColors(reused.startIndex_, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(reused.startIndex_, kChunk, viewportAround(vec3(-40, 33, 6))));
}

// Two sets sharing one chunk: a notification naming either one re-derives the
// chunk from ALL of its slots, so the other set's contribution is preserved.
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

    // Move set A only, and notify only set A's half.
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

// One consumer must never eat the other's pending work.
TEST_F(ChunkBoundsEvictionTest, CardinalAndWorldConsumersArePendingIndependently) {
    C_VoxelPool pool = makeTwoChunkPool();
    // Build both caches once.
    pool.rebuildChunkBounds(CardinalIndex::k0, true, 0.35f); // yaw branch → world cache
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f); // cardinal cache
    (void)pool.consumeCullRebuildCounters();

    // Notify chunk 0, then let ONLY the cardinal consumer run.
    pool.markCullBoundsDirty(0, 1);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u);
    EXPECT_EQ(counters.worldChunks_, 0u) << "the cardinal pass must not touch the world cache";

    // The world consumer must still see chunk 0 as owed.
    pool.rebuildChunkBounds(CardinalIndex::k0, true, 0.35f);
    counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.worldChunks_, 1u)
        << "the cardinal consumer must not have cleared the world cache's pending work";
}

// A yaw frame self-invalidates the cardinal cache, so returning to cardinal
// rebuilds everything; all four cardinal indices must land on the oracle.
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

// The upload queue and the cull caches are independent channels: an edit made
// after `kMaxPendingPositionRanges` saturates still invalidates the bounds.
TEST_F(ChunkBoundsEvictionTest, EditsAfterUploadQueueSaturationStillInvalidate) {
    const vec3 nearPose(2, 2, 2);
    const vec3 movedPose(150, -70, 9);
    C_VoxelPool pool = makeTwoChunkPool(nearPose);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    // Saturate the queue with harmless one-slot ranges inside chunk 1.
    for (std::size_t i = 0; i < C_VoxelPool::kMaxPendingPositionRanges + 16; ++i) {
        pool.queuePositionRange(static_cast<std::size_t>(kChunk), 1);
    }
    ASSERT_EQ(pool.getPendingPositionRanges().size(), C_VoxelPool::kMaxPendingPositionRanges);
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    // Now the real edit, queued past saturation.
    for (int i = 0; i < kChunk; ++i) {
        pool.getPositionGlobals()[static_cast<std::size_t>(i)].pos_ = movedPose;
    }
    pool.queuePositionRange(0, static_cast<std::size_t>(kChunk));
    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(movedPose)))
        << "a saturated upload queue must not swallow the cull invalidation";
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

// The two channels are independent: a producer can owe the cull caches a
// recompute without owing binding 5 an upload. Both live paths rely on it —
// `UPDATE_VOXEL_SET_CHILDREN` stages a GPU-transform-indirected set's real
// range for invalidation while deliberately NOT queuing its upload (the GPU
// prepass owns binding 5 for those slots), and `REBUILD_GRID_VOXELS`'s identity
// arm rewrites positions every frame but only queues on the restore frame.
// Folding "don't upload" into a zeroed range, as the staging record did before
// #2830, silently dropped invalidation for exactly the sets that move most.
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
    // Invalidate WITHOUT queuing an upload — the GPU-slotted shape.
    pool.markCullBoundsDirty(0, static_cast<std::size_t>(kChunk));
    EXPECT_TRUE(pool.getPendingPositionRanges().empty())
        << "invalidation must not enqueue a binding-5 upload";

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    const auto counters = pool.consumeCullRebuildCounters();
    EXPECT_EQ(counters.cardinalChunks_, 1u) << "the bounds must still re-derive";
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, viewportAround(movedPose)));
    expectBoundsMatchOracle(pool, 0, CardinalIndex::k0);
}

// `isRangeVisible` feeds the UPDATE movers, which run BEFORE the render pipeline
// re-derives the bounds. A range with pending invalidation must be admitted
// rather than answered from bounds that already owe a recompute — that latch is
// what made the defect self-sustaining.
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

    // Bounds not rebuilt yet — the answer must be conservatively true.
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, atMoved));
    // The untouched chunk still answers from its (valid) cached bounds.
    EXPECT_FALSE(pool.isRangeVisible(kChunk, kChunk, atMoved));

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);
    EXPECT_TRUE(pool.isRangeVisible(0, kChunk, atMoved));
    EXPECT_FALSE(pool.isRangeVisible(0, kChunk, viewportAround(nearPose)))
        << "once rebuilt the query is the ordinary overlap answer again";
}

// ---------------------------------------------------------------------------
// The detached re-voxelize pool — the third `rebuildChunkBounds` branch.
// ---------------------------------------------------------------------------
//
// `m_staticReVoxelizeBound` switches `rebuildChunkBounds` onto a branch that
// owns EVERY chunk's cardinal answer and returns early (#1556): it re-derives
// all of them on every call from a rotation-independent bound that reads
// neither position nor alpha. It therefore has to consume the cardinal pending
// bits on the way out. `allocateVoxels` arms them, nothing else on that branch
// clears them, and `isRangeVisible` admits any pending chunk conservatively —
// so bits left standing make every detached range answer visible FOREVER and
// the UPDATE-side cull stops culling this pool mode at all. That is a
// regression the cardinal-path cases above cannot see, because they never take
// this branch.
//
// POSITIVE CONTROL: delete the `dropPendingChunks(m_pendingCardinalChunks, …)`
// line from the static branch and the off-viewport expectations below fail
// (the range reads visible); the on-bound ones keep passing, so the failure
// isolates the latch rather than a broken bound.

// The branch in isolation: `setStaticReVoxelizeBound` alone reproduces the
// latch, because `allocateVoxels` armed the bits before the bound existed.
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

// The bound seeded through the production path: REBUILD_DETACHED_VOXELS owns
// the one-time `setStaticReVoxelizeBound` call, and its tick also rewrites the
// pool's active mask — which re-arms the very bits this branch must consume.
class DetachedCullBoundTest : public ::testing::Test {
  protected:
    // EntityManager before SystemManager, per RebuildDetachedVoxelsGuardTest:
    // the system manager's registration reaches the entity manager.
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

    // A DETACHED_REVOXELIZE canvas holding one origin-centered solid. The
    // explicit `targetCanvas` argument routes the allocation through this
    // canvas instead of the RenderManager's active one, so no render manager
    // is needed.
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

// An off-viewport detached range reads invisible once its static bound has
// been rebuilt — the whole point of having a bound at all.
TEST_F(DetachedCullBoundTest, OffViewportDetachedRangeIsCulledAfterStaticRebuild) {
    const IREntity::EntityId canvas = makeDetachedCanvas();
    C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    ASSERT_TRUE(pool.hasStaticReVoxelizeBound()) << "the tick must have reached the seed";
    const std::size_t count = static_cast<std::size_t>(pool.getLiveVoxelCount());
    ASSERT_GT(count, 0u);

    pool.rebuildChunkBounds(CardinalIndex::k0, false, 0.0f);

    // The bound is origin-centered, so a viewport far from the origin misses it.
    EXPECT_FALSE(pool.isRangeVisible(0, count, viewportAround(vec3(400, -400, 0))))
        << "a detached pool whose bound was just rebuilt must answer the ordinary "
           "overlap question, not admit unconditionally";
    // Not a vacuous pass: the same query over the bound still admits.
    EXPECT_TRUE(pool.isRangeVisible(0, count, viewportAround(vec3(0, 0, 0))))
        << "the conservative origin-centered bound must still cover the solid";
}

// The pending lifecycle's promised shape on this branch: an in-place edit
// admits conservatively until the next rebuild, and the rebuild restores the
// ordinary overlap answer rather than latching.
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
