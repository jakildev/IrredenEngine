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
#include <irreden/render/cull_viewport_state.hpp>

#include <irreden/render/gpu_stage_timing.hpp>

#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

inline ivec2 voxelDispatchGridForCount(int voxelCount) {
    constexpr int kMaxDispatchGroupsX = 1024;
    const int groupsX = IRMath::min(voxelCount, kMaxDispatchGroupsX);
    const int groupsY = IRMath::divCeil(voxelCount, groupsX);
    return ivec2(groupsX, groupsY);
}

inline const std::vector<std::uint32_t> &buildChunkVisibilityMask(
    C_VoxelPool &pool,
    vec2 cameraIso,
    ivec2 canvasOffsetZ1,
    ivec2 canvasSize,
    vec2 zoom = vec2(1.0f)
) {
    static thread_local std::vector<std::uint32_t> mask;
    pool.rebuildChunkBounds();
    int chunkCount = pool.getChunkCount();
    mask.assign(chunkCount, 0);

    constexpr int kMargin = 8;
    auto vp = IRMath::visibleIsoViewport(
        cameraIso, canvasOffsetZ1, canvasSize, zoom, kMargin
    );

    auto &bounds = pool.getChunkBounds();
    for (int c = 0; c < chunkCount; ++c) {
        const auto &cb = bounds[c];
        if (cb.isoMax_.x >= vp.min_.x && cb.isoMin_.x <= vp.max_.x &&
            cb.isoMax_.y >= vp.min_.y && cb.isoMin_.y <= vp.max_.y) {
            mask[c] = 1;
        }
    }
    return mask;
}

inline void buildVoxelFrameData(
    FrameDataVoxelToCanvas &frameData,
    const C_TriangleCanvasTextures &canvas,
    int liveVoxelCount
) {
    const auto renderMode = IRRender::getSubdivisionMode();
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
        const vec2 zoom = IRRender::getCameraZoom();
        IRE_LOG_INFO(
            "Voxel render mode={}, base_subdivisions={}, zoom_scale={}, "
            "effective_subdivisions={}",
            static_cast<int>(renderMode),
            IRRender::getVoxelRenderSubdivisions(),
            static_cast<int>(IRMath::round(IRMath::max(zoom.x, zoom.y))),
            effectiveSubdivisions
        );
        previousRenderMode = static_cast<int>(renderMode);
        previousEffectiveSubdivisions = effectiveSubdivisions;
    }
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
        canvas.getTextureDistances(), 0, &clearValue
    );
}

inline void syncEntityIds(C_VoxelPool &pool, int liveCount, Buffer *entityIdBuf) {
    if (!pool.isEntityIdsDirty()) {
        return;
    }
    entityIdBuf->subData(
        0, liveCount * sizeof(IREntity::EntityId), pool.getEntityIds().data());
    pool.clearEntityIdsDirty();
}

