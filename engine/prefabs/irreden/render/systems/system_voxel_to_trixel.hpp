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
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/camera.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

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
    IsoBounds2D viewport
) {
    static thread_local std::vector<std::uint32_t> mask;
    pool.rebuildChunkBounds();
    int chunkCount = pool.getChunkCount();
    mask.assign(chunkCount, 0);

    auto &bounds = pool.getChunkBounds();
    for (int c = 0; c < chunkCount; ++c) {
        const auto &cb = bounds[c];
        if (cb.isoMax_.x >= viewport.min_.x && cb.isoMin_.x <= viewport.max_.x &&
            cb.isoMax_.y >= viewport.min_.y && cb.isoMin_.y <= viewport.max_.y) {
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

    // visualYaw_, residualYaw_, and _yawPadding_ not consumed in T-055; scaffolded for T-058 (screen-space residual composite)
    frameData.visualYaw_ = IRPrefab::Camera::getYaw();
    const auto [rasterYaw, residualYaw] =
        IRPrefab::Camera::computeYawSplit(frameData.visualYaw_);
    frameData.rasterYaw_ = rasterYaw;
    frameData.residualYaw_ = residualYaw;
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
    struct Params {
        ShaderProgram *compactProgram_ = nullptr;
        ShaderProgram *stage1Program_ = nullptr;
        Buffer *frameDataBuf_ = nullptr;
        Buffer *voxelPosBuf_ = nullptr;
        Buffer *voxelColorBuf_ = nullptr;
        Buffer *voxelEntityIdBuf_ = nullptr;
        Buffer *chunkVisBuf_ = nullptr;
        Buffer *indirectBuf_ = nullptr;
        FrameDataVoxelToCanvas frameData_{};
        // Log-throttle state — emit the render-mode log line only when
        // mode or effective subdivisions change.
        int previousRenderMode_ = -1;
        int previousEffectiveSubdivisions_ = -1;
    };

    static SystemId create() {
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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->compactProgram_ = IRRender::getNamedResource<ShaderProgram>("VoxelCompactProgram");
        p->stage1Program_ = IRRender::getNamedResource<ShaderProgram>("SingleVoxelProgram1");
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        p->voxelPosBuf_ = IRRender::getNamedResource<Buffer>("VoxelPositionBuffer");
        p->voxelColorBuf_ = IRRender::getNamedResource<Buffer>("VoxelColorBuffer");
        p->voxelEntityIdBuf_ = IRRender::getNamedResource<Buffer>("VoxelEntityIdBuffer");
        p->chunkVisBuf_ = IRRender::getNamedResource<Buffer>("ChunkVisibilityBuffer");
        p->indirectBuf_ = IRRender::getNamedResource<Buffer>("IndirectDispatchParams");

        SystemId systemId = createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasFirst",
            [p](IREntity::EntityId &entity,
                C_VoxelPool &voxelPool,
                C_TriangleCanvasTextures &triangleCanvasTextures) {
                const int liveVoxelCount = voxelPool.getLiveVoxelCount();
                if (liveVoxelCount == 0) return;

                IRRender::updateCullViewport(
                    IRRender::getCameraPosition2DIso(),
                    IRRender::getCameraZoom(),
                    triangleCanvasTextures.size_
                );
                const auto &cull = IRRender::getCullViewport();

                buildVoxelFrameData(
                    p->frameData_, triangleCanvasTextures, liveVoxelCount
                );

                const int renderMode = p->frameData_.voxelRenderOptions_.x;
                const int effectiveSub = p->frameData_.voxelRenderOptions_.y;
                if (renderMode != p->previousRenderMode_ ||
                    effectiveSub != p->previousEffectiveSubdivisions_) {
                    const vec2 zoom = IRRender::getCameraZoom();
                    IRE_LOG_INFO(
                        "Voxel render mode={}, base_subdivisions={}, zoom_scale={}, "
                        "effective_subdivisions={}",
                        renderMode,
                        IRRender::getVoxelRenderSubdivisions(),
                        static_cast<int>(IRMath::round(IRMath::max(zoom.x, zoom.y))),
                        effectiveSub
                    );
                    p->previousRenderMode_ = renderMode;
                    p->previousEffectiveSubdivisions_ = effectiveSub;
                }

                clearCanvasAndDistances(entity, triangleCanvasTextures);

                // Widen both cull regions (chunk-pre-filter and per-pixel
                // GPU bounds) to the shadow-feeder AABB when sun shadows
                // are enabled, so off-screen casters within
                // kSunShadowMaxDistance still write into trixelDistances
                // and feed BAKE_SUN_SHADOW_MAP. When shadows are off the
                // sweep collapses to zero and bounds match the visible
                // viewport (legacy behavior). The sun-direction lookup
                // is gated on the shadow flag too, so disabled-shadow
                // creations skip the C_LightSource archetype scan.
                const bool shadowsEnabled = IRRender::getSunShadowsEnabled();
                const vec3 sunDir = shadowsEnabled
                    ? IRPrefab::SunShadow::resolveDirection()
                    : vec3(0.0f);
                const float sweepDistance = shadowsEnabled
                    ? IRPrefab::SunShadow::kSunShadowMaxDistance
                    : 0.0f;

                constexpr int kChunkMargin = 8;
                const IsoBounds2D chunkVp = IRMath::shadowFeederIsoBounds(
                    cull.isoViewport(kChunkMargin), sunDir, sweepDistance
                );
                const auto &uploadMask = buildChunkVisibilityMask(voxelPool, chunkVp);
                p->chunkVisBuf_->subData(
                    0,
                    uploadMask.size() * sizeof(std::uint32_t),
                    uploadMask.data()
                );

                constexpr int kGpuMargin = 4;
                const IsoBounds2D gpuVp = IRMath::shadowFeederIsoBounds(
                    cull.isoViewport(kGpuMargin), sunDir, sweepDistance
                );
                p->frameData_.cullIsoMin_ = ivec2(IRMath::floor(gpuVp.min_));
                p->frameData_.cullIsoMax_ = ivec2(IRMath::ceil(gpuVp.max_));
                p->frameDataBuf_->subData(0, sizeof(FrameDataVoxelToCanvas), &p->frameData_);

                p->voxelPosBuf_->subData(
                    0,
                    liveVoxelCount * sizeof(C_PositionGlobal3D),
                    voxelPool.getPositionGlobals().data()
                );
                p->voxelColorBuf_->subData(
                    0,
                    liveVoxelCount * sizeof(C_Voxel),
                    voxelPool.getColors().data()
                );
                syncEntityIds(voxelPool, liveVoxelCount, p->voxelEntityIdBuf_);

                const VoxelIndirectDispatchParams zeroed{};
                p->indirectBuf_->subData(0, sizeof(VoxelIndirectDispatchParams), &zeroed);

                p->compactProgram_->use();
                constexpr int kCompactLocalSize = 64;
                const int compactGroups = IRMath::divCeil(liveVoxelCount, kCompactLocalSize);
                const ivec2 compactGrid = voxelDispatchGridForCount(compactGroups);
                IRRender::device()->dispatchCompute(compactGrid.x, compactGrid.y, 1);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
                IRRender::device()->memoryBarrier(BarrierType::COMMAND);

                p->stage1Program_->use();
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::READ_ONLY, TextureFormat::R32I
                );
                IRRender::device()->dispatchComputeIndirect(p->indirectBuf_, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
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

        setSystemParams(systemId, std::move(paramsOwner));
        // The observer-based timing brackets the entire system tick. The
        // formerly-separate canvasClear and voxelCompact sub-stages now
        // collapse into voxelStage1's measurement; their registry slots
        // remain at 0.0f for API-compatibility.
        IRRender::tagGpuStage(systemId, "voxelStage1");
        return systemId;
    }
};

template <> struct System<VOXEL_TO_TRIXEL_STAGE_2> {
    struct Params {
        ShaderProgram *stage2Program_ = nullptr;
        Buffer *indirectBuf_ = nullptr;
    };

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "SingleVoxel2",
            std::vector{
                ShaderStage{IRRender::kFileCompVoxelToTrixelStage2, ShaderType::COMPUTE}
            }
        );

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->stage2Program_ = IRRender::getNamedResource<ShaderProgram>("SingleVoxel2");
        p->indirectBuf_ = IRRender::getNamedResource<Buffer>("IndirectDispatchParams");

        SystemId systemId = createSystem<C_VoxelPool, C_TriangleCanvasTextures>(
            "SingleVoxelToCanvasSecond",
            [p](const C_VoxelPool &voxelPool, C_TriangleCanvasTextures &triangleCanvasTextures) {
                if (voxelPool.getLiveVoxelCount() == 0) return;

                triangleCanvasTextures.getTextureColors()->bindAsImage(
                    0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                );
                triangleCanvasTextures.getTextureDistances()->bindAsImage(
                    1, TextureAccess::WRITE_ONLY, TextureFormat::R32I
                );
                triangleCanvasTextures.getTextureEntityIds()->bindAsImage(
                    2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI
                );

                IRRender::device()->dispatchComputeIndirect(p->indirectBuf_, 0);
                IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
            },
            [p]() { p->stage2Program_->use(); }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        IRRender::tagGpuStage(systemId, "voxelStage2");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VOXEL_TO_TRIXEL_H */
