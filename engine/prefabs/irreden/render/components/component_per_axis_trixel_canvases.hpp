#ifndef COMPONENT_PER_AXIS_TRIXEL_CANVASES_H
#define COMPONENT_PER_AXIS_TRIXEL_CANVASES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/voxel_pool_config.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <array>
#include <utility>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

// The GPU resource set behind the smooth camera Z-yaw path: three per-axis
// trixel canvases (one per face axis: X / Y / Z), the screen-space resolve
// texture and the compaction and overflow scratch
// (docs/design/per-axis-trixel-canvas-rotation.md). Splitting the
// voxel→trixel raster into one canvas per visible face axis lets each canvas
// carry a single uniform deformation, so each tiles gap-free even as the camera
// yaw moves between cardinals; the framebuffer unifies the three by depth.
//
// C_PerAxisTrixelCanvases is the live set — every per-axis reader addresses
// these fields on the component directly — and carries a second, parked set
// that is resident but never bound.
struct PerAxisCanvasStore {
    static constexpr int kAxisCount = 3; // indexed by face axis: 0=X, 1=Y, 2=Z

    // One color / distance / entity-id texture set per face axis. Mirrors the
    // C_TriangleCanvasTextures texture layout so the existing trixel machinery
    // (image binds, atomicMin distance writes, clears) applies per axis unchanged.
    //
    // Per-axis AO + sun-shadow textures let the smooth-yaw
    // lighting passes (COMPUTE_VOXEL_AO / COMPUTE_SUN_SHADOW / LIGHTING_TO_TRIXEL)
    // can light each axis canvas at trixel resolution before the framebuffer
    // scatter composites it — same rotation-only lifecycle as the colour /
    // distance / id triple. Both are canvas-resolution RGBA8, matching the
    // single-canvas C_CanvasAOTexture / C_CanvasSunShadow formats. The world
    // light volume + sun-shadow depth map stay SHARED (sampled by reconstructed
    // world-pos), so there is no per-axis copy of those.
    struct AxisTextures {
        std::pair<ResourceId, Texture2D *> colors_{0, nullptr};
        std::pair<ResourceId, Texture2D *> distances_{0, nullptr};
        std::pair<ResourceId, Texture2D *> entityIds_{0, nullptr};
        std::pair<ResourceId, Texture2D *> ao_{0, nullptr};
        std::pair<ResourceId, Texture2D *> sunShadow_{0, nullptr};
    };

    ivec2 size_{0, 0}; // worst-case texel size shared by all axes; (0,0) while unallocated
    // The cardinal canvas size the set was allocated for; resolveDepth_ is this
    // size, and a parked set is reused only under the same one. The main
    // canvas does not resize today, so the guard is for the day it does.
    ivec2 mainSize_{0, 0};
    std::array<AxisTextures, kAxisCount> axes_{};

    // Screen-space (MAIN-canvas-sized) front-most iso-depth texture produced by
    // RESOLVE_PER_AXIS_SCREEN_DEPTH: the three face-local per-axis voxel
    // canvases scattered into one cardinal-layout distance texture so
    // BAKE_SUN_SHADOW_MAP casts per-axis sun shadows through its existing
    // cardinal recovery without cross-face self-occlusion. R32I, with the same
    // format and clear sentinel as the main
    // distance texture. Distinct from the per-axis `distances_` (face-local,
    // worst-case sized) — this is a single screen-space resolve.
    std::pair<ResourceId, Texture2D *> resolveDepth_{0, nullptr};

    // Per-axis empty-cell compaction buffers. One compute pre-pass
    // (PER_AXIS_CELL_COMPACT in VOXEL_TO_TRIXEL_STAGE_1) scans each axis distance
    // canvas and appends its OCCUPIED cells into `cellCompacted_` (three axis
    // regions, `cellRegionStride_` uints apart) while filling `cellIndirect_` with
    // both a per-axis draw-indirect command (scatter composite) and a per-axis
    // compute-indirect dispatch block (the AO / sun-shadow / lighting /
    // resolve stages dispatch one 1-D workgroup per `kPerAxisCellComputeTile`
    // occupied cells instead of sweeping the full worst-case grid). Owned here so
    // the lifecycle matches the axis textures exactly — sized in allocate(), freed
    // in release() — and every per-axis system reads them off this shared
    // component (no cross-system named-resource resize, which the RRM forbids).
    std::pair<ResourceId, Buffer *> cellCompacted_{0, nullptr};
    std::pair<ResourceId, Buffer *> cellIndirect_{0, nullptr};
    int cellRegionStride_ = 0; // uints per axis region (>= axis cells, 64-aligned)

