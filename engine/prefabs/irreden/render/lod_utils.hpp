#ifndef LOD_UTILS_H
#define LOD_UTILS_H

// PURPOSE: Zoom-to-LOD level mapping, per-level voxel scale factors, and
//   skip predicates for level-of-detail rendering.
// STATUS: WIP stub -- not yet included or referenced by any system.
//
// ARTIST-DRIVEN LOD VISION:
//   Artists will model entities at multiple explicit detail levels (e.g. a
//   flower at close-up, medium, and far zoom). Each LOD is an authored voxel
//   representation, not an automatic downscale. The engine will need:
//     - Per-entity LOD storage: multiple C_ShapeDescriptor or voxel sets
//       per entity, tagged by LOD level.
//     - LOD selection system: picks the appropriate LOD based on camera zoom
//       and entity distance, using computeLodLevel() or a similar function.
//     - LOD interpolation: smooth transitions between levels to avoid
//       popping. This is a complex problem for voxels -- possible approaches
//       include voxel morphing, temporal dithering, per-face crossfade, or
//       alpha blending of overlapping representations. Requires research.
//     - Editor integration: tools for artists to preview and author each
//       LOD level, likely in the voxel_set_maker editor.
//
// TODO:
//   - Include this header from the shape/voxel render systems.
//   - Add LOD level field to C_ShapeDescriptor (already has lodLevel_).
//   - Create a system that selects active LOD per entity per frame.
//   - Design and implement LOD interpolation strategy.
//   - Hook into the voxel_set_maker editor for authoring LOD tiers.
// DEPENDENCIES: IRMath, IRRender (getSubdivisionMode, zoom level).

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

using namespace IRMath;

namespace IRRender {

inline constexpr std::uint32_t toUnderlying(LodLevel lodLevel) {
    return static_cast<std::uint32_t>(lodLevel);
}

inline LodLevel computeLodLevel(float zoomLevel) {
    if (zoomLevel >= 4.0f) return LodLevel::LOD_0;
    if (zoomLevel >= 2.0f) return LodLevel::LOD_1;
    if (zoomLevel >= 1.0f) return LodLevel::LOD_2;
    if (zoomLevel >= 0.5f) return LodLevel::LOD_3;
    return LodLevel::LOD_4;
}

inline float lodVoxelScale(LodLevel lodLevel) {
    switch (lodLevel) {
        case LodLevel::LOD_0: return 1.0f;
        case LodLevel::LOD_1: return 0.75f;
        case LodLevel::LOD_2: return 0.5f;
        case LodLevel::LOD_3: return 0.25f;
        default: return 0.125f;
    }
}

inline bool shouldSkipAtLod(LodLevel entityLodMin, LodLevel currentLod) {
    return toUnderlying(currentLod) > toUnderlying(entityLodMin) + 2;
}

} // namespace IRRender

#endif /* LOD_UTILS_H */
