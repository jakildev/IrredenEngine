#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

#include <irreden/ir_entity.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/asset/rig_format.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/rig_bridge.hpp>

// #1603 — SYSTEM_UPDATE_JOINT_MATRICES (#605 Phase 2.2).
//
// These tests exercise the CPU side of the joint-matrix uploader without a GPU
// device: the high-region block allocator, and the per-skeleton staging fill
// (identity at bind pose, severance holes identity, correct bone->slot routing).
// The actual binding-18 subData upload (endTick) needs a render device and is
// covered at runtime by the skeletal demo (#1611); endTick is never called here.

namespace {

using JointMatrices = IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES>;
constexpr float kTolerance = 1e-4f;

// Straight 3-bone chain along +X (shared with skeleton_skinning_test): rest
// world translations are (0,0,0), (2,0,0), (5,0,0).
IRAsset::Rig makeChainRig() {
    IRAsset::Rig rig;
    rig.joints_.resize(3);
    rig.joints_[0].parentIndex_ = 0;
    rig.joints_[0].translation_ = IRMath::vec4(0.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[1].parentIndex_ = 0;
    rig.joints_[1].translation_ = IRMath::vec4(2.0f, 0.0f, 0.0f, 0.0f);
    rig.joints_[2].parentIndex_ = 1;
    rig.joints_[2].translation_ = IRMath::vec4(3.0f, 0.0f, 0.0f, 0.0f);
    return rig;
}

void expectMat4Near(const IRMath::mat4 &actual, const IRMath::mat4 &expected) {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[col][row], expected[col][row], kTolerance)
                << "mismatch at [" << col << "][" << row << "]";
        }
    }
}

// ---- Block allocator (no entity manager / GPU needed) --------------------

TEST(JointBlockAllocator, BlocksAreContiguousInsideReservedRegion) {
    JointMatrices sys;
    const std::uint32_t a = sys.acquireJointBlock(30);
    const std::uint32_t b = sys.acquireJointBlock(10);

    ASSERT_NE(a, IRRender::kVoxelTransformStatic);
    ASSERT_NE(b, IRRender::kVoxelTransformStatic);
    // Both blocks live in the reserved high region and never reach into the
    // voxel-set region [0, kJointTransformSlotBase).
    EXPECT_GE(a, static_cast<std::uint32_t>(IRRender::kJointTransformSlotBase));
    EXPECT_LE(a + 30, static_cast<std::uint32_t>(IRRender::kMaxGpuVoxelTransforms));
    EXPECT_GE(b, static_cast<std::uint32_t>(IRRender::kJointTransformSlotBase));
    // Disjoint blocks.
    EXPECT_TRUE(a >= b + 10 || b >= a + 30);
}

TEST(JointBlockAllocator, ExactSizeBlocksRecycleAfterRelease) {
    JointMatrices sys;
    const std::uint32_t a = sys.acquireJointBlock(30);
    sys.releaseJointBlock(a, 30);
    // Same-size reacquire reuses the freed block rather than carving fresh.
    EXPECT_EQ(sys.acquireJointBlock(30), a);
}

TEST(JointBlockAllocator, ExhaustionReturnsStaticSentinel) {
    JointMatrices sys;
    const std::uint32_t whole =
        sys.acquireJointBlock(static_cast<std::uint32_t>(IRRender::kMaxGpuJointTransforms));
    ASSERT_NE(whole, IRRender::kVoxelTransformStatic);
    EXPECT_EQ(whole, static_cast<std::uint32_t>(IRRender::kJointTransformSlotBase));
    // Region full — one more slot is denied (caller renders unskinned).
    EXPECT_EQ(sys.acquireJointBlock(1), IRRender::kVoxelTransformStatic);
}

// ---- Staging fill (headless entity manager, no GPU) ----------------------

class JointMatrixUploadTest : public testing::Test {
  protected:
    // Creates a rig-root entity carrying C_Skeleton + one C_Joint entity per
    // pose entry, with each joint's C_WorldTransform set to `worlds[i]` (the
    // stand-in for PROPAGATE_TRANSFORM's output). joints_[i] == kNullEntity for
    // any i in `holes` (a severed slot). Returns the joint entity ids.
    std::vector<IREntity::EntityId> buildSkeleton(
        const std::vector<IRMath::SQT> &bind,
        const std::vector<IRMath::SQT> &worlds,
        const std::vector<int> &holes = {}
    ) {
        std::vector<IREntity::EntityId> joints;
        for (std::size_t i = 0; i < bind.size(); ++i) {
            const bool isHole =
                std::find(holes.begin(), holes.end(), static_cast<int>(i)) != holes.end();
            if (isHole) {
                joints.push_back(IREntity::kNullEntity);
                continue;
            }
            const IREntity::EntityId joint = IREntity::createEntity(IRComponents::C_Joint{});
            auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
            world.scale_ = worlds[i].scale_;
            world.rotation_ = worlds[i].rotation_;
            world.translation_ = worlds[i].translation_;
            joints.push_back(joint);
        }
        IRComponents::C_Skeleton skeleton;
        skeleton.joints_ = joints;
        skeleton.bindPose_ = bind;
        IREntity::createEntity(skeleton);
        return joints;
    }