    // Unified per-axis resolve scratch for winner election and the
    // view-visibility overflow lane, bound whole at
    // kBufferIndex_PerAxisResolveScratch during the per-axis dispatches only
    // (transient reuse — the resolve + BAKE consumers re-bind 28
    // themselves). Four 256-B-aligned regions, offsets in uints:
    // [0, viewMaskBaseUints_) — winner ids, one uint per texel.
    //     Region 0 on purpose: the stage-1/2 kernels' perAxisWinnerIds[cell]
    //     indexing is unchanged, and dispatchPerAxisCanvases' per-axis
    //     fillBuffer(bytes = texels*4) reset covers exactly this region.
    // [viewMaskBaseUints_, ctrlBaseUints_) — view mask: per yawed
    //     screen cell, the atomicMin of the biased quantized yawed depth
    //     (shared across the three axis routes — view visibility competes
    //     across axes). Reset to 0xFFFFFFFF once per rotating frame.
    // [ctrlBaseUints_, entriesBaseUints_) — ctrl block: indirect
    //     draw args {indexCount, instanceCount, firstIndex, baseVertex,
    //     baseInstance} + droppedCount, followed by GPU-authored sort commands.
    //     instanceCount/droppedCount are GPU atomics (resolveMode 3). The
    //     overflow indirect draw sources its args at ctrlBaseUints_*4 directly.
    // [entriesBaseUints_, +overflowCap_*3) — overflow entries, 3 uints
    //     each: {iso cell x|y (16+16), colorPacked, encoded distance} — the
    //     exact (cell, value) pair the cardinal store would have written, so
    //     the scatter's overflow branch reuses the cell path's recovery
    //     bit-for-bit.
    // A Buffer (not textures) because Metal has only one image-atomic scratch
    // slot (held by distances_). Sized/freed with the axis textures.
    std::pair<ResourceId, Buffer *> winnerIds_{0, nullptr};
    int viewMaskBaseUints_ = 0;
    int ctrlBaseUints_ = 0;
    int entriesBaseUints_ = 0;
    int overflowCap_ = 0;
    static constexpr std::uint32_t kOverflowSortBlockBits = 11;
    static constexpr std::uint32_t kOverflowSortMaxStageBits = 30;
    static constexpr std::uint32_t kOverflowSortArgsBaseUints = 8;
    static constexpr std::uint32_t kOverflowSortCommandUints = 4;
    static constexpr std::uint32_t kOverflowSortCommandCount =
        2 + kOverflowSortMaxStageBits - kOverflowSortBlockBits;
    static constexpr std::uint32_t kOverflowControlUints = 128;
    static_assert(
        kOverflowSortArgsBaseUints + kOverflowSortCommandCount * kOverflowSortCommandUints <=
            kOverflowControlUints,
        "overflow sort commands must fit the aligned control region"
    );
    // Completed-frame count used for warnings and as a merge-stage encoding hint.
    std::uint32_t laggedOverflowCount_ = 0;

    // Allocation state is the texture handles themselves — no separate bool to
    // drift out of sync (cf. the no-dirty-flags rule in .claude/rules/cpp-ecs.md).
    bool isAllocated() const {
        return axes_[0].colors_.second != nullptr;
    }

    // The set was sized for this cardinal canvas, so a rotation resuming under
    // it can bind the set as it is.
    bool fits(ivec2 size, ivec2 mainSize) const {
        return isAllocated() && size_ == size && mainSize_ == mainSize;
    }

    // VoxelPoolConfig::kMaxEdge is sized off this bound: the largest pool's
    // face demand fits the signed 2^30 field, one edge more does not.
    static_assert(
        static_cast<std::uint64_t>(IRRender::VoxelPoolConfig::kMaxEdge) *
                IRRender::VoxelPoolConfig::kMaxEdge * IRRender::VoxelPoolConfig::kMaxEdge *
                kAxisCount <=
            (std::uint64_t{1} << 30),
        "VoxelPoolConfig::kMaxEdge overflows the per-axis overflow lane"
    );
    static_assert(
        static_cast<std::uint64_t>(IRRender::VoxelPoolConfig::kMaxEdge + 1) *
                (IRRender::VoxelPoolConfig::kMaxEdge + 1) *
                (IRRender::VoxelPoolConfig::kMaxEdge + 1) * kAxisCount >
            (std::uint64_t{1} << 30),
        "VoxelPoolConfig::kMaxEdge is not the largest edge the overflow lane fits"
    );

