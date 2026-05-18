#include <irreden/render/voxel_pool_config.hpp>

#include <irreden/ir_profile.hpp>

#include <cstdlib>
#include <cstring>

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

void parseArgv(int argc, char **argv) {
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--voxel-pool-size") == 0) {
            const int parsed = std::atoi(argv[i + 1]);
            if (parsed > 0) {
                setSize(parsed);
                IRE_LOG_INFO("voxel-pool-size CLI override: edge={}", parsed);
            } else {
                IRE_LOG_WARN(
                    "--voxel-pool-size requires a positive integer; got '{}'", argv[i + 1]
                );
            }
            ++i;
        }
    }
}

} // namespace IRRender::VoxelPoolConfig
