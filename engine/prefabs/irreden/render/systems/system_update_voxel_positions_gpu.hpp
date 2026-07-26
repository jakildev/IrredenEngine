#ifndef SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H
#define SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H

// PURPOSE: GPU voxel-position prepass (#1396) — the transform-indirection
//   world-position authority for dynamic-transform voxels. Each voxel carries
//   an `entityTransformIndex` in `C_VoxelPool` (bit-packed into the local-position
//   SSBO `.w` lane, binding 17); the compute shader computes
//   `world = EntityTransform[idx] * localPos` and writes the global
//   position SSBO (binding 5) ahead of `VOXEL_TO_TRIXEL_STAGE_1`. The per-frame
//   CPU→GPU upload is O(GPU-transformed voxel sets) — one SO(3)+translation
//   matrix each — instead of O(rotating voxels), which is the whole reason for
//   the GPU path over CPU re-upload (#1272 hordes).
//
// BYTE-IDENTICAL: every voxel defaults to `kVoxelTransformStatic`, and the
//   shader early-returns on that sentinel, so the prepass never touches a
//   static voxel's binding-5 slot — the CPU-direct pending-range flush in
//   `VOXEL_TO_TRIXEL_STAGE_1` still owns those slots, exactly as before. A scene
//   with no GPU-transformed voxel sets does no dispatch at all (`endTick`
//   returns early), so existing scenes are byte-identical by construction.
//
// SHARED SUBSTRATE: the indirection is generic — an entity transform today, a
//   bone transform for skeletal voxels (#605) tomorrow. A consumer routes a
//   voxel set through this prepass (opt in via gpuTransformSlot_) instead of
//   re-deriving positions on the CPU; shape_debug --gpu-voxel-smoke is the
//   reference exerciser.
//
// LIMITATIONS (foundation scope):
//   - Single dynamic pool per frame: if GPU-transformed sets span multiple
//     canvases, only the last-ticked canvas's pool is dispatched (the binding
//     5 / 17 / 18 SSBOs are single-instance, shared — same constraint
//     `VOXEL_TO_TRIXEL_STAGE_1` documents for its multi-canvas position upload).
//   - The CPU global mirror (`m_voxelPositionsGlobal`) for a dynamic set holds
//     the un-rotated translation-only position (UPDATE_VOXEL_SET_CHILDREN still
//     computes it as a re-seed fallback but does not queue it). Cull/picking of
//     a dynamic set read that mirror, so they lag the GPU rotation until a
//     follow-up folds the prepass output back into the cull frame.
//
// RESOLVED: the canvas-switch re-seed in VOXEL_TO_TRIXEL_STAGE_1 no longer
//   clobbers this prepass's binding-5 output — it gates on static voxel runs
//   (`flushStaticPositionRanges`, #1396 commit e04f29cd) and leaves
//   GPU-transformed slots intact.
//
// DEPENDENCIES: C_VoxelPool, C_VoxelSetNew, C_WorldTransform,
//   IRRender::VoxelGpuPosition, GpuVoxelTransform, GPUUpdateParams
//   (ir_render_types.hpp), shaders/c_update_voxel_positions.{glsl,metal}.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/voxel_pool_allocation.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_world_transform.hpp>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

