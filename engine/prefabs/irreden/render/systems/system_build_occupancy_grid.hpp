#ifndef SYSTEM_BUILD_OCCUPANCY_GRID_H
#define SYSTEM_BUILD_OCCUPANCY_GRID_H

// Rebuilds the CPU-side 3D occupancy bitfield and uploads it to the SSBO
// at `kBufferIndex_OccupancyGrid` for downstream lighting compute shaders
// (COMPUTE_VOXEL_AO, COMPUTE_LIGHT_VOLUME).
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so lighting compute passes see the current frame's voxel state.
//
// Phased-out producer: this system + the OccupancyGrid SSBO it feeds will
// be deleted in T-09Y once AO migrates to screen-space neighbour sampling
// (T-09X) and light-volume LOS moves to the GPU (T-072). The sun-shadow
// path no longer reads it — the screen-space sun-depth bake replaces the
// 3D occupancy march.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
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
    struct Params {
        Buffer *ssbo_ = nullptr;
    };

    static SystemId create() {
        IRRender::createNamedResource<Buffer>(
            "OccupancyGridBuffer",
            nullptr,
            kOccupancySSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyGrid
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->ssbo_ = IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");

        SystemId systemId = createSystem<C_VoxelPool, C_OccupancyGrid>(
            "BuildOccupancyGrid",
            [p](IREntity::EntityId, C_VoxelPool &pool, C_OccupancyGrid &grid) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
                IR_ASSERT(
                    grid.sizeVoxels() == kMaxOccupancyGridSideVoxels,
                    "Lighting occupancy shaders currently require a 256^3 grid"
                );

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Clear", IR_PROFILER_COLOR_RENDER);
                    std::fill(grid.bitfield().begin(), grid.bitfield().end(), 0u);
                }

                const auto &globals = pool.getPositionGlobals();
                const auto &colors = pool.getColors();
                const int liveCount = pool.getLiveVoxelCount();

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Populate", IR_PROFILER_COLOR_RENDER);
                    for (int i = 0; i < liveCount; ++i) {
                        if (colors[i].color_.alpha_ == 0)
                            continue;
                        const vec3 &wp = globals[i].pos_;
                        // Round-half-up — must match the `roundHalfUp(...)`
                        // helper in shaders/ir_iso_common.glsl + .metal. See
                        // IRMath::roundHalfUp doc-comment for why this rule
                        // (vs std::lround / glm::round) is required for the
                        // CPU↔GPU occupancy-grid handshake.
                        const ivec3 cell = IRMath::roundVec3HalfUp(wp);
                        if (!grid.inBounds(cell.x, cell.y, cell.z))
                            continue;
                        grid.setBit(cell.x, cell.y, cell.z);
                    }
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Upload", IR_PROFILER_COLOR_RENDER);
                    p->ssbo_->subData(0, grid.bitfieldByteSize(), grid.bitfield().data());
                }
            },
            [p]() {
                p->ssbo_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid);
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_OCCUPANCY_GRID_H */
