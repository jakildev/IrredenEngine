#ifndef SYSTEM_VOXEL_TO_TRIXEL_H
#define SYSTEM_VOXEL_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/common/components/component_tags_all.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera_position_2d_iso.hpp>
#include <irreden/voxel/components/component_voxel.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

inline ivec2 voxelDispatchGridForCount(int voxelCount) {
    constexpr int kMaxDispatchGroupsX = 1024;
    const int groupsX = IRMath::min(voxelCount, kMaxDispatchGroupsX);
    const int groupsY = (voxelCount + groupsX - 1) / groupsX;
    return ivec2(groupsX, groupsY);
}

template <> struct System<VOXEL_TO_TRIXEL_STAGE_1> {
    static SystemId create() {
        static FrameDataVoxelToCanvas frameData{};
        const ivec2 scratchCanvasSize = ivec2(IRRender::getMainCanvasSizeTrixels());
        IRRender::createNamedResource<ShaderProgram>(
            "SingleVoxelProgram1",
            std::vector{
                ShaderStage{IRRender::kFileCompVoxelToTrixelStage1, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "SingleVoxelFrameData",
            nullptr,
            sizeof(FrameDataVoxelToCanvas),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataVoxelToCanvas
        );
        IRRender::createNamedResource<Buffer>(
            "VoxelPositionBuffer",
            nullptr,
            IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SingleVoxelPositions
        );
        IRRender::createNamedResource<Buffer>(
            "VoxelColorBuffer",
            nullptr,
            IRConstants::kMaxSingleVoxels * sizeof(C_Voxel),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SingleVoxelColors
        );
        IRRender::createNamedResource<Buffer>(
            "VoxelEntityIdBuffer",
            nullptr,
            IRConstants::kMaxSingleVoxels * sizeof(IREntity::EntityId),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_VoxelEntityIds
        );
        IRRender::createNamedResource<Buffer>(
            "TrixelDistanceScratchBuffer",
            nullptr,
            static_cast<std::size_t>(scratchCanvasSize.x) *
                static_cast<std::size_t>(scratchCanvasSize.y) *
                sizeof(std::int32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_TrixelDistanceScratch
        );
        return createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasFirst",
            [](IREntity::EntityId &entity,
               C_VoxelPool &voxelPool,
               C_TriangleCanvasTextures &triangleCanvasTextures) {
                static std::vector<std::int32_t> scratchDistances;
                frameData.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
                frameData.trixelCanvasOffsetZ1_ =
                    IRMath::trixelOriginOffsetZ1(triangleCanvasTextures.size_);
                const IRRender::VoxelRenderMode renderMode = IRRender::getVoxelRenderMode();
                const int baseSubdivisions = IRRender::getVoxelRenderSubdivisions();
                const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
                const ivec2 dispatchGrid =
                    voxelDispatchGridForCount(voxelPool.getVoxelPoolSize());
                frameData.voxelRenderOptions_ =
                    ivec2(static_cast<int>(renderMode), effectiveSubdivisions);
                frameData.voxelDispatchGrid_ = dispatchGrid;
                frameData.voxelCount_ = voxelPool.getVoxelPoolSize();
                static int previousRenderMode = -1;
                static int previousEffectiveSubdivisions = -1;
                if (static_cast<int>(renderMode) != previousRenderMode ||
                    effectiveSubdivisions != previousEffectiveSubdivisions) {
                    IRE_LOG_INFO(
                        "Voxel render mode={}, base_subdivisions={}, zoom_scale={}, "
                        "effective_subdivisions={}",
                        static_cast<int>(renderMode),
                        baseSubdivisions,
                        static_cast<int>(IRMath::round(
                            IRMath::max(IRRender::getCameraZoom().x, IRRender::getCameraZoom().y)
                        )),
                        effectiveSubdivisions
                    );
                    previousRenderMode = static_cast<int>(renderMode);
                    previousEffectiveSubdivisions = effectiveSubdivisions;
                }

                IRRender::getNamedResource<Buffer>("SingleVoxelFrameData")
                    ->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData);
                const std::size_t scratchSize =
                    static_cast<std::size_t>(triangleCanvasTextures.size_.x) *
                    static_cast<std::size_t>(triangleCanvasTextures.size_.y);
                auto background =
                    IREntity::getComponentOptional<C_TriangleCanvasBackground>(entity);
                if (background.has_value()) {
                    (*background.value()).clearCanvasWithBackground(triangleCanvasTextures);
                } else {
                    triangleCanvasTextures.clear();
                }
                scratchDistances.assign(
                    scratchSize,
                    static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance)
                );
                IRRender::getNamedResource<Buffer>("TrixelDistanceScratchBuffer")
                    ->subData(
                        0,
                        scratchSize * sizeof(std::int32_t),
                        scratchDistances.data()
                    );
                // TODO: each voxel allocation should have own
                // voxel GPU buffers as well.
                IRRender::getNamedResource<Buffer>("VoxelPositionBuffer")
                    ->subData(
                        0,
                        voxelPool.getVoxelPoolSize() * sizeof(C_PositionGlobal3D),
                        voxelPool.getPositionGlobals().data()
                    );
                IRRender::getNamedResource<Buffer>("VoxelColorBuffer")
                    ->subData(
                        0,
                        voxelPool.getVoxelPoolSize() * sizeof(C_Voxel),
                        voxelPool.getColors().data()
                    );

                if (voxelPool.isEntityIdsDirty()) {
                    IRE_LOG_DEBUG(
                        "[Stage1] Uploading entity IDs to SSBO, poolSize={}, first few IDs: {}, {}, {}",
                        voxelPool.getVoxelPoolSize(),
                        voxelPool.getEntityIds().size() > 0 ? voxelPool.getEntityIds()[0] : 0,
                        voxelPool.getEntityIds().size() > 1 ? voxelPool.getEntityIds()[1] : 0,
                        voxelPool.getEntityIds().size() > 2 ? voxelPool.getEntityIds()[2] : 0
                    );
                    IRRender::getNamedResource<Buffer>("VoxelEntityIdBuffer")
                        ->subData(
                            0,
                            voxelPool.getVoxelPoolSize() * sizeof(IREntity::EntityId),
                            voxelPool.getEntityIds().data()
                        );
                    voxelPool.clearEntityIdsDirty();
                }

                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                IRRender::device()->dispatchCompute(dispatchGrid.x, dispatchGrid.y, 1);
                // TODO: Look over all barriers and try and make the minimum necessary to speed up rendering
                IRRender::device()->memoryBarrier(BarrierType::ALL);
            },
            []() {
                IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1")->use();
                IREntity::EntityId backgroundCanvas = IRRender::getCanvas("background");
                auto background =
                    IREntity::getComponentOptional<C_TriangleCanvasBackground>(backgroundCanvas);
                auto backgroundTextures =
                    IREntity::getComponentOptional<C_TriangleCanvasTextures>(backgroundCanvas);
                if (background.has_value() && backgroundTextures.has_value()) {
                    (*background.value()).clearCanvasWithBackground(*backgroundTextures.value());
                }
            },
            []() {

            }
        );
    }
};