template <> struct System<UPDATE_VOXEL_POSITIONS_GPU> {
    ShaderProgram *program_ = nullptr;
    // binding 17 — entity-local positions, with the per-voxel transform slot
    // bit-packed into each entry's `.w` lane (see kVoxelTransformStatic doc;
    // Metal has no free buffer index for a dedicated slot SSBO).
    Buffer *localPosBuf_ = nullptr;
    Buffer *transformBuf_ = nullptr; // binding 18 — per-slot SO(3)+translation
    Buffer *paramsBuf_ = nullptr;    // binding 19 — voxelCount
    // binding 5 — shared with VOXEL_TO_TRIXEL_STAGE_1's "VoxelPositionBuffer".
    // Resolved lazily on first dispatch: STAGE_1::create() runs *after* this
    // system's create() during pipeline setup, so the buffer does not exist yet.
    Buffer *globalPosBuf_ = nullptr;

    // Per-frame CPU staging of the transform slots, uploaded [0, maxSlotUsed_].
    std::vector<GpuVoxelTransform> transforms_;
    // Reused staging for the binding-17 seed: local position in `.pos_`, the
    // bit-cast transform slot in `.pad_`. Built only on a (re)seed, not per frame.
    std::vector<IRRender::VoxelGpuPosition> localStaging_;
    int maxSlotUsed_ = -1;
    bool anyDynamic_ = false;
    C_VoxelPool *touchedPool_ = nullptr;
    IREntity::EntityId touchedCanvas_ = IREntity::kNullEntity;

    // Foreign pool lookup cache, valid only within one tick pass (reset in
    // beginTick — a between-frame canvas migration would invalidate it).
    IREntity::EntityId lastTickCanvas_ = IREntity::kNullEntity;
    C_VoxelPool *lastTickPool_ = nullptr;

    // Canvas whose immutable local positions + packed per-voxel transform
    // slots were last seeded into binding 17. A switch re-seeds the live range.
    IREntity::EntityId lastSeededCanvas_ = IREntity::kNullEntity;

    // EntityTransformBuffer slot allocator (#1396). A consumer acquires a slot
    // (via `IRPrefab::VoxelTransform`) when it opts a voxel set into the prepass
    // and releases it when the set no longer needs a dynamic transform. A
    // monotonic high-water plus a recycle stack keeps slot ids dense and bounded
    // by kMaxGpuVoxelTransforms. Acquire/release are rare (set lifecycle), not
    // per-frame.
    std::uint32_t nextFreeSlot_ = 0;
    std::vector<std::uint32_t> recycledSlots_;

    std::uint32_t acquireTransformSlot() {
        if (!recycledSlots_.empty()) {
            const std::uint32_t slot = recycledSlots_.back();
            recycledSlots_.pop_back();
            return slot;
        }
        // Stop at kJointTransformSlotBase, not kMaxGpuVoxelTransforms: the
        // reserved high region [kJointTransformSlotBase, kMaxGpuVoxelTransforms)
        // belongs to UPDATE_JOINT_MATRICES (#1603). Partitioning the shared
        // binding-18 budget keeps this system's contiguous `[0, maxSlotUsed_]`
        // re-upload from ever clobbering a joint slot.
        if (nextFreeSlot_ < static_cast<std::uint32_t>(kJointTransformSlotBase)) {
            return nextFreeSlot_++;
        }
        return kVoxelTransformStatic; // budget exhausted — caller stays CPU-direct
    }

    void releaseTransformSlot(std::uint32_t slot) {
        // Guard at kJointTransformSlotBase: a joint-region slot passed here
        // (corrupted caller) must not enter the voxel-set recycle stack and
        // later be re-issued to a voxel set, which would quietly corrupt the
        // partition. kVoxelTransformStatic (0xFFFFFFFF) is always > this bound
        // so it is silently dropped as well.
        if (slot < static_cast<std::uint32_t>(kJointTransformSlotBase)) {
            recycledSlots_.push_back(slot);
        }
    }

    void beginTick() {
        if (transforms_.size() != static_cast<std::size_t>(kMaxGpuVoxelTransforms)) {
            transforms_.assign(kMaxGpuVoxelTransforms, GpuVoxelTransform{});
        }
        maxSlotUsed_ = -1;
        anyDynamic_ = false;
        touchedPool_ = nullptr;
        touchedCanvas_ = IREntity::kNullEntity;
        lastTickCanvas_ = IREntity::kNullEntity;
        lastTickPool_ = nullptr;
    }

    void tick(C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform) {
        if (voxelSet.gpuTransformSlot_ == kVoxelTransformStatic || voxelSet.numVoxels_ == 0) {
            return;
        }
        const std::uint32_t slot = voxelSet.gpuTransformSlot_;
        // kJointTransformSlotBase, not kMaxGpuVoxelTransforms: a mis-seeded
        // slot in the joint-reserved region [kJointTransformSlotBase,
        // kMaxGpuVoxelTransforms) must not write transforms_[slot] here —
        // that would drag maxSlotUsed_ into the joint region and the
        // [0, maxSlotUsed_] re-upload would clobber freshly-written joint
        // matrices from UPDATE_JOINT_MATRICES in the same tick.
        if (slot >= static_cast<std::uint32_t>(kJointTransformSlotBase)) {
            return; // sentinel or joint-reserved region — set stays CPU-direct
        }
        // CPU mirror of the GLSL/Metal `transform * localPos`: sqtToMat4 is
        // bit-identical to the shader-side helper, so the same operands classify
        // the same on both sides (the CPU↔GPU consistency the plan flags).
        transforms_[slot].modelToWorld_ = IRMath::sqtToMat4(
            worldTransform.scale_,
            worldTransform.rotation_,
            worldTransform.translation_
        );
        maxSlotUsed_ = IRMath::max(maxSlotUsed_, static_cast<int>(slot));
        anyDynamic_ = true;

        // Resolve the set's pool via its canvas (a foreign entity, not this
        // iterating set — the allowed getComponent pattern; cached per canvas).
        IREntity::EntityId canvas = voxelSet.canvasEntity_;
        if (canvas == IREntity::kNullEntity) {
            canvas = IRRender::getActiveCanvasEntity();
        }
        if (canvas != lastTickCanvas_ || lastTickPool_ == nullptr) {
            lastTickPool_ = &IREntity::getComponent<C_VoxelPool>(canvas);
            lastTickCanvas_ = canvas;
        }
        touchedPool_ = lastTickPool_;
        touchedCanvas_ = canvas;
    }

    void endTick() {
        if (!anyDynamic_ || touchedPool_ == nullptr) {
            return; // no GPU-transformed voxels this frame — byte-identical
        }
        if (globalPosBuf_ == nullptr) {
            globalPosBuf_ = IRRender::getNamedResource<Buffer>("VoxelPositionBuffer");
        }
        C_VoxelPool &pool = *touchedPool_;
        const int liveCount = pool.getLiveVoxelCount();
        if (liveCount <= 0 || globalPosBuf_ == nullptr) {
            return;
        }

        // Seed binding 17 (entity-local position in .xyz, packed transform slot
        // in .w) once per pool, and re-seed when a set newly opts into / out of
        // GPU indirection (its transform-index range was just queued) or on a
        // canvas switch. Rigid in entity-local space, so steady state never
        // re-seeds — only the per-slot matrices upload each frame.
        const bool indicesChanged = !pool.getPendingTransformIndexRanges().empty();
        if (lastSeededCanvas_ != touchedCanvas_ || indicesChanged) {
            const auto &positions = pool.getPositions();
            const auto &indices = pool.getTransformIndices();
            localStaging_.resize(static_cast<std::size_t>(liveCount));
            for (int i = 0; i < liveCount; ++i) {
                localStaging_[i].pos_ = positions[i].pos_;
                const std::uint32_t slot = indices[i];
                std::memcpy(&localStaging_[i].pad_, &slot, sizeof(float));
            }
            localPosBuf_->subData(
                0,
                static_cast<std::size_t>(liveCount) * sizeof(IRRender::VoxelGpuPosition),
                localStaging_.data()
            );
            pool.clearPendingTransformIndexRanges();
            lastSeededCanvas_ = touchedCanvas_;
        }

        // Per-frame transform upload — O(GPU-transformed sets), not O(voxels).
        if (maxSlotUsed_ >= 0) {
            transformBuf_->subData(
                0,
                static_cast<std::size_t>(maxSlotUsed_ + 1) * sizeof(GpuVoxelTransform),
                transforms_.data()
            );
        }
        GPUUpdateParams params{};
        params.voxelCount_ = liveCount;
        paramsBuf_->subData(0, sizeof(GPUUpdateParams), &params);

        program_->use();
        globalPosBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SingleVoxelPositions);
        localPosBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_LocalVoxelPositions);
        transformBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_EntityTransforms);
        paramsBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_UpdateParams);

        // One thread per live voxel; the 2D workgroup grid mirrors the visibility
        // compact dispatch exactly so the shader's linear-index reconstruction
        // (workGroupIndex * 64 + localId.x) lines up with what we dispatch. The
        // count→grid fold is shared with VOXEL_TO_TRIXEL_STAGE_1 via the
        // lightweight voxel_dispatch_grid.hpp helper (#1422) — no dependency on
        // the heavy STAGE_1 system header.
        constexpr int kLocalSize = 64;
        const ivec2 grid = voxelDispatchGridForCount(IRMath::divCeil(liveCount, kLocalSize));
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    static SystemId create() {
        const int maxSingleVoxels = IRRender::VoxelPoolConfig::getTotalSize();

        IRRender::createNamedResource<ShaderProgram>(
            "UpdateVoxelPositionsProgram",
            std::vector{ShaderStage{IRRender::kFileCompUpdateVoxelPositions, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "LocalVoxelPositionBuffer",
            nullptr,
            maxSingleVoxels * sizeof(IRRender::VoxelGpuPosition),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_LocalVoxelPositions
        );
        IRRender::createNamedResource<Buffer>(
            "EntityTransformBuffer",
            nullptr,
            kMaxGpuVoxelTransforms * sizeof(GpuVoxelTransform),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_EntityTransforms
        );
        IRRender::createNamedResource<Buffer>(
            "UpdateParamsBuffer",
            nullptr,
            sizeof(GPUUpdateParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_UpdateParams
        );

        SystemId systemId =
            registerSystem<UPDATE_VOXEL_POSITIONS_GPU, C_VoxelSetNew, C_WorldTransform>(
                "UpdateVoxelPositionsGpu"
            );
        auto *p = getSystemParams<System<UPDATE_VOXEL_POSITIONS_GPU>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("UpdateVoxelPositionsProgram");
        p->localPosBuf_ = IRRender::getNamedResource<Buffer>("LocalVoxelPositionBuffer");
        p->transformBuf_ = IRRender::getNamedResource<Buffer>("EntityTransformBuffer");
        p->paramsBuf_ = IRRender::getNamedResource<Buffer>("UpdateParamsBuffer");
        return systemId;
    }
};

} // namespace IRSystem

namespace IRPrefab::VoxelTransform {

// Handle to the UPDATE_VOXEL_POSITIONS_GPU transform-slot allocator (#1396). A
// consumer reaches the system's free-list through this rather than threading the
// SystemId through every call, so the slot API stays id-free. The id is resolved
// from SystemManager's `SystemName` registry (#2526): creating the system is all
// the wiring there is, and `IREntity::kNullEntity` means "never created" —
// callers treat that as "stay CPU-direct".
inline IRSystem::System<IRSystem::UPDATE_VOXEL_POSITIONS_GPU> *allocator() {
    const IRSystem::SystemId systemId = IRSystem::findSystem(IRSystem::UPDATE_VOXEL_POSITIONS_GPU);
    if (systemId == IREntity::kNullEntity) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>>(
        systemId
    );
}

// DEPRECATED — registration self-wires via SystemManager; remove once
// out-of-tree creations migrate. Kept as a no-op so an existing call site
// keeps compiling (engine API removal rule).
inline void setAllocatorSystem(IRSystem::SystemId) {}

// Acquire an EntityTransformBuffer slot for a GPU-transform-indirected set.
// Returns `IRRender::kVoxelTransformStatic` when the allocator was never wired
// or the budget is exhausted — callers treat that as "stay CPU-direct".
inline std::uint32_t acquireSlot() {
    auto *p = allocator();
    return p ? p->acquireTransformSlot() : IRRender::kVoxelTransformStatic;
}

inline void releaseSlot(std::uint32_t slot) {
    if (slot == IRRender::kVoxelTransformStatic) {
        return;
    }
    if (auto *p = allocator()) {
        p->releaseTransformSlot(slot);
    }
}

} // namespace IRPrefab::VoxelTransform

#endif /* SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H */
