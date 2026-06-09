#ifndef SYSTEM_UPDATE_JOINT_MATRICES_H
#define SYSTEM_UPDATE_JOINT_MATRICES_H

// PURPOSE: per-frame skeletal joint skin-matrix upload (#605 Phase 2.2 / #1603).
//   For every C_Skeleton, computes each joint's skin matrix
//   (jointWorld × bindInverse, IRPrefab::Skeleton::skinMatrix) and writes it into
//   the existing binding-18 EntityTransformBuffer that UPDATE_VOXEL_POSITIONS_GPU
//   (#1396) already consumes. Cost is O(joints), not O(voxels). Phase 2.3 (#1605)
//   then seeds each skinned voxel's transform slot (`.w`) to `slotBase + bone_id`
//   so the existing c_update_voxel_positions prepass skins it with no new shader.
//
// WHY NO NEW BUFFER: the architect's #605 re-plan unifies skeletal skinning onto
//   the #1396 prepass — a per-bone skin matrix is exactly the per-voxel transform
//   indirection the prepass already does, with the slot pointing at a bone instead
//   of an entity. So this writes into the SAME binding-18 buffer; the speculative
//   binding-21 JointTransformBuffer is retired for the voxel path in Phase 2.4.
//
// SHARED-BUDGET PARTITION (no clobber by construction): binding-18's 4096 slots
//   are shared between dynamic voxel-set transforms and joint blocks. To keep the
//   prepass's contiguous `[0, maxSlotUsed_]` per-frame re-upload from ever
//   clobbering a joint slot, the two allocators own disjoint regions:
//     - voxel-set single slots grow UP from 0, capped at kJointTransformSlotBase
//       (UPDATE_VOXEL_POSITIONS_GPU::acquireTransformSlot).
//     - joint blocks are carved DOWN from kMaxGpuVoxelTransforms by this system's
//       acquireJointBlock (a contiguous block per skeleton; bone_id is the offset
//       within the block).
//   Each system uploads only its own region, so the two never overwrite each
//   other — and Metal's subData orphan copies head+tail, so the disjoint partial
//   uploads compose on both backends.
//
// SCHEDULING: register in the RENDER pipeline AFTER PROPAGATE_TRANSFORM (so each
//   joint's C_WorldTransform is the current posed transform) and BEFORE
//   UPDATE_VOXEL_POSITIONS_GPU (so binding 18 holds the joint matrices when the
//   prepass reads them in Phase 2.3). A creation that authors skeletons must
//   register UPDATE_VOXEL_POSITIONS_GPU too — this system reuses its buffer.
//
// ITERATION: iterates joints via the <C_Joint, C_WorldTransform> archetype, so a
//   joint's world transform arrives by dense-column iteration with NO per-entity
//   getComponent. The per-skeleton rest pose (bindPose_) and target slot are
//   gathered once per frame in beginTick (reading C_Skeleton's own fields, also
//   no foreign getComponent) into a joint -> (bind, slot) map the tick looks up.
//
// DEPENDENCIES: C_Skeleton (+ bindPose_), C_Joint, C_WorldTransform,
//   IRPrefab::Skeleton::skinMatrix (#1602), GpuVoxelTransform / binding-18 slot
//   constants (ir_render_types.hpp). No shader, no new GPU resource of its own.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/skeleton.hpp>

#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

