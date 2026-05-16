#ifndef IRREDEN_RENDER_VOXEL_POOL_API_H
#define IRREDEN_RENDER_VOXEL_POOL_API_H

#include <irreden/render/voxel_pool_allocation.hpp>

#include <cstddef>
#include <string>

namespace IRRender {

// Allocate / release contiguous spans of voxel component arrays from
// the named canvas pool. Wraps RenderManager::allocateVoxels /
// deallocateVoxels; full pool-allocation contract (co-indexed spans,
// startIndex_ as source of truth, canvas-archetype-migration footgun)
// lives in `ir_render.hpp`.
//
// Lives in a dedicated header (out of ir_render.hpp) so component
// constructors that allocate voxels in their ctor — see "GPU / IO
// resource RAII" exception in .claude/rules/cpp-ecs.md — do not pull
// in the full render surface. Matches the T-205 split of
// `getActiveCanvasEntityOrNull` into `active_canvas.hpp`. See #754.
VoxelPoolAllocation allocateVoxels(unsigned int size, std::string canvasName = "main");

void deallocateVoxels(
    std::size_t startIndex, std::size_t size, std::string canvasName = "main"
);

} // namespace IRRender

#endif /* IRREDEN_RENDER_VOXEL_POOL_API_H */