template <> struct System<VOXEL_TO_TRIXEL_STAGE_1> {
    static SystemId create() {
        static FrameDataVoxelToCanvas frameData{};
        IRRender::createNamedResource<ShaderProgram>(
            "VoxelCompactProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompVoxelVisibilityCompact, ShaderType::COMPUTE}
            }
        );
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
        constexpr int kMaxVoxelPoolChunks =
            IRMath::divCeil(IRConstants::kMaxSingleVoxels, IRRender::kVoxelChunkSize);
        IRRender::createNamedResource<Buffer>(
            "ChunkVisibilityBuffer",
            nullptr,
            kMaxVoxelPoolChunks * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_ChunkVisibility
        );
        IRRender::createNamedResource<Buffer>(
            "CompactedVoxelIndices",
            nullptr,
            IRConstants::kMaxSingleVoxels * sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_CompactedVoxelIndices
        );
        IRRender::createNamedResource<Buffer>(
            "IndirectDispatchParams",
            nullptr,
            sizeof(VoxelIndirectDispatchParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_IndirectDispatchParams
        );

        static ShaderProgram *s_compactProgram =
            IRRender::getNamedResource<ShaderProgram>("VoxelCompactProgram");
        static ShaderProgram *s_stage1Program =
            IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1");
        static Buffer *s_frameDataBuf =
            IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        static Buffer *s_voxelPosBuf =
            IRRender::getNamedResource<Buffer>("VoxelPositionBuffer");
        static Buffer *s_voxelColorBuf =
            IRRender::getNamedResource<Buffer>("VoxelColorBuffer");
        static Buffer *s_voxelEntityIdBuf =
            IRRender::getNamedResource<Buffer>("VoxelEntityIdBuffer");
        static Buffer *s_chunkVisBuf =
            IRRender::getNamedResource<Buffer>("ChunkVisibilityBuffer");
        static Buffer *s_indirectBuf =
            IRRender::getNamedResource<Buffer>("IndirectDispatchParams");

        return createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasFirst",
            [](IREntity::EntityId &entity,
               C_VoxelPool &voxelPool,
               C_TriangleCanvasTextures &triangleCanvasTextures) {
                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0, t1;

                const int liveVoxelCount = voxelPool.getLiveVoxelCount();
                if (liveVoxelCount == 0) return;

                IRRender::updateCullViewport(
                    IRRender::getCameraPosition2DIso(),
                    IRRender::getCameraZoom(),
                    triangleCanvasTextures.size_
                );
                const auto &cull = IRRender::getCullViewport();

                buildVoxelFrameData(frameData, triangleCanvasTextures, liveVoxelCount);

                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }
                clearCanvasAndDistances(entity, triangleCanvasTextures);
                if (timing.enabled_) { IRRender::device()->finish(); t1 = IRRender::SteadyClock::now(); timing.canvasClearMs_ = IRRender::elapsedMs(t0, t1); }

                ivec2 cullOffsetZ1 = IRMath::trixelOriginOffsetZ1(cull.canvasSize_);
                const auto &uploadMask = buildChunkVisibilityMask(
                    voxelPool, cull.cameraIso_, cullOffsetZ1, cull.canvasSize_, cull.zoom_
                );
                s_chunkVisBuf->subData(
                    0,
                    uploadMask.size() * sizeof(std::uint32_t),
                    uploadMask.data()
                );

                constexpr int kGpuMargin = 4;
                auto cullVp = cull.isoViewport(kGpuMargin);
                frameData.cullIsoMin_ = ivec2(glm::floor(cullVp.min_));
                frameData.cullIsoMax_ = ivec2(glm::ceil(cullVp.max_));
                s_frameDataBuf->subData(0, sizeof(FrameDataVoxelToCanvas), &frameData);

                s_voxelPosBuf->subData(
                    0,
                    liveVoxelCount * sizeof(C_PositionGlobal3D),
                    voxelPool.getPositionGlobals().data()
                );
                s_voxelColorBuf->subData(
                    0,
                    liveVoxelCount * sizeof(C_Voxel),
                    voxelPool.getColors().data()
                );
                syncEntityIds(voxelPool, liveVoxelCount, s_voxelEntityIdBuf);

                // --- GPU visibility compaction ---
                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }

                static const VoxelIndirectDispatchParams zeroed{};
                s_indirectBuf->subData(0, sizeof(VoxelIndirectDispatchParams), &zeroed);

                s_compactProgram->use();
                constexpr int kCompactLocalSize = 64;
                const int compactGroups = IRMath::divCeil(liveVoxelCount, kCompactLocalSize);
                const ivec2 compactGrid = voxelDispatchGridForCount(compactGroups);
                IRRender::device()->dispatchCompute(compactGrid.x, compactGrid.y, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
                IRRender::device()->memoryBarrier(BarrierType::COMMAND);

                if (timing.enabled_) { IRRender::device()->finish(); t1 = IRRender::SteadyClock::now(); timing.voxelCompactMs_ = IRRender::elapsedMs(t0, t1); }

                // --- Stage 1: indirect dispatch from compacted visible set ---
                if (timing.enabled_) { t0 = IRRender::SteadyClock::now(); }

                s_stage1Program->use();
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                IRRender::device()->dispatchComputeIndirect(s_indirectBuf, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                if (timing.enabled_) { IRRender::device()->finish(); t1 = IRRender::SteadyClock::now(); timing.voxelStage1Ms_ = IRRender::elapsedMs(t0, t1); }
            },
            []() {
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

        static ShaderProgram *s_stage2Program =
            IRRender::getNamedResource<ShaderProgram>("SingleVoxel2");
        static Buffer *s_indirectBuf =
            IRRender::getNamedResource<Buffer>("IndirectDispatchParams");

        return createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasSecond",
            [](const C_VoxelPool &voxelPool, C_TriangleCanvasTextures &triangleCanvasTextures) {
                if (voxelPool.getLiveVoxelCount() == 0) return;

                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint t0;
                if (timing.enabled_) { IRRender::device()->finish(); t0 = IRRender::SteadyClock::now(); }

                triangleCanvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::WRITE_ONLY, TextureFormat::R32I
                );
                triangleCanvasTextures.getTextureEntityIds()->bindAsImage(
                    2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI
                );

                IRRender::device()->dispatchComputeIndirect(s_indirectBuf, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                if (timing.enabled_) { IRRender::device()->finish(); timing.voxelStage2Ms_ = IRRender::elapsedMs(t0, IRRender::SteadyClock::now()); }
            },
            []() { s_stage2Program->use(); }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
