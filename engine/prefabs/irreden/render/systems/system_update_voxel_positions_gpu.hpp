#ifndef SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H
#define SYSTEM_UPDATE_VOXEL_POSITIONS_GPU_H

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