template <> struct System<UPDATE_JOINT_MATRICES> {
    // binding 18 — the shared EntityTransformBuffer created by
    // UPDATE_VOXEL_POSITIONS_GPU. Resolved lazily on first upload because this
    // system's create() may run before the prepass's during pipeline setup, so
    // the named resource need not exist yet at create() time.
    Buffer *transformBuf_ = nullptr;

    // Per-frame CPU staging for the reserved joint region, indexed by
    // (absoluteSlot - kJointTransformSlotBase). Each active block is
    // identity-filled in beginTick (so severance holes / un-bind-posed joints
    // upload identity), occupied joints overwrite their slot in tick, and the
    // touched range uploads once in endTick.
    std::vector<GpuVoxelTransform> jointStaging_;

    // joint entity -> (rest pose, absolute binding-18 slot), rebuilt each
    // beginTick from the live C_Skeleton set. Only joints that have a bind-pose
    // entry are present; everything else stays at the identity-filled slot.
    // A vector (sorted by joint id at the end of beginTick, binary-searched in
    // tick) rather than a hash map: clear() keeps its capacity, so the per-frame
    // rebuild allocates nothing after warmup (a map churns a node per joint).
    struct JointTarget {
        IREntity::EntityId joint_ = IREntity::kNullEntity;
        IRMath::SQT bind_;
        std::uint32_t slot_ = 0;
    };
    std::vector<JointTarget> jointTargets_;

    // Persistent per-skeleton slot block, so a skeleton keeps the same slots
    // across frames (Phase 2.3 seeds voxels against this base once) and its
    // block is released when the skeleton disappears. base_ is joint 0's slot.
    struct SlotBlock {
        std::uint32_t base_ = 0;
        std::uint32_t count_ = 0;
    };
    std::unordered_map<IREntity::EntityId, SlotBlock> skeletonBlocks_;
    // Reused across frames: rig roots seen this beginTick (for the release diff).
    std::unordered_set<IREntity::EntityId> seenSkeletons_;

    // Block allocator over the reserved joint region
    // [kJointTransformSlotBase, kMaxGpuVoxelTransforms): a down-counter plus a
    // free-list keyed by block size (a skeleton's joint count rarely changes, so
    // exact-size recycling is the common path). Disjoint from the prepass's
    // up-counter by the kJointTransformSlotBase partition, so the two cannot
    // hand out the same slot.
    std::uint32_t nextJointBlockSlot_ = static_cast<std::uint32_t>(kMaxGpuVoxelTransforms);
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> freeJointBlocks_;

    // Local-slot range into jointStaging_ touched this frame, for a tight upload.
    int usedLo_ = -1;
    int usedHi_ = -1;

    // Absolute base slot of a contiguous `count`-slot block, or
    // kVoxelTransformStatic when the reserved joint region is exhausted (the
    // skeleton then renders unskinned rather than aliasing another's slots).
    std::uint32_t acquireJointBlock(std::uint32_t count) {
        if (count == 0) {
            return kVoxelTransformStatic;
        }
        auto recycled = freeJointBlocks_.find(count);
        if (recycled != freeJointBlocks_.end() && !recycled->second.empty()) {
            const std::uint32_t base = recycled->second.back();
            recycled->second.pop_back();
            return base;
        }
        if (nextJointBlockSlot_ < static_cast<std::uint32_t>(kJointTransformSlotBase) + count) {
            return kVoxelTransformStatic; // reserved joint region exhausted
        }
        nextJointBlockSlot_ -= count;
        return nextJointBlockSlot_;
    }

    void releaseJointBlock(std::uint32_t base, std::uint32_t count) {
        if (count == 0) {
            return;
        }
        freeJointBlocks_[count].push_back(base);
    }

    // jointStaging_ index for an absolute binding-18 slot.
    static int localSlot(std::uint32_t absoluteSlot) {
        return static_cast<int>(absoluteSlot) - kJointTransformSlotBase;
    }

    void beginTick() {
        if (jointStaging_.size() != static_cast<std::size_t>(kMaxGpuJointTransforms)) {
            jointStaging_.assign(kMaxGpuJointTransforms, GpuVoxelTransform{});
        }
        jointTargets_.clear();
        seenSkeletons_.clear();
        usedLo_ = -1;
        usedHi_ = -1;

        IREntity::forEachComponent<C_Skeleton>(
            [this](IREntity::EntityId rigRoot, C_Skeleton &skeleton) {
                const std::uint32_t jointCount =
                    static_cast<std::uint32_t>(skeleton.joints_.size());
                if (jointCount == 0) {
                    return; // not yet rigged — nothing to upload
                }
                seenSkeletons_.insert(rigRoot);

                // Reuse the skeleton's existing block; (re)allocate when it is
                // new or its joint count changed (release-then-acquire).
                SlotBlock &block = skeletonBlocks_[rigRoot];
                if (block.count_ != jointCount) {
                    if (block.count_ != 0) {
                        releaseJointBlock(block.base_, block.count_);
                    }
                    block.base_ = acquireJointBlock(jointCount);
                    block.count_ = (block.base_ == kVoxelTransformStatic) ? 0 : jointCount;
                }
                if (block.base_ == kVoxelTransformStatic) {
                    return; // out of joint-slot budget — skeleton stays unskinned
                }

                const int loLocal = localSlot(block.base_);
                const int hiLocal = loLocal + static_cast<int>(jointCount);
                // Identity-fill the block so severance holes (kNullEntity) and
                // not-yet-bind-posed joints upload identity (no deformation).
                for (int s = loLocal; s < hiLocal; ++s) {
                    jointStaging_[s].modelToWorld_ = IRMath::mat4(1.0f);
                }
                usedLo_ = (usedLo_ < 0) ? loLocal : IRMath::min(usedLo_, loLocal);
                usedHi_ = IRMath::max(usedHi_, hiLocal);

                // bindPose_ parallels joints_; a joint with no bind entry stays
                // identity (handled by the fill above + the skip here).
                const std::size_t bindCount = skeleton.bindPose_.size();
                for (std::uint32_t i = 0; i < jointCount; ++i) {
                    const IREntity::EntityId joint = skeleton.joints_[i];
                    if (joint == IREntity::kNullEntity ||
                        i >= static_cast<std::uint32_t>(bindCount)) {
                        continue;
                    }
                    jointTargets_.push_back(
                        JointTarget{joint, skeleton.bindPose_[i], block.base_ + i});
                }
            }
        );

        // Sort by joint id so tick can binary-search (one O(N log N) pass here
        // beats a per-joint hash map's per-frame node churn).
        std::sort(
            jointTargets_.begin(),
            jointTargets_.end(),
            [](const JointTarget &a, const JointTarget &b) { return a.joint_ < b.joint_; }
        );

        // Release blocks for skeletons that disappeared since last frame.
        for (auto it = skeletonBlocks_.begin(); it != skeletonBlocks_.end();) {
            if (seenSkeletons_.count(it->first) == 0) {
                releaseJointBlock(it->second.base_, it->second.count_);
                it = skeletonBlocks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void tick(IREntity::EntityId joint, C_Joint &, const C_WorldTransform &world) {
        const auto target = std::lower_bound(
            jointTargets_.begin(),
            jointTargets_.end(),
            joint,
            [](const JointTarget &entry, IREntity::EntityId id) { return entry.joint_ < id; }
        );
        if (target == jointTargets_.end() || target->joint_ != joint) {
            return; // not a bind-posed joint of any current skeleton
        }
        jointStaging_[localSlot(target->slot_)].modelToWorld_ =
            IRPrefab::Skeleton::skinMatrix(
                IRMath::SQT{world.scale_, world.rotation_, world.translation_},
                target->bind_
            );
    }

    void endTick() {
        if (usedHi_ < 0) {
            return; // no skeletons this frame — nothing to upload
        }
        if (transformBuf_ == nullptr) {
            // Asserts if absent: registering this system without
            // UPDATE_VOXEL_POSITIONS_GPU (which creates the buffer) is a
            // pipeline misconfiguration, not a silent no-op.
            transformBuf_ = IRRender::getNamedResource<Buffer>("EntityTransformBuffer");
        }
        // One partial upload of the touched joint region. Slots in any
        // freed-block gap inside [usedLo_, usedHi_) carry stale data but are
        // referenced by no voxel, so re-uploading them is harmless.
        const std::size_t offset =
            static_cast<std::size_t>(kJointTransformSlotBase + usedLo_) * sizeof(GpuVoxelTransform);
        const std::size_t size =
            static_cast<std::size_t>(usedHi_ - usedLo_) * sizeof(GpuVoxelTransform);
        transformBuf_->subData(static_cast<std::ptrdiff_t>(offset), size, &jointStaging_[usedLo_]);
    }

    static SystemId create() {
        // No shader, no buffer of its own — the skin matrices land in
        // UPDATE_VOXEL_POSITIONS_GPU's EntityTransformBuffer (resolved lazily in
        // endTick) and the prepass's existing shader applies them per voxel.
        return registerSystem<UPDATE_JOINT_MATRICES, C_Joint, C_WorldTransform>(
            "UpdateJointMatrices"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_UPDATE_JOINT_MATRICES_H */
