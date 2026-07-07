#ifndef COMPONENT_PER_AXIS_TRIXEL_CANVASES_H
#define COMPONENT_PER_AXIS_TRIXEL_CANVASES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <array>
#include <utility>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

// Three per-axis trixel canvases (one per face axis: X / Y / Z) that back the
// smooth camera Z-yaw path
// (#1308; docs/design/per-axis-trixel-canvas-rotation.md). Splitting the
// voxel→trixel raster into one canvas per visible face axis lets each canvas
// carry a single uniform deformation, so each tiles gap-free even as the camera
// yaw moves between cardinals; the three are unified by depth at the framebuffer
// (T3 / #1310).
//
// Lifecycle (T1): unlike C_TriangleCanvasTextures (which allocates its GPU
// textures in its constructor), this component allocates LAZILY — only while the
// camera sits at a non-cardinal residual yaw. At residualYaw == 0 the textures
// are released and the renderer falls back to the single main canvas (the
// byte-identical fast path), so a static / cardinal scene pays zero extra GPU
// memory. Allocation is driven once per frame by
// IRPrefab::PerAxisCanvas::syncAllocationToCameraYaw().
//
// T2 (#1309) routes each visible voxel face into its axis canvas (Stage-1
// routing + continuous center reposition + shared world depth). The
// framebuffer still reads only the single main canvas, so populating these
// textures does not change the rendered output until T3 (#1310) composites
// the three by depth.
//
// This is the GPU-resource-RAII component pattern (engine/prefabs/CLAUDE.md
// §"Documented exceptions") — the component owns the textures and frees them in
// onDestroy(); the only twist is that allocation is deferred to the lifecycle
// rather than the constructor (the gate needs the per-frame camera yaw).
struct C_PerAxisTrixelCanvases {
    static constexpr int kAxisCount = 3; // indexed by face axis: 0=X, 1=Y, 2=Z

    // One color / distance / entity-id texture set per face axis. Mirrors the
    // C_TriangleCanvasTextures texture layout so the existing trixel machinery
    // (image binds, atomicMin distance writes, clears) applies per axis unchanged.
    //
    // T4 (#1311) adds a per-axis AO + sun-shadow texture so the smooth-yaw
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
    std::array<AxisTextures, kAxisCount> axes_{};

    // Screen-space (MAIN-canvas-sized) front-most iso-depth texture produced by
    // RESOLVE_PER_AXIS_SCREEN_DEPTH (#1435): the three face-local per-axis voxel
    // canvases scattered into one cardinal-layout distance texture so
    // BAKE_SUN_SHADOW_MAP casts per-axis sun shadows through its existing
    // cardinal recovery, without the cross-face self-occlusion that retired the
    // face-local bake in #1380. R32I, same format/clear sentinel as the main
    // distance texture. Distinct from the per-axis `distances_` (face-local,
    // worst-case sized) — this is a single screen-space resolve.
    std::pair<ResourceId, Texture2D *> resolveDepth_{0, nullptr};

    // Per-axis empty-cell compaction buffers (#1961 / #2256). One compute pre-pass
    // (PER_AXIS_CELL_COMPACT in VOXEL_TO_TRIXEL_STAGE_1) scans each axis distance
    // canvas and appends its OCCUPIED cells into `cellCompacted_` (three axis
    // regions, `cellRegionStride_` uints apart) while filling `cellIndirect_` with
    // both a per-axis draw-indirect command (scatter composite) and a per-axis
    // compute-indirect dispatch block (#2256: the AO / sun-shadow / lighting /
    // resolve stages dispatch one 1-D workgroup per `kPerAxisCellComputeTile`
    // occupied cells instead of sweeping the full worst-case grid). Owned here so
    // the lifecycle matches the axis textures exactly — sized in allocate(), freed
    // in release() — and every per-axis system reads them off this shared
    // component (no cross-system named-resource resize, which the RRM forbids).
    std::pair<ResourceId, Buffer *> cellCompacted_{0, nullptr};
    std::pair<ResourceId, Buffer *> cellIndirect_{0, nullptr};
    int cellRegionStride_ = 0; // uints per axis region (>= axis cells, 64-aligned)

    // Per-cell deterministic-winner scratch for the per-axis store (#2255):
    // one uint per per-axis canvas texel (shared across the three axes — each
    // axis's store/resolve/tap sequence completes before the next begins),
    // holding the minimum run-stable voxel pool index among the faces that tie
    // the settled per-cell distance key. Stage 2's per-axis color tap admits
    // only that index, so the color/entity-id planes are byte-identical
    // run-to-run at a fixed pose. A Buffer (not a fourth texture) because
    // Metal has only one image-atomic scratch slot (held by distances_) — the
    // same rationale as the #1435 resolve scratch, whose binding
    // (kBufferIndex_PerAxisResolveScratch) this transiently reuses during the
    // per-axis dispatches only. Reset to 0xFFFFFFFF ("no winner") per axis by
    // dispatchPerAxisCanvases.
    std::pair<ResourceId, Buffer *> winnerIds_{0, nullptr};

    // Allocation state is the texture handles themselves — no separate bool to
    // drift out of sync (cf. the no-dirty-flags rule in .claude/rules/cpp-ecs.md).
    bool isAllocated() const {
        return axes_[0].colors_.second != nullptr;
    }

    // Allocate the three axis texture sets at @p size (worst-case per-axis) plus
    // the screen-space resolve texture at @p mainSize. No-op if already
    // allocated. Called at rotation start by the lifecycle.
    void allocate(ivec2 size, ivec2 mainSize) {
        if (isAllocated()) {
            return;
        }
        size_ = size;
        // Reuse the canonical canvas-texture factories so the per-axis textures
        // stay format-identical to the single canvas (detail:: in
        // component_triangle_canvas_textures.hpp).
        for (AxisTextures &axis : axes_) {
            axis.colors_ = detail::makeCanvasColorTexture(size);
            axis.distances_ = detail::makeCanvasDistanceTexture(size);
            axis.entityIds_ = detail::makeCanvasEntityIdTexture(size);
            // AO + sun-shadow are RGBA8 like the single-canvas lighting
            // textures (#1311); the colour factory matches that format.
            axis.ao_ = detail::makeCanvasColorTexture(size);
            axis.sunShadow_ = detail::makeCanvasColorTexture(size);
        }
        resolveDepth_ = detail::makeCanvasDistanceTexture(mainSize);

        // Per-axis empty-cell compaction buffers (#1961 / #2256), sized to this
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
        // feature degrades to the #1380 no-cast behavior instead of corrupting
        // the shared sun map. When the resolve stage IS registered it
        // overwrites every texel each frame.
        static constexpr std::int32_t kDistanceClear =
            static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
        IRRender::device()->clearTexImage(resolveDepth_.second, 0, &kDistanceClear);
        winnerIds_ = IRRender::createResource<Buffer>(
            nullptr,
            static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y) *
                sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
    }

    // Release all three axis texture sets + the resolve texture and reset to the
    // unallocated state. No-op if not allocated. Called at rotation stop by the
    // lifecycle.
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
        size_ = ivec2{0, 0};
    }

    void onDestroy() {
        release();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_TRIXEL_CANVASES_H */
