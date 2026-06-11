#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

#include <irreden/ir_entity.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// #1605 — per-voxel skinning via bone→slot in the binding-17 seed path
// (#605 Phase 2.3).
//
// Covers the CPU seam between UPDATE_JOINT_MATRICES' skeleton slot blocks and
// the voxel pool's per-voxel transform indices: bone ids resolve to
// `block.base_ + bone_id`, out-of-range bone ids fall back to the set's entity
// slot, and a joint-count change (block realloc) re-stamps automatically. The
// binding-17 upload itself (UPDATE_VOXEL_POSITIONS_GPU's endTick) needs a GPU
// device and is exercised at runtime by shape_debug --skin-smoke.

namespace {

using IRComponents::C_Joint;
using IRComponents::C_Skeleton;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRMath::Color;
using IRMath::ivec3;
using JointMatrices = IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES>;

class VoxelBoneSlotSeedTest : public testing::Test {
  protected:
    // Rig root carrying a 3x1x1 voxel set allocated from an explicit canvas
    // pool (headless — no RenderManager) plus a C_Skeleton of `jointCount`
    // joints at rest. Bone paint: voxel i gets bone_id `boneIds[i]`.
    IREntity::EntityId buildRiggedSet(
        std::uint32_t jointCount,
        const std::vector<std::uint8_t> &boneIds
    ) {
        m_canvas = IREntity::createEntity(C_VoxelPool{ivec3(8, 8, 8)});
        const IREntity::EntityId rigRoot = IREntity::createEntity(
            C_VoxelSetNew{ivec3(3, 1, 1), Color{200, 100, 50, 255}, true, m_canvas}
        );
        auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(rigRoot);
        for (std::size_t i = 0; i < boneIds.size(); ++i) {
            voxelSet.voxels_[i].bone_id_ = boneIds[i];
        }

        C_Skeleton skeleton;
        for (std::uint32_t i = 0; i < jointCount; ++i) {
            skeleton.joints_.push_back(IREntity::createEntity(C_Joint{}));
            skeleton.bindPose_.push_back(IRMath::SQT{});
        }
        IREntity::setComponent(rigRoot, skeleton);
        return rigRoot;
    }

    IREntity::EntityManager m_entityManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
};

TEST_F(VoxelBoneSlotSeedTest, BoneIdsResolveToBlockSlotsWithEntitySlotFallback) {
    // Bones 0 and 1 are in range of the 2-joint skeleton; bone 7 is not and
    // must fall back to the set's entity slot (rigid follow), never index past
    // the skeleton's block into a neighbouring rig's slots.
    const IREntity::EntityId rigRoot = buildRiggedSet(2, {0, 1, 7});
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(rigRoot);
    constexpr std::uint32_t kEntitySlot = 3;
    voxelSet.gpuTransformSlot_ = kEntitySlot;

    JointMatrices sys;
    sys.beginTick(); // allocates the skeleton's block → auto-seeds the set

    const std::uint32_t base = sys.skeletonBlocks_.at(rigRoot).base_;
    ASSERT_NE(base, IRRender::kVoxelTransformStatic);
    const auto &pool = IREntity::getComponent<C_VoxelPool>(m_canvas);
    const auto &indices = pool.getTransformIndices();
    const std::size_t start = voxelSet.voxelStartIdx_;
    EXPECT_EQ(indices[start + 0], base + 0);
    EXPECT_EQ(indices[start + 1], base + 1);
    EXPECT_EQ(indices[start + 2], kEntitySlot);
    // The stamped slice is queued so UPDATE_VOXEL_POSITIONS_GPU re-seeds
    // binding 17 this frame.
    ASSERT_EQ(pool.getPendingTransformIndexRanges().size(), 1u);
    EXPECT_EQ(pool.getPendingTransformIndexRanges()[0], std::make_pair(start, std::size_t{3}));
}

TEST_F(VoxelBoneSlotSeedTest, JointCountChangeRestampsAgainstTheNewBlock) {
    const IREntity::EntityId rigRoot = buildRiggedSet(2, {0, 1, 1});
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(rigRoot);
    voxelSet.gpuTransformSlot_ = 0;

    JointMatrices sys;
    sys.beginTick();
    const std::uint32_t firstBase = sys.skeletonBlocks_.at(rigRoot).base_;

    // Steady state: same joint count → no realloc, no new pending range.
    auto &pool = IREntity::getComponent<C_VoxelPool>(m_canvas);
    pool.clearPendingTransformIndexRanges();
    sys.beginTick();
    EXPECT_TRUE(pool.getPendingTransformIndexRanges().empty());

    // Grow the skeleton — the block reallocates (different size → different
    // base) and the voxel slots must re-stamp against the new base.
    auto &skeleton = IREntity::getComponent<C_Skeleton>(rigRoot);
    skeleton.joints_.push_back(IREntity::createEntity(C_Joint{}));
    skeleton.bindPose_.push_back(IRMath::SQT{});
    sys.beginTick();

    const std::uint32_t newBase = sys.skeletonBlocks_.at(rigRoot).base_;
    ASSERT_NE(newBase, firstBase);
    const auto &indices = pool.getTransformIndices();
    const std::size_t start = voxelSet.voxelStartIdx_;
    EXPECT_EQ(indices[start + 0], newBase + 0);
    EXPECT_EQ(indices[start + 1], newBase + 1);
    EXPECT_EQ(indices[start + 2], newBase + 1);
    EXPECT_FALSE(pool.getPendingTransformIndexRanges().empty());
}

TEST_F(VoxelBoneSlotSeedTest, UnwiredEntitySlotAllocatorLeavesSetCpuDirect) {
    // No IRPrefab::VoxelTransform allocator is wired in this headless test, so
    // a set that never acquired an entity slot cannot route through the
    // prepass — seeding must bail and leave every voxel CPU-direct rather than
    // stamp bone slots a dispatch would never consume.
    const IREntity::EntityId rigRoot = buildRiggedSet(2, {0, 1, 1});
    auto &voxelSet = IREntity::getComponent<C_VoxelSetNew>(rigRoot);
    ASSERT_EQ(voxelSet.gpuTransformSlot_, IRRender::kVoxelTransformStatic);

    JointMatrices sys;
    sys.beginTick();

    EXPECT_EQ(voxelSet.gpuTransformSlot_, IRRender::kVoxelTransformStatic);
    const auto &pool = IREntity::getComponent<C_VoxelPool>(m_canvas);
    const std::size_t start = voxelSet.voxelStartIdx_;
    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_EQ(pool.getTransformIndices()[start + i], IRRender::kVoxelTransformStatic);
    }
}

TEST_F(VoxelBoneSlotSeedTest, SlotBaseQueryMatchesTheSkeletonBlock) {
    // IRPrefab::JointTransform::slotBase is the mechanism downstream phases
    // (#1606 binding-21 retirement, #1610 FK editing) use to reach the block —
    // unwired it reports the static sentinel, wired it must agree with the
    // system's own map. Wiring needs a SystemManager, so the unwired half is
    // what's assertable headlessly.
    EXPECT_EQ(
        IRPrefab::JointTransform::slotBase(IREntity::kNullEntity),
        IRRender::kVoxelTransformStatic
    );
}

} // namespace
