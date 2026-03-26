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

inline void buildVoxelFrameData(
    FrameDataVoxelToCanvas &frameData,
    const C_TriangleCanvasTextures &canvas,
    int liveVoxelCount
) {
    const auto renderMode = IRRender::getVoxelRenderMode();
    const int baseSubdivisions = IRRender::getVoxelRenderSubdivisions();
    const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
    const ivec2 dispatchGrid = voxelDispatchGridForCount(liveVoxelCount);

    frameData.cameraTrixelOffset_ = IRRender::getCameraPosition2DIso();
    frameData.trixelCanvasOffsetZ1_ = IRMath::trixelOriginOffsetZ1(canvas.size_);
    frameData.voxelRenderOptions_ =
        ivec2(static_cast<int>(renderMode), effectiveSubdivisions);
    frameData.voxelDispatchGrid_ = dispatchGrid;
    frameData.voxelCount_ = liveVoxelCount;
    frameData.canvasSizePixels_ = canvas.size_;

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
}

inline void clearCanvasAndDistances(
    IREntity::EntityId canvasEntity,
    C_TriangleCanvasTextures &canvas
) {
    auto background =
        IREntity::getComponentOptional<C_TriangleCanvasBackground>(canvasEntity);
    if (background.has_value()) {
        (*background.value()).clearCanvasWithBackground(canvas);
    } else {
        canvas.clear();
    }
    static const std::int32_t clearValue =
        static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
    IRRender::device()->clearTexImage(
        canvas.getTextureDistances()->getHandle(), 0, &clearValue
    );
}

inline void syncEntityIds(C_VoxelPool &pool, int liveCount) {
    if (!pool.isEntityIdsDirty()) {
        return;
    }
    IRRender::getNamedResource<Buffer>("VoxelEntityIdBuffer")
        ->subData(0, liveCount * sizeof(IREntity::EntityId), pool.getEntityIds().data());
    pool.clearEntityIdsDirty();
}

template <> struct System<VOXEL_TO_TRIXEL_STAGE_1> {
    static SystemId create() {
        static FrameDataVoxelToCanvas frameData{};
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
        return createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasFirst",
            [](IREntity::EntityId &entity,
               C_VoxelPool &voxelPool,
               C_TriangleCanvasTextures &triangleCanvasTextures) {
                const int liveVoxelCount = voxelPool.getLiveVoxelCount();

                buildVoxelFrameData(frameData, triangleCanvasTextures, liveVoxelCount);
                clearCanvasAndDistances(entity, triangleCanvasTextures);

                IRRender::getNamedResource<Buffer>("VoxelPositionBuffer")
                    ->subData(
                        0,
                        liveVoxelCount * sizeof(C_PositionGlobal3D),
                        voxelPool.getPositionGlobals().data()
                    );
                IRRender::getNamedResource<Buffer>("VoxelColorBuffer")
                    ->subData(
                        0,
                        liveVoxelCount * sizeof(C_Voxel),
                        voxelPool.getColors().data()
                    );
                syncEntityIds(voxelPool, liveVoxelCount);

                const ivec2 dispatchGrid = voxelDispatchGridForCount(liveVoxelCount);
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                IRRender::device()->dispatchCompute(dispatchGrid.x, dispatchGrid.y, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
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
            []() {}
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
                    voxelDispatchGridForCount(voxelPool.getLiveVoxelCount());
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
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            []() { IRRender::getNamedResource<ShaderProgram>("SingleVoxel2")->use(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
