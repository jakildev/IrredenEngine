#include <irreden/render/voxel_pool_api.hpp>

#include <irreden/ir_render.hpp>
#include <irreden/render/render_manager.hpp>

#include <utility>

namespace IRRender {

VoxelPoolAllocation allocateVoxels(unsigned int size, std::string canvasName) {
    return getRenderManager().allocateVoxels(size, std::move(canvasName));
}

void deallocateVoxels(std::size_t startIndex, std::size_t size, std::string canvasName) {
    getRenderManager().deallocateVoxels(startIndex, size, std::move(canvasName));
}

} // namespace IRRender
