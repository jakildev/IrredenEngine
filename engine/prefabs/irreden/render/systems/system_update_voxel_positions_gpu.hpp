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
//   bone transform for skeletal voxels (#605) tomorrow. Per-entity SO(3) on the
//   main canvas (#1272 / #1299) routes voxel sets through this prepass instead
//   of re-deriving positions on the CPU.
//
// LIMITATIONS (foundation scope; tightened by the #1299 migration):
//   - Single dynamic pool per frame: if GPU-transformed sets span multiple
//     canvases, only the last-ticked canvas's pool is dispatched (the binding
//     5 / 17 / 18 SSBOs are single-instance, shared — same constraint
//     `VOXEL_TO_TRIXEL_STAGE_1` documents for its multi-canvas position upload).
//   - The CPU global mirror (`m_voxelPositionsGlobal`) for a dynamic set holds
//     the un-rotated translation-only position (UPDATE_VOXEL_SET_CHILDREN still
//     computes it as a re-seed fallback but does not queue it). Cull/picking of
//     a dynamic set read that mirror, so they lag the GPU rotation until #1299
//     folds the prepass output back into the cull frame.
//
// DEPENDENCIES: C_VoxelPool, C_VoxelSetNew, C_WorldTransform,
//   IRRender::VoxelGpuPosition, GpuVoxelTransform, GPUUpdateParams
//   (ir_render_types.hpp), shaders/c_update_voxel_positions.{glsl,metal}.

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/voxel_pool_allocation.hpp>
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
    // Cap on concurrently GPU-transformed voxel sets (= EntityTransformBuffer
    // slots). A set whose `gpuTransformSlot_` exceeds this stays on the CPU path.
    static constexpr int kMaxGpuVoxelTransforms = 4096;

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
        if (slot >= static_cast<std::uint32_t>(kMaxGpuVoxelTransforms)) {
            return; // out of transform-slot budget — set stays CPU-direct
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
        // (workGroupIndex * 64 + localId.x) lines up with what we dispatch. This
        // is the same fold as `voxelDispatchGridForCount` in system_voxel_to_trixel.hpp;
        // it is inlined here to avoid depending on that heavy STAGE_1 header just
        // for the helper (a shared home for it is a worthwhile follow-up cleanup).
        constexpr int kLocalSize = 64;
        constexpr int kMaxGroupsX = 1024;
        const int groups = IRMath::divCeil(liveCount, kLocalSize);
        const int groupsX = IRMath::min(groups, kMaxGroupsX);
        const int groupsY = IRMath::divCeil(groups, groupsX);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
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

#endif /* SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H */
