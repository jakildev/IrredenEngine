#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/render/components/component_canvas_fog_of_war.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/fog_of_war.hpp>
#include <irreden/render/systems/system_fog_reveal_eval.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_canvas.hpp>
#include <irreden/render/systems/system_fog_reveal_eval_shape.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/render/systems/system_update_joint_matrices.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/systems/system_update_voxel_set_children.hpp>
#include "common/allocation_counter.hpp"

#include <cstring>
#include <vector>

namespace {

using IRComponents::C_FogRevealed;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_VoxelPool;
using IRComponents::C_VoxelSetNew;
using IRComponents::C_WorldTransform;
using IRComponents::FogHiddenPolicy;
using IRComponents::FogOverride;

void expectMat4Near(const IRMath::mat4 &actual, const IRMath::mat4 &expected) {
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            EXPECT_NEAR(actual[column][row], expected[column][row], 1e-4f);
        }
    }
}

IRComponents::C_CanvasFogOfWar fogWithCircle(float radius) {
    IRComponents::C_CanvasFogOfWar fog{IRComponents::C_CanvasFogOfWar::HeadlessInit{}};
    IRComponents::C_CanvasFogOfWar::addVisionCircle(
        fog.observers_,
        0.0f,
        0.0f,
        radius,
        0.0f,
        0.0f,
        0.0f,
        IRComponents::kFogVisionZCostMirrorUp,
        0.0f
    );
    return fog;
}

