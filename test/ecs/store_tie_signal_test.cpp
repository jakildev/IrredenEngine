#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/systems/system_voxel_to_trixel.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// #2346 regression guard: the cardinal store's tie-possibility signal
// (`C_VoxelPool::storeTiesPossible_`) must re-arm on ACTIVATION-ONLY edits, not
// just position writes. `VOXEL_TO_TRIXEL_STAGE_1` recomputes the signal on
// frames whose CPU position upload changed (pending position ranges flushed, or
// a canvas re-seed), but the active-mask mutators
// (activate/deactivate/carve/fillPlane/reshape) change which voxels are live
// WITHOUT queuing a position range — so before the fix, activating a voxel onto
// an already-occupied `roundHalfUp` cell left the signal stale and the
// last-writer-wins cardinal race this feature closes reappeared for
// editor/carve/reveal workflows. The pool now flags every active-mask mutation
// via `consumeActiveMaskChanged()`, which the system ORs into the recompute
// trigger. These tests exercise that path headlessly (no RenderManager) against
// a hand-seeded pool, and confirm the end-to-end reclassification — the positive
// enabled-path test the render `CLAUDE.md` calls for (byte-identity at default
// only proves the OFF path is a no-op).

namespace {

using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;
using IRMath::vec3;

// Drive one static voxel's world position on a headless pool. The scan reads the
// GLOBAL position mirror, which `UPDATE_VOXEL_SET_CHILDREN` fills in the live
// pipeline; a headless pool leaves it zeroed, so the tests seed it directly to
// place each voxel in a chosen `roundHalfUp` cell.
void placeVoxel(C_VoxelPool &pool, std::size_t slot, vec3 worldPos) {
    pool.getPositionGlobals()[slot].pos_ = worldPos;
}

class StoreTieSignalTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;
};

// The exact bug: two voxels share a rounded world cell, one starts inactive.
// Activating the second (pure activation — no position write) must re-arm the
// tie signal so the winner-guarded stage 2 runs. Before the fix the signal
// stayed false and the race persisted.
TEST_F(StoreTieSignalTest, ActivationIntoCollisionReArmsTieSignal) {
    C_VoxelPool pool(ivec3(8, 8, 8));
    placeVoxel(pool, 0, vec3(2, 2, 2));
    placeVoxel(pool, 1, vec3(2, 2, 2));    // same cell as slot 0
    pool.setActiveBit(0);                  // slot 0 active, slot 1 still inactive
    (void)pool.consumeActiveMaskChanged(); // discard the setup-time signal

    std::vector<std::uint64_t> scratch;

    // Baseline: only one active voxel in the cell → no tie.
    IRSystem::recomputeStoreTiesPossible(pool, 2, scratch);
    EXPECT_FALSE(pool.storeTiesPossible_);

    // Activate slot 1 — an active-mask-only edit, no queued position range.
    pool.setActiveBit(1);
    // The fix: the mutation flags the pool, so the system re-runs the scan.
    EXPECT_TRUE(pool.consumeActiveMaskChanged());

    // Re-running the scan (as VOXEL_TO_TRIXEL_STAGE_1 now does) catches the tie.
    IRSystem::recomputeStoreTiesPossible(pool, 2, scratch);
    EXPECT_TRUE(pool.storeTiesPossible_);
}

// Negative control: an activation that does NOT create a collision still flags
// the pool (an edit happened), but the recompute leaves the signal false — the
// signal drives a real reclassification, not a blanket "always tie".
TEST_F(StoreTieSignalTest, ActivationWithoutCollisionLeavesSignalClear) {
    C_VoxelPool pool(ivec3(8, 8, 8));
    placeVoxel(pool, 0, vec3(2, 2, 2));
    placeVoxel(pool, 1, vec3(5, 5, 5)); // distinct cell
    pool.setActiveBit(0);
    (void)pool.consumeActiveMaskChanged();

    std::vector<std::uint64_t> scratch;
    pool.setActiveBit(1);
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
    IRSystem::recomputeStoreTiesPossible(pool, 2, scratch);
    EXPECT_FALSE(pool.storeTiesPossible_);
}

// Every named active-mask mutator flags the pool, and the read is a
// read-and-clear (so a frame with no edit does not spuriously re-trigger the
// scan). Covers the single-bit setters and the range setters (which mass-write
// whole mask words, bypassing the per-bit path).
TEST_F(StoreTieSignalTest, EveryActiveMaskMutatorFlagsAndConsumeClears) {
    C_VoxelPool pool(ivec3(8, 8, 8));
    (void)pool.consumeActiveMaskChanged(); // clear any construction-time signal
    EXPECT_FALSE(pool.consumeActiveMaskChanged());

    pool.setActiveBit(3);
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
    EXPECT_FALSE(pool.consumeActiveMaskChanged()); // consumed

    pool.clearActiveBit(3);
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
    (void)pool.consumeActiveMaskChanged();

    // A 40-slot range spans two 32-bit mask words, so it exercises the
    // whole-word middle mass-write that skips setActiveBit/clearActiveBit.
    pool.setActiveMaskRange(0, 40);
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
    (void)pool.consumeActiveMaskChanged();

    pool.clearActiveMaskRange(0, 40);
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
}

// The reviewer-named editor path: a C_VoxelSetNew bulk mutator and a carve both
// route through the pool's active-mask mutators, so both flag the pool. This is
// the set-level surface an editor/carve/reveal workflow actually calls.
TEST_F(StoreTieSignalTest, VoxelSetMutatorsFlagThePool) {
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
    const IREntity::EntityId object = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 3, 3), Color{200, 100, 50, 255}, false, canvas}
    );

    auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    auto &set = IREntity::getComponent<C_VoxelSetNew>(object);
    (void)pool.consumeActiveMaskChanged(); // clear the construction-time signal
    ASSERT_FALSE(pool.consumeActiveMaskChanged());

    set.deactivateAll();
    EXPECT_TRUE(pool.consumeActiveMaskChanged());

    set.activateAll();
    EXPECT_TRUE(pool.consumeActiveMaskChanged());

    // A custom carve resyncs the active mask per voxel (editVoxels →
    // resyncDerivedState → syncActiveMask), so it flags the pool too.
    set.carve([](vec3 localPos) { return localPos.x < 0.5f; });
    EXPECT_TRUE(pool.consumeActiveMaskChanged());
}

} // namespace
