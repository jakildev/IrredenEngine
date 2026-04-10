#ifndef SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H
#define SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H

// PURPOSE: GPU compute system for transforming voxel local positions to
//   world-space global positions. The compute shader
//   (c_update_voxel_positions.glsl) runs one thread per voxel: it finds
//   which entity the voxel belongs to via GPUEntityTransform, then writes
//   globalPos = localPos + entityWorldPos to the global position buffer
//   (binding 5) that voxel-to-trixel shaders read.
// STATUS: WIP stub -- not registered in any creation's pipeline. The
//   system creates buffers and maps local positions, but:
//   - endTick only calls program->use(); no dispatchCompute is issued.
//   - EntityTransformBuffer is never uploaded with per-entity world
//     positions and pool offsets.
//   - UpdateParamsBuffer (entityCount) is never uploaded.
// TODO:
//   1. In the per-entity tick, collect GPUEntityTransform structs
//      (worldPosition, poolOffset, voxelCount) for each entity that
//      uses the voxel pool.
//   2. Upload the collected transforms to EntityTransformBuffer.
//   3. Upload entityCount to UpdateParamsBuffer.
//   4. Issue dispatchCompute with ceil(totalVoxels / 64) groups.
//   5. Register in the RENDER pipeline before VOXEL_TO_TRIXEL_STAGE_1.
//   6. PERF: The shader's linear entity search (for loop over entityCount)
//      will be O(entities) per voxel thread. Replace with a prefix-sum
//      offset table or binary search for scale.
// DEPENDENCIES: C_VoxelPool, C_Position3D, GPUEntityTransform,
//   GPUUpdateParams (ir_render_types.hpp).

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/voxel/components/component_voxel_set.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>

#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

template <> struct System<UPDATE_VOXEL_POSITIONS_GPU> {
    static SystemId create() {
        constexpr int kMaxGPUEntities = 4096;

        IRRender::createNamedResource<ShaderProgram>(
            "UpdateVoxelPositionsProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompUpdateVoxelPositions, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "LocalVoxelPositionBuffer",
            nullptr,
            IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
            BUFFER_STORAGE_MAP_WRITE | BUFFER_STORAGE_MAP_PERSISTENT | BUFFER_STORAGE_MAP_COHERENT,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_LocalVoxelPositions
        );
        IRRender::createNamedResource<Buffer>(
            "EntityTransformBuffer",
            nullptr,
            kMaxGPUEntities * sizeof(GPUEntityTransform),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_EntityTransforms
        );
        IRRender::createNamedResource<Buffer>(
            "UpdateParamsBuffer",
            nullptr,
            sizeof(GPUUpdateParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_UpdateParams
        );

        static void *mappedLocalPositions =
            IRRender::getNamedResource<Buffer>("LocalVoxelPositionBuffer")
                ->mapRange(
                    0,
                    IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
                    BUFFER_STORAGE_MAP_WRITE | BUFFER_STORAGE_MAP_PERSISTENT | BUFFER_STORAGE_MAP_COHERENT
                );
        static bool localPositionsUploaded = false;

        return createSystem<C_VoxelPool>(
            "UpdateVoxelPositionsGPU",
            [](C_VoxelPool &voxelPool) {
                if (!localPositionsUploaded && voxelPool.getLiveVoxelCount() > 0) {
                    std::memcpy(
                        mappedLocalPositions,
                        voxelPool.getPositions().data(),
                        voxelPool.getLiveVoxelCount() * sizeof(C_Position3D)
                    );
                    localPositionsUploaded = true;
                }
            },
            []() {
                IRRender::getNamedResource<ShaderProgram>("UpdateVoxelPositionsProgram")->use();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H */
