#ifndef SYSTEM_BUILD_OCCUPANCY_GRID_H
#define SYSTEM_BUILD_OCCUPANCY_GRID_H

// Rebuilds the CPU-side 3D occupancy bitfield and uploads it to the SSBO
// at `kBufferIndex_OccupancyGrid` for downstream lighting compute shaders.
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so lighting compute passes see the current frame's voxel state.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <cmath>
#include <cstddef>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

/// Upper-bound grid edge length the SSBO is sized for — 256³ = 2 MB.
/// Per-instance `C_OccupancyGrid` components may be smaller, but the
/// SSBO is fixed so swapping between grid extents doesn't reallocate.
constexpr int kMaxOccupancyGridSideVoxels = 256;

constexpr std::size_t kOccupancySSBOByteSize =
    (static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels) *
     static_cast<std::size_t>(kMaxOccupancyGridSideVoxels)) /
    8u;

template <> struct System<BUILD_OCCUPANCY_GRID> {
    static SystemId create() {
        IRRender::createNamedResource<Buffer>(
            "OccupancyGridBuffer",
            nullptr,
            kOccupancySSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyGrid
        );

        static Buffer *s_ssbo =
            IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");

        return createSystem<C_VoxelPool, C_OccupancyGrid>(
            "BuildOccupancyGrid",
            [](C_VoxelPool &pool, C_OccupancyGrid &grid) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

                if (!grid.isAnyDirty()) return;

                {
                    IR_PROFILE_BLOCK(
                        "BuildOccupancyGrid::ClearDirty",
                        IR_PROFILER_COLOR_RENDER
                    );
                    grid.clearBitsInDirtyChunks();
                }

                const auto &globals = pool.getPositionGlobals();
                const auto &colors = pool.getColors();
                const int liveCount = pool.getLiveVoxelCount();

                {
                    IR_PROFILE_BLOCK(
                        "BuildOccupancyGrid::Populate",
                        IR_PROFILER_COLOR_RENDER
                    );
                    for (int i = 0; i < liveCount; ++i) {
                        if (colors[i].color_.alpha_ == 0) continue;
                        const vec3 &wp = globals[i].pos_;
                        const int wx = static_cast<int>(std::lround(wp.x));
                        const int wy = static_cast<int>(std::lround(wp.y));
                        const int wz = static_cast<int>(std::lround(wp.z));
                        grid.setBit(wx, wy, wz);
                    }
                }

                {
                    IR_PROFILE_BLOCK(
                        "BuildOccupancyGrid::Upload",
                        IR_PROFILER_COLOR_RENDER
                    );
                    s_ssbo->subData(
                        0,
                        grid.bitfieldByteSize(),
                        grid.bitfield().data()
                    );
                }

                grid.clearAllDirty();
            },
            []() {
                s_ssbo->bindBase(
                    BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_OCCUPANCY_GRID_H */
