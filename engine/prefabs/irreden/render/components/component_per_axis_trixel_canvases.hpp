#ifndef COMPONENT_PER_AXIS_TRIXEL_CANVASES_H
#define COMPONENT_PER_AXIS_TRIXEL_CANVASES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>
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
// T1 stands up the storage + allocation lifecycle only; no voxel faces route
// here yet (Stage-1 routing is T2 / #1309), so allocating or releasing these
// textures never changes the rendered output.
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
    struct AxisTextures {
        std::pair<ResourceId, Texture2D *> colors_{0, nullptr};
        std::pair<ResourceId, Texture2D *> distances_{0, nullptr};
        std::pair<ResourceId, Texture2D *> entityIds_{0, nullptr};
    };

    ivec2 size_{0, 0}; // worst-case texel size shared by all axes; (0,0) while unallocated
    std::array<AxisTextures, kAxisCount> axes_{};

    // Allocation state is the texture handles themselves — no separate bool to
    // drift out of sync (cf. the no-dirty-flags rule in .claude/rules/cpp-ecs.md).
    bool isAllocated() const {
        return axes_[0].colors_.second != nullptr;
    }

    // Allocate all three axis texture sets at @p size. No-op if already
    // allocated. Called at rotation start by the lifecycle.
    void allocate(ivec2 size) {
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
        }
    }

    // Release all three axis texture sets and reset to the unallocated state.
    // No-op if not allocated. Called at rotation stop by the lifecycle.
    void release() {
        if (!isAllocated()) {
            return;
        }
        for (AxisTextures &axis : axes_) {
            IRRender::destroyResource<Texture2D>(axis.colors_.first);
            IRRender::destroyResource<Texture2D>(axis.distances_.first);
            IRRender::destroyResource<Texture2D>(axis.entityIds_.first);
            axis = AxisTextures{};
        }
        size_ = ivec2{0, 0};
    }

    void onDestroy() {
        release();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_PER_AXIS_TRIXEL_CANVASES_H */