    // Runs one frame of the system over the live joints.
    void runFrame(JointMatrices &sys, const std::vector<IREntity::EntityId> &joints) {
        sys.beginTick();
        for (const IREntity::EntityId joint : joints) {
            if (joint == IREntity::kNullEntity) {
                continue;
            }
            IRComponents::C_Joint tag;
            auto &world = IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
            sys.tick(joint, tag, world);
        }
    }

    IREntity::EntityManager m_entity_manager;
};

TEST_F(JointMatrixUploadTest, SlotsAreIdentityAtBindPose) {
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(makeChainRig());
    const std::vector<IREntity::EntityId> joints = buildSkeleton(bind, bind); // posed == rest

    JointMatrices sys;
    runFrame(sys, joints);

    ASSERT_EQ(sys.skeletonBlocks_.size(), 1u);
    const std::uint32_t base = sys.skeletonBlocks_.begin()->second.base_;
    EXPECT_EQ(sys.skeletonBlocks_.begin()->second.count_, bind.size());
    for (std::uint32_t i = 0; i < bind.size(); ++i) {
        expectMat4Near(
            sys.jointStaging_[JointMatrices::localSlot(base + i)].modelToWorld_,
            IRMath::mat4(1.0f)
        );
    }
}

TEST_F(JointMatrixUploadTest, BoneIndexMapsToBasePlusIndex) {
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(makeChainRig());

    // Pose joint 1: 90° about +Z, away from its rest transform.
    std::vector<IRMath::SQT> worlds = bind;
    worlds[1].rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);

    const std::vector<IREntity::EntityId> joints = buildSkeleton(bind, worlds);
    JointMatrices sys;
    runFrame(sys, joints);

    const std::uint32_t base = sys.skeletonBlocks_.begin()->second.base_;
    // Each bone i lands at base + i and holds skinMatrix(world_i, bind_i).
    for (std::uint32_t i = 0; i < bind.size(); ++i) {
        expectMat4Near(
            sys.jointStaging_[JointMatrices::localSlot(base + i)].modelToWorld_,
            IRPrefab::Skeleton::skinMatrix(worlds[i], bind[i])
        );
    }
    // The posed joint is genuinely non-identity (guards against a no-op fill).
    const IRMath::mat4 posed =
        sys.jointStaging_[JointMatrices::localSlot(base + 1)].modelToWorld_;
    EXPECT_GT(IRMath::abs(posed[0][0] - 1.0f) + IRMath::abs(posed[1][1] - 1.0f), kTolerance);
}

TEST_F(JointMatrixUploadTest, SeveranceHoleStaysIdentity) {
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(makeChainRig());
    // Pose every joint so an un-cleared slot would be visibly non-identity.
    std::vector<IRMath::SQT> worlds = bind;
    for (IRMath::SQT &w : worlds) {
        w.rotation_ = IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    }
    const std::vector<IREntity::EntityId> joints = buildSkeleton(bind, worlds, /*holes=*/{1});

    JointMatrices sys;
    runFrame(sys, joints);

    const std::uint32_t base = sys.skeletonBlocks_.begin()->second.base_;
    // The severed slot uploads identity even though its neighbours are posed.
    expectMat4Near(
        sys.jointStaging_[JointMatrices::localSlot(base + 1)].modelToWorld_,
        IRMath::mat4(1.0f)
    );
    // The live neighbours (bones 0 and 2) are posed, so their slots are filled
    // and non-identity — confirms the hole was skipped, not the whole block.
    for (const std::uint32_t boneIndex : {0u, 2u}) {
        const IRMath::mat4 &m =
            sys.jointStaging_[JointMatrices::localSlot(base + boneIndex)].modelToWorld_;
        EXPECT_GT(IRMath::abs(m[0][0] - 1.0f) + IRMath::abs(m[1][1] - 1.0f), kTolerance);
    }
}

} // namespace
