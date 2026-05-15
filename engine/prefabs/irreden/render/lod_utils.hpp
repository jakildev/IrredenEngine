#ifndef LOD_UTILS_H
#define LOD_UTILS_H

// PURPOSE: Zoom-to-LOD level mapping and per-level voxel scale factors for
//   level-of-detail rendering. Phase 1 wires this into LOD_UPDATE (writes
//   C_ActiveLodLevel) and SHAPES_TO_TRIXEL (filters C_ShapeDescriptor by
//   lodMin_). Design rationale in docs/design/lod-strategy.md.
//
// Tier index goes DOWN as detail goes UP: LOD_0 is the highest-detail tier
// (zoom-in close-up), LOD_4 is the coarsest silhouette tier (always
// rendered). A shape's lodMin_ field carries the smallest index at which it
// remains visible — i.e. the coarsest tier in which it still draws. The
// filter rule is "draw if activeLod <= lodMin_" (skip if lodMin_ <
// activeLod). LOD_4 is the default for new shapes so unmarked content keeps
// rendering at every zoom.

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

namespace IRRender {

inline constexpr std::uint32_t toUnderlying(LodLevel lodLevel) {
    return static_cast<std::uint32_t>(lodLevel);
}

// Maps the active camera zoom to the discrete LOD tier the renderer should
// use this frame. Thresholds chosen against the engine's actual zoom range
// (`kTrixelCanvasZoomMin..kTrixelCanvasZoomMax` = [1.0, 64.0], snapped to
// powers of two by render_manager.cpp): each tier doubles the previous
// zoom step so the per-tier ranges align with power-of-2 zoom snaps.
inline LodLevel computeLodLevel(float zoomLevel) {
    if (zoomLevel >= 16.0f)
        return LodLevel::LOD_0;
    if (zoomLevel >= 8.0f)
        return LodLevel::LOD_1;
    if (zoomLevel >= 4.0f)
        return LodLevel::LOD_2;
    if (zoomLevel >= 2.0f)
        return LodLevel::LOD_3;
    return LodLevel::LOD_4;
}

// Per-tier voxel scale factor for future content tiers — a single .vxs
// authored at LOD_0 can be downsampled to coarser tiers by this ratio.
// Not consumed by Phase 1 (the filter is binary per-shape), but kept here
// so Phase 2's prefab-manifest composition has a shared scale table.
inline float lodVoxelScale(LodLevel lodLevel) {
    switch (lodLevel) {
    case LodLevel::LOD_0:
        return 1.0f;
    case LodLevel::LOD_1:
        return 0.75f;
    case LodLevel::LOD_2:
        return 0.5f;
    case LodLevel::LOD_3:
        return 0.25f;
    default:
        return 0.125f;
    }
}

// True when a shape with lodMin_ == @p entityLodMin should be culled at
// the current @p activeLod. Strict inequality matches the Phase 1 spec —
// a shape with lodMin_ == LOD_4 is never culled because no activeLod
// satisfies activeLod > LOD_4.
inline bool shouldSkipAtLod(LodLevel entityLodMin, LodLevel activeLod) {
    return toUnderlying(entityLodMin) < toUnderlying(activeLod);
}

} // namespace IRRender

#endif /* LOD_UTILS_H */
