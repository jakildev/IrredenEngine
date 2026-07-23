#ifndef SYSTEM_UPDATE_JOINT_MATRICES_H
#define SYSTEM_UPDATE_JOINT_MATRICES_H

// PURPOSE: per-frame skeletal joint skin-matrix upload (#605 Phase 2.2 / #1603).
//   For every C_Skeleton, computes each joint's skin matrix
//   (jointWorld × bindInverse, IRPrefab::Skeleton::skinMatrix) and writes it into
//   the existing binding-18 EntityTransformBuffer that UPDATE_VOXEL_POSITIONS_GPU
//   (#1396) already consumes. Cost is O(joints), not O(voxels). Phase 2.3 (#1605,
//   `seedVoxelBoneSlots` below) seeds each skinned voxel's transform slot (`.w`)
//   to `slotBase + bone_id` so the existing c_update_voxel_positions prepass
//   skins it with no new shader.
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
//   getComponent in tick. The per-skeleton rest pose (bindPose_) and target slot are
//   gathered once per frame in beginTick (reading C_Skeleton's own fields).
//   seedVoxelBoneSlots (called from beginTick, block-realloc-only) looks up
//   C_VoxelSetNew and C_VoxelPool via getComponentOptional/getComponent — compliant:
//   rare, beginTick-only, no iterator-invalidation risk.
//
// DEPENDENCIES: C_Skeleton (+ bindPose_), C_Joint, C_WorldTransform,
//   IRPrefab::Skeleton::skinMatrix (#1602), GpuVoxelTransform / binding-18 slot
//   constants (ir_render_types.hpp). No shader, no new GPU resource of its own.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/systems/system_update_voxel_positions_gpu.hpp>
#include <irreden/voxel/components/component_joint.hpp>
#include <irreden/voxel/components/component_skeleton.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
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

    // Voxels of `voxelSet` safe to address in its live pool: the stable
    // `numVoxels_` scalar clamped to the pool's writable tail. Reads the LIVE
    // pool size, never the captured `voxelSet.voxels_` span — a between-frame
    // canvas archetype migration deep-copies C_VoxelPool and frees that span's
    // backing while `voxelStartIdx_` / `numVoxels_` stay valid (the #2032
    // dangling-read hazard; mirrors C_VoxelSetNew::updateAsChild). The clamp
    // guarantees `[voxelStartIdx_, voxelStartIdx_ + count)` never runs past
    // live storage even if a migration shrank the pool.
    static std::size_t
    liveWritableVoxelCount(const C_VoxelSetNew &voxelSet, const C_VoxelPool &pool) {
        const std::size_t poolSize = pool.getColors().size();
        const std::size_t startIdx = voxelSet.voxelStartIdx_;
        const std::size_t writableTail = poolSize > startIdx ? poolSize - startIdx : 0u;
        return IRMath::min(static_cast<std::size_t>(voxelSet.numVoxels_), writableTail);
    }

    // Reused scratch for seedVoxelBoneSlots' per-voxel slot array (rare calls,
    // but the capacity persists so repeated re-rigs don't churn).
    std::vector<std::uint32_t> boneSlotStaging_;

    // Phase 2.3 (#1605): point each voxel of the rig root's voxel set at its
    // bone's binding-18 slot (`block.base_ + bone_id`) so the existing
    // c_update_voxel_positions prepass skins it — no new shader, no stage-1
    // change. A bone_id outside the joint list falls back to the SET's entity
    // slot (rigid follow of the rig root), which is also what the whole set
    // resolves to at bind pose (skinMatrix == rig world there). Lazily opts the
    // set into the prepass via `IRPrefab::VoxelTransform::acquireSlot`; when no
    // entity slot is available (allocator unwired / budget exhausted) the set
    // stays CPU-direct — unrigged content pays nothing and renders unchanged.
    //
    // Called automatically from beginTick whenever the skeleton's slot block is
    // (re)allocated (first rig, joint count change). After re-painting
    // C_Voxel::bone_id_ without changing the joint list, call
    // `IRPrefab::JointTransform::seedVoxelBoneSlots(rigRoot)` to re-stamp.
    void seedVoxelBoneSlots(IREntity::EntityId rigRoot) {
        const auto voxelSetOpt = IREntity::getComponentOptional<C_VoxelSetNew>(rigRoot);
        if (!voxelSetOpt.has_value()) {
            return; // skeleton without a voxel set (pure rig) — nothing to seed
        }
        C_VoxelSetNew &voxelSet = *voxelSetOpt.value();
        if (voxelSet.numVoxels_ == 0 || voxelSet.canvasEntity_ == IREntity::kNullEntity) {
            return; // headless / staged set — seeded when it lands in a pool
        }
        const auto blockIt = skeletonBlocks_.find(rigRoot);
        if (blockIt == skeletonBlocks_.end() || blockIt->second.count_ == 0) {
            return; // no joint block — skeleton renders unskinned
        }
        if (voxelSet.gpuTransformSlot_ == kVoxelTransformStatic) {
            voxelSet.gpuTransformSlot_ = IRPrefab::VoxelTransform::acquireSlot();
            if (voxelSet.gpuTransformSlot_ == kVoxelTransformStatic) {
                return; // no entity slot available — set stays CPU-direct
            }
        }
        const SlotBlock &block = blockIt->second;
        // Read bone ids from the live pool, never the captured `voxelSet.voxels_`
        // span (see liveWritableVoxelCount — derefing the stale span here is the
        // #2032 first-frame segfault).
        C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(voxelSet.canvasEntity_);
        const std::size_t startIdx = voxelSet.voxelStartIdx_;
        const std::size_t count = liveWritableVoxelCount(voxelSet, pool);
        if (count == 0) {
            return; // pool storage shrank below this set's range — nothing safe to seed
        }
        const std::vector<C_Voxel> &poolVoxels = pool.getColors();
        boneSlotStaging_.resize(count);
        for (std::size_t i = 0; i < count; ++i) {
            const std::uint32_t boneId = poolVoxels[startIdx + i].bone_id_;
            boneSlotStaging_[i] =
                (boneId < block.count_) ? block.base_ + boneId : voxelSet.gpuTransformSlot_;
        }
        pool.setTransformIndicesForRange(startIdx, boneSlotStaging_);
    }

    // Re-stamp a rig root's voxel set to rigid follow (entity slot) when the
    // skeleton's joint block was NOT allocated (exhaustion) or is being released
    // (skeleton disappeared). Without this, stale per-bone `.w` stamps from the
    // prior block persist and may alias a recycled foreign block's joints —
    // delivering the "renders unskinned rather than aliasing" promise from
    // acquireJointBlock's doc comment.
    void resetVoxelBoneSlotsToRigid(IREntity::EntityId rigRoot) {
        const auto voxelSetOpt = IREntity::getComponentOptional<C_VoxelSetNew>(rigRoot);
        if (!voxelSetOpt.has_value()) {
            return;
        }
        C_VoxelSetNew &voxelSet = *voxelSetOpt.value();
        if (voxelSet.numVoxels_ == 0 || voxelSet.canvasEntity_ == IREntity::kNullEntity ||
            voxelSet.gpuTransformSlot_ == kVoxelTransformStatic) {
            return; // never seeded — nothing to reset
        }
        // Stable `numVoxels_` clamped to the live pool, not `voxelSet.voxels_.size()`
        // — the captured span is invalidated by a canvas archetype migration (see
        // liveWritableVoxelCount, #2032).
        C_VoxelPool &pool = IREntity::getComponent<C_VoxelPool>(voxelSet.canvasEntity_);
        const std::size_t count = liveWritableVoxelCount(voxelSet, pool);
        if (count == 0) {
            return;
        }
        pool.setTransformIndexForRange(voxelSet.voxelStartIdx_, count, voxelSet.gpuTransformSlot_);
    }

    void beginTick() {
        if (jointStaging_.size() != static_cast<std::size_t>(kMaxGpuJointTransforms)) {
            jointStaging_.assign(kMaxGpuJointTransforms, GpuVoxelTransform{});
        }
        jointTargets_.clear();
        seenSkeletons_.clear();
        usedLo_ = -1;
        usedHi_ = -1;

        IREntity::forEachComponent<C_Skeleton>([this](
                                                   IREntity::EntityId rigRoot,
                                                   C_Skeleton &skeleton
                                               ) {
            const std::uint32_t jointCount = static_cast<std::uint32_t>(skeleton.joints_.size());
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
                // count_ = 0 on exhaustion so next-frame's count_ != jointCount
                // branch fires again and re-attempts the acquire. This is
                // intentional: exhaustion is exceptional and cheap to retry.
                block.count_ = (block.base_ == kVoxelTransformStatic) ? 0 : jointCount;
                if (block.count_ != 0) {
                    // The bone→slot mapping was just established or moved —
                    // re-stamp the rig's voxel set so binding 17 re-seeds
                    // this frame (Phase 2.3, #1605).
                    seedVoxelBoneSlots(rigRoot);
                } else {
                    // Exhaustion: re-stamp voxels to rigid follow so stale
                    // per-bone `.w` stamps don't alias a recycled block.
                    resetVoxelBoneSlotsToRigid(rigRoot);
                }
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
                if (joint == IREntity::kNullEntity || i >= static_cast<std::uint32_t>(bindCount)) {
                    continue;
                }
                jointTargets_.push_back(JointTarget{joint, skeleton.bindPose_[i], block.base_ + i});
            }
        });

        // Sort by joint entity id so tick can binary-search (O(N log N) once per
        // frame, O(log N) per joint in tick). Chosen over an unordered_map because
        // clear() keeps the vector's capacity, so the rebuild allocates nothing
        // after warmup; a map would churn one heap node per joint per frame.
        //
        // Scalability: for large rigs (hundreds of joints across many skeletons)
        // the sort cost is proportional to total joint count and the binary search
        // is O(log N_total). If this becomes a bottleneck, the sort + search can be
        // eliminated by adding (skeletonEntity_, boneIndex_) to C_Joint — tick
        // would then compute the slot directly (skeletonBlocks_[skeletonEntity_].base_
        // + boneIndex_) in O(1) per joint with a single unordered_map lookup.
        //
        // Hierarchy (topological) ordering: processing joints in parent-before-child
        // order is not required here because PROPAGATE_TRANSFORM has already composed
        // each joint's C_WorldTransform before this system runs. Sorting by entity id
        // is a data-layout choice only, not a correctness constraint.
        std::sort(
            jointTargets_.begin(),
            jointTargets_.end(),
            [](const JointTarget &a, const JointTarget &b) { return a.joint_ < b.joint_; }
        );

        // Release blocks for skeletons that disappeared since last frame.
        for (auto it = skeletonBlocks_.begin(); it != skeletonBlocks_.end();) {
            if (seenSkeletons_.count(it->first) == 0) {
                resetVoxelBoneSlotsToRigid(it->first);
                releaseJointBlock(it->second.base_, it->second.count_);
                it = skeletonBlocks_.erase(it);
            } else {
                ++it;
            }
        }
    }

    void tick(IREntity::EntityId joint, C_Joint &, const C_WorldTransform &world) {
        // O(log N_total) lookup into the sorted jointTargets_ built each beginTick.
        // See the sort comment above for the trade-off analysis and the upgrade path.
        const auto target = std::lower_bound(
            jointTargets_.begin(),
            jointTargets_.end(),
            joint,
            [](const JointTarget &entry, IREntity::EntityId id) { return entry.joint_ < id; }
        );
        if (target == jointTargets_.end() || target->joint_ != joint) {
            return; // not a bind-posed joint of any current skeleton
        }
        jointStaging_[localSlot(target->slot_)].modelToWorld_ = IRPrefab::Skeleton::skinMatrix(
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

namespace IRPrefab::JointTransform {

// Handle to the UPDATE_JOINT_MATRICES skeleton slot blocks — the same shape as
// `IRPrefab::VoxelTransform` (the entity-slot half of the shared binding-18
// budget). The id is resolved from SystemManager's `SystemName` registry
// (#2526), so creating the system is all the wiring there is; a creation that
// rigs voxel sets needs no follow-up call.
inline IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES> *system() {
    const IRSystem::SystemId systemId = IRSystem::findSystem(IRSystem::UPDATE_JOINT_MATRICES);
    if (systemId == IREntity::kNullEntity) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::UPDATE_JOINT_MATRICES>>(systemId);
}

// DEPRECATED — registration self-wires via SystemManager; remove once
// out-of-tree creations migrate. Kept as a no-op so an existing call site
// keeps compiling (engine API removal rule).
inline void setSystem(IRSystem::SystemId) {}

// Absolute binding-18 slot of joint 0 in `rigRoot`'s skeleton block —
// per-voxel skinning slots are `slotBase + bone_id` (#605 Phase 2.3). Returns
// `IRRender::kVoxelTransformStatic` when the system is unwired, the skeleton
// has no block yet (first UPDATE_JOINT_MATRICES tick hasn't run), or the
// joint region is exhausted.
inline std::uint32_t slotBase(IREntity::EntityId rigRoot) {
    auto *p = system();
    if (p == nullptr) {
        return IRRender::kVoxelTransformStatic;
    }
    const auto blockIt = p->skeletonBlocks_.find(rigRoot);
    if (blockIt == p->skeletonBlocks_.end() || blockIt->second.count_ == 0) {
        return IRRender::kVoxelTransformStatic;
    }
    return blockIt->second.base_;
}

// Re-stamp the per-voxel bone→slot indices of `rigRoot`'s voxel set (see the
// member doc on `System<UPDATE_JOINT_MATRICES>::seedVoxelBoneSlots`). Call
// after painting `C_Voxel::bone_id_` on an already-rigged set; rig/joint-count
// changes re-stamp automatically. No-op when the system is unwired.
inline void seedVoxelBoneSlots(IREntity::EntityId rigRoot) {
    if (auto *p = system()) {
        p->seedVoxelBoneSlots(rigRoot);
    }
}

} // namespace IRPrefab::JointTransform

#endif /* SYSTEM_UPDATE_JOINT_MATRICES_H */