template <> struct System<VOXEL_TO_TRIXEL_STAGE_2> {
    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "SingleVoxel2",
            std::vector{
                ShaderStage{IRRender::kFileCompVoxelToTrixelStage2, ShaderType::COMPUTE}
            }
        );
        return createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasSecond",
            [](const C_VoxelPool &voxelPool, C_TriangleCanvasTextures &triangleCanvasTextures) {
                const ivec2 dispatchGrid =
                    voxelDispatchGridForCount(voxelPool.getVoxelPoolSize());
                IRRender::getNamedResource<Buffer>("VoxelPositionBuffer")
                    ->subData(
                        0,
                        voxelPool.getVoxelPoolSize() * sizeof(C_PositionGlobal3D),
                        voxelPool.getPositionGlobals().data()
                    );
                IRRender::getNamedResource<Buffer>("VoxelColorBuffer")
                    ->subData(
                        0,
                        voxelPool.getVoxelPoolSize() * sizeof(C_Voxel),
                        voxelPool.getColors().data()
                    );
                triangleCanvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::WRITE_ONLY, TextureFormat::R32I
                );
                triangleCanvasTextures.getTextureEntityIds()->bindAsImage(
                    2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI
                );

                IRRender::device()->dispatchCompute(dispatchGrid.x, dispatchGrid.y, 1);
                IRRender::device()->memoryBarrier(BarrierType::ALL);
            },
            []() { IRRender::getNamedResource<ShaderProgram>("SingleVoxel2")->use(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