TEST(FogGhostPolicyTest, CapturesHoldsRefreshesAndDiscards) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    fog.shown_ = true;
    C_WorldTransform first{};
    first.translation_ = {1.0f, 2.0f, 3.0f};
    IRPrefab::Fog::stepGhostLifecycle(fog, first, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostPoseValid_);
    EXPECT_EQ(fog.ghostPose_.translation_, first.translation_);

    fog.shown_ = false;
    C_WorldTransform moved{};
    moved.translation_ = {9.0f, 8.0f, 7.0f};
    IRPrefab::Fog::stepGhostLifecycle(fog, moved, true, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostHeld_);
    EXPECT_EQ(fog.ghostPose_.translation_, first.translation_);

    IRPrefab::Fog::stepGhostLifecycle(fog, moved, false, true, 0.75f, [](auto, auto) {
        return 1.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, PolicyAndOverridesCannotCreateGhostsFromInvalidPose) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    C_WorldTransform pose{};
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, true, false, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);

    fog.ghostHeld_ = true;
    fog.ghostPoseValid_ = true;
    fog.override_ = FogOverride::FORCE_HIDDEN;
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);

    fog.override_ = FogOverride::NONE;
    fog.ghostHeld_ = true;
    fog.ghostPoseValid_ = true;
    fog.hiddenPolicy_ = FogHiddenPolicy::HIDE;
    IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, false, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, StaggeredCaptureMakesOnlyTheFollowingHideEligible) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    fog.shown_ = true;
    C_WorldTransform shownPose{};
    shownPose.translation_ = {3.0f, 4.0f, 5.0f};

    IRPrefab::Fog::stepGhostLifecycle(fog, shownPose, true, false, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostPoseValid_);
    EXPECT_EQ(fog.ghostPose_.translation_, shownPose.translation_);

    fog.shown_ = false;
    C_WorldTransform hiddenPose{};
    hiddenPose.translation_ = {30.0f, 40.0f, 50.0f};
    IRPrefab::Fog::stepGhostLifecycle(fog, hiddenPose, true, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostHeld_);
    EXPECT_EQ(fog.ghostPose_.translation_, shownPose.translation_);

    C_FogRevealed switchedOnHide{};
    switchedOnHide.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    switchedOnHide.shown_ = false;
    IRPrefab::Fog::stepGhostLifecycle(
        switchedOnHide,
        hiddenPose,
        true,
        true,
        0.75f,
        [](auto, auto) { return 0.0f; }
    );
    EXPECT_FALSE(switchedOnHide.ghostHeld_);
    EXPECT_FALSE(switchedOnHide.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, ReshowRefreshesPoseAndForceHiddenDiscardsIt) {
    C_FogRevealed fog{};
    fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    fog.override_ = FogOverride::FORCE_REVEALED;
    fog.shown_ = true;
    C_WorldTransform refreshed{};
    refreshed.translation_ = {-2.0f, 7.0f, 1.0f};

    IRPrefab::Fog::stepGhostLifecycle(fog, refreshed, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostPoseValid_);
    EXPECT_EQ(fog.ghostPose_.translation_, refreshed.translation_);

    fog.override_ = FogOverride::NONE;
    fog.shown_ = false;
    IRPrefab::Fog::stepGhostLifecycle(fog, C_WorldTransform{}, true, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    ASSERT_TRUE(fog.ghostHeld_);
    EXPECT_EQ(fog.ghostPose_.translation_, refreshed.translation_);

    fog.override_ = FogOverride::FORCE_HIDDEN;
    IRPrefab::Fog::stepGhostLifecycle(fog, C_WorldTransform{}, false, true, 0.75f, [](auto, auto) {
        return 0.0f;
    });
    EXPECT_FALSE(fog.ghostHeld_);
    EXPECT_FALSE(fog.ghostPoseValid_);
}

TEST(FogGhostPolicyTest, VoxelRouteRetainsAlphaMaskAndExploredCarrier) {
    IREntity::EntityManager entityManager;
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{IRMath::ivec3(2, 1, 1)});
    auto &pool = IREntity::getComponent<C_VoxelPool>(canvas);
    C_VoxelSetNew voxelSet{IRMath::ivec3(2, 1, 1), IRMath::Color{255, 255, 255, 255}, true, canvas};
    voxelSet.voxels_[1].color_.alpha_ = 0;
    voxelSet.resyncAfterRawEdits();

    IRSystem::System<IRSystem::FOG_REVEAL_EVAL> system;
    system.activePool_ = &pool;
    system.activeCanvas_ = canvas;
    system.fogAttached_ = true;
    system.observers_ = fogWithCircle(2.0f).observers_;
    system.settings_.staggerPeriod_ = 1;
    system.restampedByWorker_.assign(1, 0u);
    system.pending_.reset(1);

    IREntity::EntityId entity = 1;
    C_FogRevealed revealed{};
    revealed.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    C_WorldTransform transform{};
    system.tick(entity, revealed, transform, voxelSet);
    system.endTick();
    ASSERT_TRUE(revealed.shown_);

    IRSystem::System<IRSystem::UPDATE_VOXEL_SET_CHILDREN> positionSystem;
    positionSystem.canvasToPool_[canvas] = &pool;
    positionSystem.pendingByWorker_.resize(1);
    positionSystem.tick(entity, voxelSet, transform);
    const auto frozenPositions = pool.getPositionGlobals();

    transform.translation_ = {10.0f, 0.0f, 0.0f};
    system.observers_.visionCircles_[0].x = 20.0f;
    system.pending_.reset(1);
    system.restampedByWorker_.assign(1, 0u);
    system.tick(entity, revealed, transform, voxelSet);
    system.endTick();

    EXPECT_TRUE(revealed.ghostHeld_);
    EXPECT_TRUE(voxelSet.ghostHeld_);
    EXPECT_FALSE(voxelSet.visible_);
    EXPECT_EQ(pool.getActiveMask()[0] & 0x3u, 0x1u);
    positionSystem.tick(entity, voxelSet, transform);
    ASSERT_EQ(pool.getPositionGlobals().size(), frozenPositions.size());
    EXPECT_EQ(
        std::memcmp(
            pool.getPositionGlobals().data(),
            frozenPositions.data(),
            frozenPositions.size() * sizeof(frozenPositions.front())
        ),
        0
    );
    for (const IRComponents::C_Voxel &voxel : voxelSet.voxels_) {
        const std::uint32_t factor =
            (voxel.reserved_ & IRComponents::VoxelReserved::kFogBodyFactorMask) >>
            IRComponents::VoxelReserved::kFogBodyFactorShift;
        EXPECT_EQ(factor, IRComponents::kFogStateExplored);
    }

    voxelSet.setLodCulled(true);
    EXPECT_EQ(pool.getActiveMask()[0] & 0x3u, 0u);
    voxelSet.setLodCulled(false);
    EXPECT_EQ(pool.getActiveMask()[0] & 0x3u, 0x1u);

    voxelSet.voxels_[0].color_.alpha_ = 0;
    voxelSet.voxels_[1].color_.alpha_ = 255;
    voxelSet.resyncAfterRawEdits();
    EXPECT_EQ(pool.getActiveMask()[0] & 0x3u, 0x2u);
}

