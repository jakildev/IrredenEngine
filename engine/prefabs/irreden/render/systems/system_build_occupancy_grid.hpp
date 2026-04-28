#ifndef SYSTEM_BUILD_OCCUPANCY_GRID_H
#define SYSTEM_BUILD_OCCUPANCY_GRID_H

// Rebuilds the CPU-side 3D occupancy bitfield and uploads it to the SSBO
// at `kBufferIndex_OccupancyGrid` for downstream lighting compute shaders.
// Also rebuilds a small per-entity bbox table (one entry per voxel-pool
// entity) and uploads it to `kBufferIndex_OccupancyEntityBounds` so the
// sun-shadow compute can skip self-cells during the occupancy march
// (parity with the analytic path's selfEntityId exclusion).
//
// Must run at the **start of RENDER**, before `VOXEL_TO_TRIXEL_STAGE_1`,
// so lighting compute passes see the current frame's voxel state.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_occupancy_grid.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

#include <algorithm>
#include <cstddef>
#include <unordered_map>
#include <vector>

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

/// Upper bound on simultaneously-rendered voxel-pool entities the shadow
/// shader will linear-scan for self-bbox lookup. Sized for typical demo
/// scenes; bump if a creation needs more and accept the per-pixel cost.
constexpr int kMaxOccupancyEntityBounds = 256;
constexpr std::size_t kOccupancyBoundsSSBOByteSize =
    static_cast<std::size_t>(kMaxOccupancyEntityBounds) * sizeof(GPUOccupancyEntityBounds);

namespace detail {

/// How many entries `system_build_occupancy_grid` uploaded to the bounds
/// SSBO this frame. Read by `system_compute_sun_shadow` to populate
/// `FrameDataSun::occupancyBoundsCount_`. Both systems are part of the
/// RENDER pipeline; build runs before shadow each frame, so the value is
/// always fresh by the time shadow reads it.
inline int &occupancyEntityBoundsCount() {
    static int s_count = 0;
    return s_count;
}

} // namespace detail

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
        IRRender::createNamedResource<Buffer>(
            "OccupancyEntityBoundsBuffer",
            nullptr,
            kOccupancyBoundsSSBOByteSize,
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OccupancyEntityBounds
        );

        static Buffer *s_ssbo = IRRender::getNamedResource<Buffer>("OccupancyGridBuffer");
        static Buffer *s_boundsSSBO =
            IRRender::getNamedResource<Buffer>("OccupancyEntityBoundsBuffer");

        // Scratch reused across frames. Per-entity bbox accumulator keyed by
        // entity id; cleared at the start of each tick. Vector for upload is
        // packed each frame so the shader sees a contiguous, valid prefix.
        static std::unordered_map<IREntity::EntityId, GPUOccupancyEntityBounds> s_boundsByEntity;
        static std::vector<GPUOccupancyEntityBounds> s_boundsUpload;

        return createSystem<C_VoxelPool, C_OccupancyGrid>(
            "BuildOccupancyGrid",
            [](IREntity::EntityId, C_VoxelPool &pool, C_OccupancyGrid &grid) {
                IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);
                IR_ASSERT(
                    grid.sizeVoxels() == kMaxOccupancyGridSideVoxels,
                    "Lighting occupancy shaders currently require a 256^3 grid"
                );

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Clear", IR_PROFILER_COLOR_RENDER);
                    std::fill(grid.bitfield().begin(), grid.bitfield().end(), 0u);
                }
                s_boundsByEntity.clear();

                const auto &globals = pool.getPositionGlobals();
                const auto &colors = pool.getColors();
                const auto &voxelEntities = pool.getEntityIds();
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
                        const int wx = cell.x;
                        const int wy = cell.y;
                        const int wz = cell.z;
                        if (!grid.inBounds(wx, wy, wz))
                            continue;
                        grid.setBit(wx, wy, wz);

                        const IREntity::EntityId owner = voxelEntities[i];
                        if (owner == IREntity::kNullEntity)
                            continue;

                        auto [it, inserted] = s_boundsByEntity.try_emplace(owner);
                        GPUOccupancyEntityBounds &b = it->second;
                        if (inserted) {
                            b.entityId = uvec4(static_cast<std::uint32_t>(owner), 0u, 0u, 0u);
                            b.minCell = ivec4(wx, wy, wz, 0);
                            b.maxCell = ivec4(wx, wy, wz, 0);
                        } else {
                            b.minCell.x = IRMath::min(b.minCell.x, wx);
                            b.minCell.y = IRMath::min(b.minCell.y, wy);
                            b.minCell.z = IRMath::min(b.minCell.z, wz);
                            b.maxCell.x = IRMath::max(b.maxCell.x, wx);
                            b.maxCell.y = IRMath::max(b.maxCell.y, wy);
                            b.maxCell.z = IRMath::max(b.maxCell.z, wz);
                        }
                    }
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::Upload", IR_PROFILER_COLOR_RENDER);
                    s_ssbo->subData(0, grid.bitfieldByteSize(), grid.bitfield().data());
                }

                {
                    IR_PROFILE_BLOCK("BuildOccupancyGrid::UploadBounds", IR_PROFILER_COLOR_RENDER);
                    s_boundsUpload.clear();
                    s_boundsUpload.reserve(s_boundsByEntity.size());
                    for (auto &kv : s_boundsByEntity) {
                        if (static_cast<int>(s_boundsUpload.size()) >= kMaxOccupancyEntityBounds) {
                            IR_ASSERT(
                                false,
                                "Too many voxel-pool entities for OccupancyEntityBounds buffer"
                            );
                            break;
                        }
                        s_boundsUpload.push_back(kv.second);
                    }
                    if (!s_boundsUpload.empty()) {
                        s_boundsSSBO->subData(
                            0,
                            s_boundsUpload.size() * sizeof(GPUOccupancyEntityBounds),
                            s_boundsUpload.data()
                        );
                    }
                    detail::occupancyEntityBoundsCount() = static_cast<int>(s_boundsUpload.size());
                }
            },
            []() {
                s_ssbo->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_OccupancyGrid);
                s_boundsSSBO->bindBase(
                    BufferTarget::SHADER_STORAGE,
                    kBufferIndex_OccupancyEntityBounds
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_BUILD_OCCUPANCY_GRID_H */
