#include <gtest/gtest.h>

#include <cstdint>
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
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/rig_bridge.hpp>
#include <irreden/voxel/skeleton.hpp>

// #1612 — CPU simulation of the full skinning pipeline (#605 Phase 2.11).
//
// The GPU shader (c_update_voxel_positions.glsl) executes:
//   uint slot = floatBitsToUint(localEntry.w);  // bone_id → slot (Phase 2.3)
//   vec3 worldPos = (transforms[slot] * vec4(localEntry.xyz, 1.0)).xyz;
//
// This test simulates that chain on the CPU:
//   1. C_VoxelPool transform indices (Phase 2.3 seeding): bone_id → base + bone_id
//   2. jointStaging_ (Phase 2.2 staging fill): slot → skinMatrix(jointWorld, bindPose)
//   3. worldPos = mat * vec4(localPos, 1.0)
//
// The two sub-paths are unit-tested independently in joint_matrix_upload_test.cpp
// (Phase 2.2) and voxel_bone_slot_seed_test.cpp (Phase 2.3). This file exercises
// their integration: a bug in the slot-base alignment or localSlot offset would
// cause a mismatch without requiring a running GPU device.
//
// Render-verify reference screenshots (the second deliverable of #1612) require
// the rigged demo (#1611) to author its entities; deferred until that PR lands.

namespace {

using JointMatrices = IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES>;
constexpr float kTolerance = 1e-4f;

// Straight 3-bone chain along +X (same geometry as joint_matrix_upload_test and
// skeleton_skinning_test). Rest world translations: (0,0,0), (2,0,0), (5,0,0).
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

class SkinningPipelineConsistencyTest : public testing::Test {
  protected:
    // Builds a rig root with a bind.size()-voxel set (one per bone) and a
    // C_Skeleton of the given joints. Each joint i gets C_WorldTransform = worlds[i],
    // and voxel i gets bone_id = i. gpuTransformSlot_ is pre-set so that
    // seedVoxelBoneSlots does not bail on the "no entity slot" path.
    void buildRig(
        const std::vector<IRMath::SQT>& bind,
        const std::vector<IRMath::SQT>& worlds
    ) {
        m_canvas = IREntity::createEntity(
            IRComponents::C_VoxelPool{IRMath::ivec3(8, 8, 8)}
        );
        m_rigRoot = IREntity::createEntity(
            IRComponents::C_VoxelSetNew{
                IRMath::ivec3(static_cast<int>(bind.size()), 1, 1),
                IRMath::Color{200, 100, 50, 255},
                true,
                m_canvas
            }
        );
        auto& voxelSet = IREntity::getComponent<IRComponents::C_VoxelSetNew>(m_rigRoot);
        voxelSet.gpuTransformSlot_ = kEntitySlot;
        for (std::size_t i = 0; i < bind.size(); ++i) {
            voxelSet.voxels_[i].bone_id_ = static_cast<std::uint8_t>(i);
        }

        IRComponents::C_Skeleton skeleton;
        skeleton.bindPose_ = bind;
        for (std::size_t i = 0; i < bind.size(); ++i) {
            const IREntity::EntityId joint =
                IREntity::createEntity(IRComponents::C_Joint{});
            auto& world =
                IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
            world.scale_ = worlds[i].scale_;
            world.rotation_ = worlds[i].rotation_;
            world.translation_ = worlds[i].translation_;
            skeleton.joints_.push_back(joint);
            m_joints.push_back(joint);
        }
        IREntity::setComponent(m_rigRoot, skeleton);
    }

    // Runs one full frame: beginTick seeds voxel slots, tick fills staging matrices.
    void runFrame(JointMatrices& sys) {
        sys.beginTick();
        for (const IREntity::EntityId joint : m_joints) {
            IRComponents::C_Joint tag;
            auto& world =
                IREntity::getComponent<IRComponents::C_WorldTransform>(joint);
            sys.tick(joint, tag, world);
        }
    }

    IREntity::EntityManager m_entityManager;
    IREntity::EntityId m_canvas = IREntity::kNullEntity;
    IREntity::EntityId m_rigRoot = IREntity::kNullEntity;
    std::vector<IREntity::EntityId> m_joints;

