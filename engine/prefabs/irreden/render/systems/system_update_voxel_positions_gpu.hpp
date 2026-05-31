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
#include <irreden/render/camera.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_world_transform.hpp>

#include <array>
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

    // EntityTransformBuffer slot allocator (#1299, PR-A). MAIN_CANVAS_SO3
    // entities acquire a slot at `setMode` time (via `IRPrefab::VoxelTransform`)
    // and release it when leaving the mode. A monotonic high-water plus a
    // recycle stack keeps slot ids dense and bounded by kMaxGpuVoxelTransforms.
    // Acquire/release are rare (mode changes), not per-frame.
    std::uint32_t nextFreeSlot_ = 0;
    std::vector<std::uint32_t> recycledSlots_;

    // Camera residual yaw, snapshotted once per frame in beginTick (#1300,
    // PR-B). The per-entity SO(3) face-deform bake composes it with each
    // entity's octahedral-snap residual so a MAIN_CANVAS_SO3 entity rasterized
    // directly onto the shared canvas deforms its faces by BOTH its own
    // rotation and the camera residual. The camera is moved in the INPUT
    // pipeline, so this value is stable through the RENDER stages that read it.
    float cameraResidualYaw_ = 0.0f;

    std::uint32_t acquireTransformSlot() {
        if (!recycledSlots_.empty()) {
            const std::uint32_t slot = recycledSlots_.back();
            recycledSlots_.pop_back();
            return slot;
        }
        if (nextFreeSlot_ < static_cast<std::uint32_t>(kMaxGpuVoxelTransforms)) {
            return nextFreeSlot_++;
        }
        return kVoxelTransformStatic; // budget exhausted — caller stays CPU-direct
    }

    void releaseTransformSlot(std::uint32_t slot) {
        if (slot < static_cast<std::uint32_t>(kMaxGpuVoxelTransforms)) {
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
        // Snapshot the camera residual yaw once per frame for the per-entity
        // SO(3) face-deform bake (#1300, PR-B). residualYaw is 0 at every
        // cardinal — which is exactly when the main canvas runs the
        // single-canvas voxel raster that consumes faceDeform (the per-axis
        // canvas path owns the non-cardinal residual window) — so the bake
        // reduces to the entity residual there, byte-matching the detached path.
        cameraResidualYaw_ = IRPrefab::Camera::getYawSplit().second;
    }

    void tick(C_VoxelSetNew &voxelSet, const C_WorldTransform &worldTransform) {
        if (voxelSet.gpuTransformSlot_ == kVoxelTransformStatic || voxelSet.numVoxels_ == 0) {
            return;
        }
        const std::uint32_t slot = voxelSet.gpuTransformSlot_;
        if (slot >= static_cast<std::uint32_t>(kMaxGpuVoxelTransforms)) {
            return; // out of transform-slot budget — set stays CPU-direct
        }
        // Per-entity main-canvas SO(3) (#1299, PR-A): octahedral-snap the
        // orientation to one of the 24 cube orientations ONCE here, and drive
        // BOTH the prepass matrix and the per-voxel visible triplet from it so
        // the rotated geometry and the faces it shows always agree (the
        // architect's "one snap site"). Non-snap sets (e.g. the continuous
        // GPU-transform smoke) keep the raw rotation — byte-identical prepass.
        const IRMath::vec4 rotation = voxelSet.snapTransformOctahedral_
                                          ? IRMath::octahedralSnap(worldTransform.rotation_)
                                          : worldTransform.rotation_;
        // CPU mirror of the GLSL/Metal `transform * localPos`: sqtToMat4 is
        // bit-identical to the shader-side helper, so the same operands classify
        // the same on both sides (the CPU↔GPU consistency the plan flags).
        transforms_[slot].modelToWorld_ = IRMath::sqtToMat4(
            worldTransform.scale_,
            rotation,
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

        // Stamp the per-voxel visible triplet from the SAME snapped orientation
        // onto every owned voxel's `C_Voxel::reserved_`, so the raster stages
        // render the entity's own faces (binding 6, re-uploaded in full by
        // VOXEL_TO_TRIXEL_STAGE_1 right after this prepass). One stamp value for
        // the whole set — every voxel shares the snapped orientation.
        if (voxelSet.snapTransformOctahedral_) {
            const std::array<IRMath::FaceId, 3> triplet = IRMath::visibleTriplet(rotation);
            const std::uint32_t packed =
                IRComponents::packVoxelVisibleTriplet(triplet[0], triplet[1], triplet[2]);
            lastTickPool_->setVoxelReservedForRange(
                voxelSet.voxelStartIdx_, static_cast<std::size_t>(voxelSet.numVoxels_), packed
            );

            // Per-entity residual face deformation (#1300, PR-B). The octahedral
            // snap above quantizes the orientation; the residual is the leftover
            // continuous rotation that turns PR-A's per-orientation *stepping*
            // into smooth SO(3). Compose it with the camera residual yaw — exact
            // because faceDeformationMatrix(face, yaw) == faceDeformationMatrixSO3(
            // face, qZ(-yaw)), so the two residuals fuse into one quaternion
            // instead of two linearized 2x2 products. The raster emits at the
            // snapped orientation (positions via modelToWorld_, faces via the
            // triplet) and applies this 2x2 per axis at emit time. Axis-only
            // (X_NEG/X_POS share the X matrix), matching the detached per-canvas
            // bake in system_voxel_to_trixel.hpp.
            //
            // Hand it to the pool (binding-7 per-canvas UBO path, #1300 Q2),
            // NOT the per-entity SSBO (binding 18): the raster reading a
            // prepass-owned `BUFFER_STORAGE_DYNAMIC` SSBO every frame tripped a
            // Metal cross-stage orphan hazard (architect direction; the SSBO
            // "hordes" path is the deferred follow-up). VOXEL_TO_TRIXEL_STAGE_1
            // copies this into `FrameDataVoxelToCanvas::faceDeformSO3_` for the
            // single SO(3) entity on this canvas; the surrounding world voxels
            // keep the shared camera-residual `faceDeform_`.
            const IRMath::vec4 residual = IRMath::octahedralSnapResidual(worldTransform.rotation_);
            const IRMath::vec4 cameraResidual =
                IRMath::quatAxisAngle(IRMath::vec3(0.0f, 0.0f, 1.0f), -cameraResidualYaw_);
            const IRMath::vec4 combined = IRMath::quatMul(cameraResidual, residual);
            const IRMath::mat2 fdX = IRMath::faceDeformationMatrixSO3(IRMath::kXFace, combined);
            const IRMath::mat2 fdY = IRMath::faceDeformationMatrixSO3(IRMath::kYFace, combined);
            const IRMath::mat2 fdZ = IRMath::faceDeformationMatrixSO3(IRMath::kZFace, combined);
            lastTickPool_->setSO3FaceDeform(
                IRMath::vec4(fdX[0], fdX[1]),
                IRMath::vec4(fdY[0], fdY[1]),
                IRMath::vec4(fdZ[0], fdZ[1])
            );
        }
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
        // for the helper. Extraction tracked in #1422.
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

namespace IRPrefab::VoxelTransform {

// Wire-once-at-init handle to the UPDATE_VOXEL_POSITIONS_GPU transform-slot
// allocator (#1299, PR-A). The generic `IRPrefab::RotationMode::setMode` reaches
// the system's free-list through this rather than threading the SystemId through
// every call — the same shape as `IRPrefab::Chunk::setMembershipMigrationManager`
// (which threads the id per call; here the id is config so a generic setMode can
// stay id-free). Init-config: a creation that uses MAIN_CANVAS_SO3 entities calls
// `setAllocatorSystem(systemId)` once, right after
// `System<UPDATE_VOXEL_POSITIONS_GPU>::create()`. `IREntity::kNullEntity` is the
// "never wired" sentinel (system ids count up from 0, so they never collide).
inline IRSystem::SystemId g_allocatorSystem = IREntity::kNullEntity;

inline void setAllocatorSystem(IRSystem::SystemId systemId) {
    g_allocatorSystem = systemId;
}

inline IRSystem::System<IRSystem::UPDATE_VOXEL_POSITIONS_GPU> *allocator() {
    if (g_allocatorSystem == IREntity::kNullEntity) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<IRSystem::UPDATE_VOXEL_POSITIONS_GPU>>(
        g_allocatorSystem
    );
}

// Acquire an EntityTransformBuffer slot for a MAIN_CANVAS_SO3 set. Returns
// `IRRender::kVoxelTransformStatic` when the allocator was never wired or the
// budget is exhausted — callers treat that as "stay GRID / CPU-direct".
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
