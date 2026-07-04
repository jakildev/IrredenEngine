#include <gtest/gtest.h>

#include <irreden/ir_command.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_locked.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <vector>

// Covers Command<RANDOMIZE_VOXELS> (#17), the first consumer of the one-shot
// IRSystem::executeQuery primitive. The command randomizes every active voxel's
// RGB across all voxel sets while (a) excluding C_Locked-tagged entities via the
// Exclude<> archetype filter, (b) preserving each voxel's original alpha, and
// (c) skipping alpha-0 (carved) voxels — so the pool's active-mask stays stable
// through editVoxels' resync. Fired through IRCommand::fireByName, which also
// proves the enum value now dispatches to a real specialization instead of the
// unimplemented-log fall-through.
//
// Headless: the explicit targetCanvas arg on C_VoxelSetNew routes pool ops
// through a specific canvas entity, bypassing the RenderManager active-canvas
// lookup (same fixture as voxel_set_edit_api_test.cpp). RNG is seeded so the
// randomization is deterministic run-to-run and independent of prior tests.

namespace {

using IRComponents::C_Locked;
using IRComponents::C_Voxel;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::kVoxelActiveMaskBits;
using IRMath::Color;
using IRMath::ivec3;

class CommandRandomizeVoxelsTest : public ::testing::Test {
  protected:
    IREntity::EntityManager m_entityManager;

    // Pool active-mask bit for the voxel at set-local flat index `localIndex`.
    static bool maskBit(const C_VoxelSetNew &set, const C_VoxelPool &pool, int localIndex) {
        const std::size_t slot = set.voxelStartIdx_ + static_cast<std::size_t>(localIndex);
        return (pool.getActiveMask()[slot / kVoxelActiveMaskBits] >>
                (slot % kVoxelActiveMaskBits)) &
               1u;
    }
};

TEST_F(CommandRandomizeVoxelsTest, RandomizesUnlockedPreservesAlphaSkipsLockedAndCarved) {
    IRMath::seedThreadRng(12345u);

    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});

    // Unlocked set: solid opaque; we carve one voxel to alpha 0 below.
    const IREntity::EntityId unlocked = IREntity::createEntity(
        C_VoxelSetNew{ivec3(3, 3, 3), Color{200, 100, 50, 255}, false, canvas}
    );
    // Locked set: solid opaque, tagged C_Locked so the query's Exclude<> skips it.
    const IREntity::EntityId locked = IREntity::createEntity(
        C_VoxelSetNew{ivec3(2, 2, 2), Color{10, 20, 30, 255}, false, canvas},
        C_Locked{}
    );

    auto &unlockedSet = IREntity::getComponent<C_VoxelSetNew>(unlocked);
    auto &lockedSet = IREntity::getComponent<C_VoxelSetNew>(locked);
    ASSERT_EQ(unlockedSet.numVoxels_, 27);
    ASSERT_EQ(lockedSet.numVoxels_, 8);

    // Carve local voxel 0 (alpha -> 0), then resync so the pool mask reflects it.
    unlockedSet.voxels_[0].deactivate();
    unlockedSet.resyncAfterRawEdits();

    const auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);

    // Snapshot colors + the carved voxel's mask bit before firing.
    std::vector<Color> unlockedBefore;
    std::vector<Color> lockedBefore;
    for (int i = 0; i < unlockedSet.numVoxels_; ++i) {
        unlockedBefore.push_back(unlockedSet.voxels_[i].color_);
    }
    for (int i = 0; i < lockedSet.numVoxels_; ++i) {
        lockedBefore.push_back(lockedSet.voxels_[i].color_);
    }
    ASSERT_FALSE(maskBit(unlockedSet, pool, 0)) << "carved voxel starts inactive";

    IRCommand::fireByName(IRCommand::RANDOMIZE_VOXELS);

    // Unlocked set: alpha preserved on every voxel, and at least one active
    // voxel's RGB changed (the randomization actually ran — proving fireByName
    // dispatched to the specialization, not the unimplemented-log path).
    bool anyRgbChanged = false;
    for (int i = 0; i < unlockedSet.numVoxels_; ++i) {
        const Color &before = unlockedBefore[i];
        const Color &after = unlockedSet.voxels_[i].color_;
        EXPECT_EQ(after.alpha_, before.alpha_) << "alpha preserved at voxel " << i;
        if (after.red_ != before.red_ || after.green_ != before.green_ ||
            after.blue_ != before.blue_) {
            anyRgbChanged = true;
        }
    }
    EXPECT_TRUE(anyRgbChanged) << "randomize should recolor the unlocked set";

    // Carved voxel 0 is skipped entirely: color byte-identical, still inactive,
    // pool mask unchanged.
    EXPECT_EQ(unlockedSet.voxels_[0].color_.red_, unlockedBefore[0].red_);
    EXPECT_EQ(unlockedSet.voxels_[0].color_.green_, unlockedBefore[0].green_);
    EXPECT_EQ(unlockedSet.voxels_[0].color_.blue_, unlockedBefore[0].blue_);
    EXPECT_EQ(unlockedSet.voxels_[0].color_.alpha_, 0);
    EXPECT_FALSE(maskBit(unlockedSet, pool, 0)) << "carved voxel stays inactive";

    // Locked set: byte-identical (excluded from the query).
    for (int i = 0; i < lockedSet.numVoxels_; ++i) {
        EXPECT_EQ(lockedSet.voxels_[i].color_.red_, lockedBefore[i].red_);
        EXPECT_EQ(lockedSet.voxels_[i].color_.green_, lockedBefore[i].green_);
        EXPECT_EQ(lockedSet.voxels_[i].color_.blue_, lockedBefore[i].blue_);
        EXPECT_EQ(lockedSet.voxels_[i].color_.alpha_, lockedBefore[i].alpha_);
    }
}

} // namespace