TEST(FogGhostPolicyTest, HeldGpuVoxelTransformRemainsInsideUploadRangeAndFrozen) {
    IREntity::EntityManager entityManager;
    const IREntity::EntityId canvas = IREntity::createEntity(C_VoxelPool{IRMath::ivec3(1, 1, 1)});
    C_VoxelSetNew voxelSet{IRMath::ivec3(1), IRMath::Color{255, 255, 255, 255}, true, canvas};
    voxelSet.gpuTransformSlot_ = 3;

    IRSystem::System<IRSystem::UPDATE_VOXEL_POSITIONS_GPU> system;
    C_WorldTransform shownPose{};
    shownPose.translation_ = {1.0f, 2.0f, 3.0f};
    system.beginTick();
    system.tick(voxelSet, shownPose);
    const IRMath::mat4 frozen = system.transforms_[3].modelToWorld_;

    voxelSet.ghostHeld_ = true;
    C_WorldTransform hiddenPose{};
    hiddenPose.translation_ = {30.0f, 20.0f, 10.0f};
    system.beginTick();
    system.tick(voxelSet, hiddenPose);

    EXPECT_TRUE(system.anyDynamic_);
    EXPECT_EQ(system.maxSlotUsed_, 3);
    expectMat4Near(system.transforms_[3].modelToWorld_, frozen);
}

TEST(FogGhostPolicyTest, HeldSkeletonRetainsJointBlockWhileLiveSiblingUpdates) {
    IREntity::EntityManager entityManager;
    const auto makeRig = [](float x) {
        const IREntity::EntityId joint = IREntity::createEntity(IRComponents::C_Joint{});
        auto &jointWorld = IREntity::getComponent<C_WorldTransform>(joint);
        jointWorld.translation_ = {x, 0.0f, 0.0f};

        IRComponents::C_Skeleton skeleton;
        skeleton.joints_.push_back(joint);
        skeleton.bindPose_.push_back(IRMath::SQT{});
        const IREntity::EntityId root = IREntity::createEntity(C_VoxelSetNew{}, skeleton);
        return std::pair{root, joint};
    };
    const auto heldRig = makeRig(1.0f);
    const auto liveRig = makeRig(2.0f);

    IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES> system;
    const auto runFrame = [&](const auto &rigs) {
        system.beginTick();
        for (const auto &[root, joint] : rigs) {
            static_cast<void>(root);
            IRComponents::C_Joint tag;
            system.tick(joint, tag, IREntity::getComponent<C_WorldTransform>(joint));
        }
    };
    const std::array rigs{heldRig, liveRig};
    runFrame(rigs);

    const auto heldBlock = system.skeletonBlocks_.at(heldRig.first);
    const auto liveBlock = system.skeletonBlocks_.at(liveRig.first);
    const IRMath::mat4 heldBefore =
        system.jointStaging_[system.localSlot(heldBlock.base_)].modelToWorld_;
    const IRMath::mat4 liveBefore =
        system.jointStaging_[system.localSlot(liveBlock.base_)].modelToWorld_;

    IREntity::getComponent<C_VoxelSetNew>(heldRig.first).ghostHeld_ = true;
    IREntity::getComponent<C_WorldTransform>(heldRig.second).translation_.x = 11.0f;
    IREntity::getComponent<C_WorldTransform>(liveRig.second).translation_.x = 12.0f;
    runFrame(rigs);

    const int heldSlot = system.localSlot(heldBlock.base_);
    const int liveSlot = system.localSlot(liveBlock.base_);
    expectMat4Near(system.jointStaging_[heldSlot].modelToWorld_, heldBefore);
    EXPECT_NE(system.jointStaging_[liveSlot].modelToWorld_, liveBefore);
    EXPECT_LE(system.usedLo_, heldSlot);
    EXPECT_GT(system.usedHi_, heldSlot);
}

TEST(FogGhostPolicyTest, ShapeRoutePublishesFrozenPoseAndExploredFactor) {
    auto fog = fogWithCircle(2.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_SHAPE> system;
    system.fog_ = &fog;
    system.observers_ = fog.observers_;
    system.settings_.staggerPeriod_ = 1;
    system.pendingHeld_.reset(1);

    IREntity::EntityId entity = 7;
    C_FogRevealed revealed{};
    revealed.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    C_WorldTransform transform{};
    transform.translation_ = {1.0f, 0.0f, 0.0f};
    C_ShapeDescriptor shape{};
    system.tick(entity, revealed, transform, shape);
    ASSERT_TRUE(revealed.shown_);

    transform.translation_ = {10.0f, 0.0f, 0.0f};
    system.observers_.visionCircles_[0].x = 20.0f;
    system.tick(entity, revealed, transform, shape);
    system.endTick();

    ASSERT_TRUE(revealed.ghostHeld_);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_FOG_HIDDEN, 0u);
    EXPECT_NE(shape.flags_ & IRMath::SDF::SHAPE_FLAG_FOG_GHOST, 0u);
    EXPECT_EQ(shape.fogBodyFactor_, IRComponents::kFogStateExplored);
    ASSERT_EQ(system.heldGhostPoses_.size(), 1u);
    EXPECT_EQ(system.heldGhostPoses_[0].entity_, entity);
    EXPECT_EQ(system.heldGhostPoses_[0].pose_.translation_, IRMath::vec3(1.0f, 0.0f, 0.0f));
}