    static int overflowCapacityFor(int axisCells, int voxelCapacity) {
        IR_ASSERT(axisCells >= 0 && voxelCapacity >= 0, "negative per-axis capacity input");
        const std::uint64_t faceDemand = static_cast<std::uint64_t>(voxelCapacity) * kAxisCount;
        const std::uint64_t canvasReserve =
            IRMath::max(static_cast<std::uint64_t>(axisCells) / 4u, std::uint64_t{65536});
        const std::uint64_t demand = IRMath::max(faceDemand, canvasReserve);
        IR_ASSERT(
            demand <= (std::uint64_t{1} << 30),
            "per-axis capacity exceeds signed shader field"
        );
        return static_cast<int>(IRMath::nextPowerOfTwo(static_cast<std::uint32_t>(demand)));
    }

    // Allocate the three axis texture sets at @p size (worst-case per-axis) plus
    // the screen-space resolve texture at @p mainSize. No-op if already
    // allocated. Called at rotation start by the lifecycle.
    void allocate(ivec2 size, ivec2 mainSize) {
        if (isAllocated()) {
            return;
        }
        size_ = size;
        mainSize_ = mainSize;
        // Reuse the canonical canvas-texture factories so the per-axis textures
        // stay format-identical to the single canvas (detail:: in
        // component_triangle_canvas_textures.hpp).
        for (AxisTextures &axis : axes_) {
            axis.colors_ = detail::makeCanvasColorTexture(size);
            axis.distances_ = detail::makeCanvasDistanceTexture(size);
            axis.entityIds_ = detail::makeCanvasEntityIdTexture(size);
            // AO + sun-shadow are RGBA8 like the single-canvas lighting
            // textures; the colour factory matches that format.
            axis.ao_ = detail::makeCanvasColorTexture(size);
            axis.sunShadow_ = detail::makeCanvasColorTexture(size);
        }
        resolveDepth_ = detail::makeCanvasDistanceTexture(mainSize);

        // Per-axis empty-cell compaction buffers, sized to this
        // allocation's axis extent. The compacted-cell region is rounded up to a
        // 64-uint (256 B) multiple so each axis's bindRange offset stays
        // SSBO-alignment-safe; the indirect buffer holds three
        // kPerAxisCellIndirectStrideBytes-spaced structs (draw args + compute
        // dispatch args). Same 25/26 bind indices the compaction + consumers
        // bindRange onto per axis.
        const int axisCells = size.x * size.y;
        cellRegionStride_ = IRMath::divCeil(axisCells, 64) * 64;
        cellCompacted_ = IRRender::createResource<Buffer>(
            nullptr,
            static_cast<size_t>(cellRegionStride_) * static_cast<size_t>(kAxisCount) *
                sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisCellCompacted
        );
        cellIndirect_ = IRRender::createResource<Buffer>(
            nullptr,
            static_cast<size_t>(kAxisCount) * static_cast<size_t>(kPerAxisCellIndirectStrideBytes),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisCellIndirect
        );
        // Clear to the empty sentinel so a creation that allocates per-axis
        // canvases but does NOT register RESOLVE_PER_AXIS_SCREEN_DEPTH still
        // reads "no per-axis caster" from BAKE rather than garbage — the
        // feature degrades to the no-cast behavior instead of corrupting
        // the shared sun map. When the resolve stage IS registered it
        // overwrites every texel each frame.
        static constexpr std::int32_t kDistanceClear =
            static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
        IRRender::device()->clearTexImage(resolveDepth_.second, 0, &kDistanceClear);
        // Unified resolve scratch for the winner region, view mask, control
        // block, and overflow entries. Regions
        // start on 256 B boundaries so bindRange windows stay
        // SSBO-alignment-safe; the whole buffer is bindBase'd at 28 during the
        // per-axis dispatches, with region offsets carried in
        // FrameDataVoxelToCanvas::overflowScratchLayout_.
        constexpr int kScratchAlignUints = 64; // 256 B / 4
        const int alignedCells = IRMath::divCeil(axisCells, kScratchAlignUints) * kScratchAlignUints;
        viewMaskBaseUints_ = alignedCells;
        ctrlBaseUints_ = viewMaskBaseUints_ + alignedCells;
        entriesBaseUints_ = ctrlBaseUints_ + static_cast<int>(kOverflowControlUints);
        // Mode 3 emits at most one record per voxel per axis: its canonical
        // trixel lane returns before dual-face emission, with only micro-slice
        // zero active. Capacity covers the whole main pool, including the first
        // frame after a camera jump or spawn. The sort, append clamp and layout
        // share this power-of-two count. This bound depends on the mode-3 lane
        // guard in both stage-1 shader bodies and unique axis compact lists.
        overflowCap_ = overflowCapacityFor(axisCells, IRRender::VoxelPoolConfig::getTotalSize());
        winnerIds_ = IRRender::createResource<Buffer>(
            nullptr,
            (static_cast<std::size_t>(entriesBaseUints_) +
             static_cast<std::size_t>(overflowCap_) * 3u) *
                sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
        // Seed the ctrl block so the first rotating frame's pre-reset counter
        // readback (the cap-overflow warn) reads zeros, not uninitialized data.
        const std::array<std::uint32_t, 8> ctrlSeed{};
        winnerIds_.second->subData(
            static_cast<std::ptrdiff_t>(ctrlBaseUints_) * sizeof(std::uint32_t),
            sizeof(ctrlSeed),
            ctrlSeed.data()
        );
    }

    // Release all three axis texture sets + the resolve texture and reset to the
    // unallocated state. No-op if not allocated.
    void release() {
        if (!isAllocated()) {
            return;
        }
        for (AxisTextures &axis : axes_) {
            IRRender::destroyResource<Texture2D>(axis.colors_.first);
            IRRender::destroyResource<Texture2D>(axis.distances_.first);
            IRRender::destroyResource<Texture2D>(axis.entityIds_.first);
            IRRender::destroyResource<Texture2D>(axis.ao_.first);
            IRRender::destroyResource<Texture2D>(axis.sunShadow_.first);
            axis = AxisTextures{};
        }
        IRRender::destroyResource<Texture2D>(resolveDepth_.first);
        resolveDepth_ = {0, nullptr};
        IRRender::destroyResource<Buffer>(cellCompacted_.first);
        cellCompacted_ = {0, nullptr};
        IRRender::destroyResource<Buffer>(cellIndirect_.first);
        cellIndirect_ = {0, nullptr};
        cellRegionStride_ = 0;
        IRRender::destroyResource<Buffer>(winnerIds_.first);
        winnerIds_ = {0, nullptr};
        viewMaskBaseUints_ = 0;
        ctrlBaseUints_ = 0;
        entriesBaseUints_ = 0;
        overflowCap_ = 0;
        laggedOverflowCount_ = 0;
        size_ = ivec2{0, 0};
        mainSize_ = ivec2{0, 0};
    }
};

// Unlike C_TriangleCanvasTextures (which allocates its GPU textures in its
// constructor), this component allocates LAZILY: the live set exists only
// while the camera sits at a non-cardinal residual yaw. On a cardinal frame the
// set is parked — swapped into `parked_`, resident but not live, so
// isAllocated() turns false and the renderer takes the single-canvas fast path
// (byte-identical) — and a rotation that resumes within
// IRPrefab::PerAxisCanvas::kParkedCardinalFrames swaps it back instead of
// re-allocating. A camera that settles on a cardinal frees the set once that
// window passes, so a static / cardinal scene pays zero extra GPU memory. The
// lifecycle is driven once per frame by
// IRPrefab::PerAxisCanvas::syncAllocationToCameraYaw().
//
// Stage 1 routes each visible voxel face into its axis canvas using continuous
// center repositioning and shared world depth.
//
// This is the GPU-resource-RAII component pattern (engine/prefabs/CLAUDE.md
// §"Documented exceptions") — the component owns the textures and frees them in
// onDestroy(); the only twist is that allocation is deferred to the lifecycle
// rather than the constructor (the gate needs the per-frame camera yaw).
struct C_PerAxisTrixelCanvases : PerAxisCanvasStore {
    // A set kept resident across a cardinal. Never bound: every per-axis
    // reader gates on isAllocated(), which reads the live set only.
    PerAxisCanvasStore parked_;
    // Consecutive cardinal frames the parked set has waited.
    int parkedFrames_ = 0;

    bool hasParked() const {
        return parked_.isAllocated();
    }

    // Move the live set aside so the cardinal frame renders single-canvas with
    // the resources still resident.
    void park() {
        IR_ASSERT(isAllocated() && !hasParked(), "park needs a live set and no parked one");
        std::swap(static_cast<PerAxisCanvasStore &>(*this), parked_);
        parkedFrames_ = 0;
    }

    // Make the parked set live again. The caller has checked it fits the canvas.
    void unpark() {
        IR_ASSERT(!isAllocated() && hasParked(), "unpark needs a parked set and no live one");
        std::swap(static_cast<PerAxisCanvasStore &>(*this), parked_);
        parkedFrames_ = 0;
    }

    // Free the parked set with its wait, whether the window closed or the
    // canvas changed size under it.
    void releaseParked() {
        parked_.release();
        parkedFrames_ = 0;
    }

    void onDestroy() {
        release();
        releaseParked();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_TRIXEL_CANVASES_H */
