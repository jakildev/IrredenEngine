#include <irreden/render/voxel_pool_config.hpp>

namespace IRRender::VoxelPoolConfig {

namespace {
int g_edge = kDefaultEdge;
} // namespace

void setSize(int edge) {
    g_edge = IRMath::max(1, edge);
}

int getEdge() {
    return g_edge;
}

IRMath::ivec3 getSize() {
    return IRMath::ivec3{g_edge, g_edge, g_edge};
}

IRMath::ivec3 getMaxAllocationSize() {
    return getSize();
}

int getTotalSize() {
    return g_edge * g_edge * g_edge;
}

int getMaxAllocationSizeTotal() {
    return getTotalSize();
}

} // namespace IRRender::VoxelPoolConfig
