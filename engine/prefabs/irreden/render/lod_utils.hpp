#ifndef LOD_UTILS_H
#define LOD_UTILS_H

// PURPOSE: Zoom-to-LOD level mapping and per-level voxel scale factors for
//   level-of-detail rendering. Phase 1 wires this into LOD_UPDATE (writes
//   C_ActiveLodLevel) and SHAPES_TO_TRIXEL (filters C_ShapeDescriptor by
//   lodMin_). Design rationale in docs/design/lod-strategy.md.
//
// Tier index goes DOWN as detail goes UP: LOD_0 is the highest-detail tier
// (zoom-in close-up), LOD_4 is the coarsest silhouette tier (always
// rendered). A shape carries a LOD band [lodMax_ .. lodMin_]: lodMin_ is the
// coarsest tier (largest index) it still draws at, lodMax_ the finest tier
// (smallest index). The filter draws iff lodMax_ <= activeLod <= lodMin_.
// Defaults (lodMin_ = LOD_4, lodMax_ = LOD_0) span the whole range so unmarked
// content renders at every zoom; disjoint bands across co-located variants
// give exclusive (swap, not stack) LOD. See docs/design/lod-strategy.md.

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

// True when a shape occupying the inclusive LOD band [@p lodMax (finest tier,
// smallest index) .. @p lodMin (coarsest tier, largest index)] should be culled
// at the current @p activeLod. The shape draws only when activeLod lands inside
// the band; it is skipped when the camera is coarser than the band's low-detail
// floor (activeLod > lodMin) or finer than its high-detail ceiling
// (activeLod < lodMax — a finer variant has taken over).
//
// Defaults (lodMin = LOD_4, lodMax = LOD_0) span the whole tier range, so an
// unmarked shape is never culled (every activeLod satisfies LOD_0 <= activeLod
// <= LOD_4) — byte-identical to the pre-band single-sided filter. Authoring
// co-located variants with disjoint bands yields exclusive LOD: exactly one
// renders per zoom, so they swap rather than stack (the additive-overlap
// glitch from #1467). The coarsest variant keeps lodMin = LOD_4 to persist at
// min zoom; the finest keeps lodMax = LOD_0 to persist past its threshold.
inline bool shouldSkipAtLod(LodLevel lodMin, LodLevel lodMax, LodLevel activeLod) {
    const std::uint32_t active = toUnderlying(activeLod);
    return active > toUnderlying(lodMin) || active < toUnderlying(lodMax);
}

} // namespace IRRender

#endif /* LOD_UTILS_H */