    // Pre-set entity slot so seedVoxelBoneSlots writes pool indices rather than
    // bailing on the "unwired allocator" path.
    static constexpr std::uint32_t kEntitySlot = 5;
};

TEST_F(SkinningPipelineConsistencyTest, BindPoseWorldPositionMatchesLocalPosition) {
    // At bind pose the skin matrix is identity: the GPU shader must output localPos
    // unchanged. Verifies all three bones simultaneously.
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(makeChainRig());
    buildRig(bind, bind); // worlds == bind

    JointMatrices sys;
    runFrame(sys);

    const auto& pool =
        IREntity::getComponent<IRComponents::C_VoxelPool>(m_canvas);
    const auto& voxelSet =
        IREntity::getComponent<IRComponents::C_VoxelSetNew>(m_rigRoot);
    const std::size_t start = voxelSet.voxelStartIdx_;
    const IRMath::vec3 localPos(1.0f, 0.5f, -0.5f);

    for (std::size_t i = 0; i < 3; ++i) {
        const std::uint32_t slot = pool.getTransformIndices()[start + i];
        const IRMath::mat4& mat =
            sys.jointStaging_[JointMatrices::localSlot(slot)].modelToWorld_;
        const IRMath::vec4 worldPos = mat * IRMath::vec4(localPos, 1.0f);
        EXPECT_NEAR(worldPos.x, localPos.x, kTolerance) << "bone " << i;
        EXPECT_NEAR(worldPos.y, localPos.y, kTolerance) << "bone " << i;
        EXPECT_NEAR(worldPos.z, localPos.z, kTolerance) << "bone " << i;
    }
}

TEST_F(SkinningPipelineConsistencyTest, PosedBoneTransformsVoxelViaStagingAndSlotChain) {
    // Joint 1 is posed 90 degrees about Z. The GPU pipeline (slot seeding + staging
    // fill) must agree with the direct skinMatrix computation for all three bones,
    // and the concrete world position for bone 1 is hand-verified.
    //
    // Geometry:
    //   bind[1] = SQT{scale=1, rot=identity, trans=(2,0,0)}
    //   worlds[1] = SQT{scale=1, rot=R(90°Z), trans=(2,0,0)}
    //   skinMatrix = T(2,0,0)*R(90°Z) * T(-2,0,0)
    //   localPos=(1,0,0) → bindInverse→(-1,0,0) → R(90°Z)→(0,-1,0) → T→(2,-1,0)
    const std::vector<IRMath::SQT> bind = IRPrefab::Rig::bindPose(makeChainRig());
    std::vector<IRMath::SQT> worlds = bind;
    worlds[1].rotation_ =
        IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi);
    buildRig(bind, worlds);

    JointMatrices sys;
    runFrame(sys);

    const auto& pool =
        IREntity::getComponent<IRComponents::C_VoxelPool>(m_canvas);
    const auto& voxelSet =
        IREntity::getComponent<IRComponents::C_VoxelSetNew>(m_rigRoot);
    const std::size_t start = voxelSet.voxelStartIdx_;
    const IRMath::vec3 localPos(1.0f, 0.0f, 0.0f);

    // Chain consistency check: staging matrix at pool-resolved slot matches the
    // direct skinMatrix computation for every bone.
    for (std::size_t boneIdx = 0; boneIdx < 3; ++boneIdx) {
        const std::uint32_t slot = pool.getTransformIndices()[start + boneIdx];
        const IRMath::mat4& mat =
            sys.jointStaging_[JointMatrices::localSlot(slot)].modelToWorld_;
        const IRMath::vec4 worldPos = mat * IRMath::vec4(localPos, 1.0f);
        const IRMath::mat4 direct =
            IRPrefab::Skeleton::skinMatrix(worlds[boneIdx], bind[boneIdx]);
        const IRMath::vec4 expected = direct * IRMath::vec4(localPos, 1.0f);
        EXPECT_NEAR(worldPos.x, expected.x, kTolerance) << "bone " << boneIdx;
        EXPECT_NEAR(worldPos.y, expected.y, kTolerance) << "bone " << boneIdx;
        EXPECT_NEAR(worldPos.z, expected.z, kTolerance) << "bone " << boneIdx;
    }

    // Concrete expected position for bone 1 (the posed joint).
    const std::uint32_t slot1 = pool.getTransformIndices()[start + 1];
    const IRMath::mat4& mat1 =
        sys.jointStaging_[JointMatrices::localSlot(slot1)].modelToWorld_;
    const IRMath::vec4 worldPos1 = mat1 * IRMath::vec4(localPos, 1.0f);
    EXPECT_NEAR(worldPos1.x, 2.0f, kTolerance);
    EXPECT_NEAR(worldPos1.y, -1.0f, kTolerance);
    EXPECT_NEAR(worldPos1.z, 0.0f, kTolerance);
}

} // namespace