TEST(FogGhostPolicyTest, CanvasRoutePublishesFrozenPoseAndExploredFactor) {
    IREntity::EntityManager entityManager;
    const IREntity::EntityId detached = IREntity::createEntity(IRComponents::C_DetachedCanvas{});
    auto fog = fogWithCircle(2.0f);
    IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS> system;
    system.fog_ = &fog;
    system.settings_.staggerPeriod_ = 1;

    const IREntity::EntityId entity = 9;
    C_FogRevealed revealed{};
    revealed.hiddenPolicy_ = FogHiddenPolicy::GHOST;
    C_WorldTransform transform{};
    transform.translation_ = {1.0f, 0.0f, 0.0f};
    IRComponents::C_EntityCanvas canvas{};
    canvas.canvasEntity_ = detached;
    system.tick(entity, revealed, transform, canvas);
    ASSERT_TRUE(revealed.shown_);

    transform.translation_ = {10.0f, 0.0f, 0.0f};
    fog.observers_.visionCircles_[0].x = 20.0f;
    system.tick(entity, revealed, transform, canvas);
    system.endTick();

    ASSERT_TRUE(revealed.ghostHeld_);
    EXPECT_FALSE(canvas.fogHidden_);
    EXPECT_TRUE(canvas.fogGhost_);
    EXPECT_FLOAT_EQ(
        canvas.fogRevealFactor_,
        static_cast<float>(IRComponents::kFogStateExplored) / 255.0f
    );
    ASSERT_EQ(system.heldGhostPoses_.size(), 1u);
    EXPECT_EQ(system.heldGhostPoses_[0].entity_, entity);
    EXPECT_EQ(system.heldGhostPoses_[0].pose_.translation_, IRMath::vec3(1.0f, 0.0f, 0.0f));
}

TEST(FogGhostPolicyTest, CanvasRouteStampsPlacementFromPublishedFrozenPose) {
    IREntity::EntityManager entityManager;
    IRSystem::SystemManager systemManager;
    const IRSystem::SystemId evalId = IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS>::create();
    auto *eval =
        IRSystem::getSystemParams<IRSystem::System<IRSystem::FOG_REVEAL_EVAL_CANVAS>>(evalId);
    const IREntity::EntityId camera = IREntity::createEntity();
    IREntity::setName(camera, "camera");

    const IREntity::EntityId detached =
        IREntity::createEntity(IRComponents::C_CanvasLocalRotation{});
    const IREntity::EntityId owner = IREntity::createEntity();
    C_WorldTransform frozenPose{};
    frozenPose.translation_ = {1.25f, -2.5f, 3.75f};
    eval->heldGhostPoses_.push_back({owner, frozenPose});

    C_WorldTransform livePose{};
    livePose.translation_ = {20.0f, 30.0f, 40.0f};
    IRComponents::C_EntityCanvas canvas{detached, IRMath::ivec2(64)};
    canvas.fogGhost_ = true;
    IRSystem::System<IRSystem::PROPAGATE_CANVAS_ROTATION> propagate;
    propagate.beginTick();
    propagate.tick(
        owner,
        livePose,
        IRComponents::C_RotationMode{IRComponents::RotationMode::DETACHED_REVOXELIZE},
        canvas
    );

    const auto &placement = IREntity::getComponent<IRComponents::C_CanvasLocalRotation>(detached);
    EXPECT_EQ(placement.ownerWorldTranslation_, frozenPose.translation_);
    EXPECT_EQ(placement.worldCellOffset_, IRMath::vec3(1.0f, -2.0f, 4.0f));
    EXPECT_NE(placement.ownerWorldTranslation_, livePose.translation_);
}

TEST(FogGhostPolicyTest, WarmHeldLifecycleIsAllocationFreeAndLinear) {
    std::vector<C_FogRevealed> fogs(262144);
    C_WorldTransform pose{};
    for (auto &fog : fogs) {
        fog.hiddenPolicy_ = FogHiddenPolicy::GHOST;
        fog.ghostHeld_ = true;
        fog.ghostPoseValid_ = true;
    }
    auto run = [&]() {
        for (auto &fog : fogs) {
            IRPrefab::Fog::stepGhostLifecycle(fog, pose, false, true, 0.75f, [](auto, auto) {
                return 0.0f;
            });
        }
    };
    run();
    IRTest::AllocationCounter allocations;
    run();
    EXPECT_EQ(allocations.allocations(), 0u);
}

} // namespace
