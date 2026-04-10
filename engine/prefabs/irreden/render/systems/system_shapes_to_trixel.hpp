#ifndef SYSTEM_SHAPES_TO_TRIXEL_H
#define SYSTEM_SHAPES_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/cull_viewport_state.hpp>

#include <irreden/render/gpu_stage_timing.hpp>

#include <cmath>

#include <cstring>
#include <optional>
#include <unordered_map>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

constexpr int kMaxShapeDescriptors = 8192;
constexpr int kMaxShapeTileDescriptors = 262144;
constexpr std::uint32_t kBufferIndex_ShapesFrameData = 23;
constexpr int kShapeTileSize = 8;

template <> struct System<SHAPES_TO_TRIXEL> {
    static SystemId create() {
        static GPUShapesFrameData frameData{};
        using CanvasId = IREntity::EntityId;
        static std::unordered_map<CanvasId, std::vector<GPUShapeDescriptor>> gpuShapesByCanvas;

        static std::optional<IsoBounds2D> cullBounds;

        IRRender::createNamedResource<ShaderProgram>(
            "ShapesToTrixelProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompShapesToTrixel, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "ShapeDescriptorBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUShapeDescriptor),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_ShapeDescriptors
        );
        IRRender::createNamedResource<Buffer>(
            "ShapesFrameDataBuffer",
            nullptr,
            sizeof(GPUShapesFrameData),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_ShapesFrameData
        );
        IRRender::createNamedResource<Buffer>(
            "JointTransformBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUJointTransform),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_JointTransforms
        );
        IRRender::createNamedResource<Buffer>(
            "AnimationParamsBuffer",
            nullptr,
            kMaxShapeDescriptors * sizeof(GPUAnimationParams),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_AnimationParams
        );
        IRRender::createNamedResource<Buffer>(
            "ShapeTileDescriptorBuffer",
            nullptr,
            kMaxShapeTileDescriptors * sizeof(ShapeTileDescriptor),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_ShapeTileDescriptors
        );

        static ShaderProgram *s_shapesProgram =
            IRRender::getNamedResource<ShaderProgram>("ShapesToTrixelProgram");
        static Buffer *s_shapeDescBuf =
            IRRender::getNamedResource<Buffer>("ShapeDescriptorBuffer");
        static Buffer *s_shapesFrameDataBuf =
            IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer");
        static Buffer *s_shapeTileDescBuf =
            IRRender::getNamedResource<Buffer>("ShapeTileDescriptorBuffer");

        return createSystem<C_ShapeDescriptor, C_PositionGlobal3D>(
            "ShapesToTrixel",
            [](IREntity::EntityId &entityId,
               const C_ShapeDescriptor &shape,
               const C_PositionGlobal3D &pos) {
                if (cullBounds.has_value()) {
                    vec2 shapeIsoPosition = IRMath::pos3DtoPos2DIso(pos.pos_);
                    vec2 shapeIsoHalfExtent = IRMath::shapeIsoHalfExtent(vec3(shape.params_));
                    if (shapeIsoPosition.x + shapeIsoHalfExtent.x < cullBounds->min_.x ||
                        shapeIsoPosition.x - shapeIsoHalfExtent.x > cullBounds->max_.x ||
                        shapeIsoPosition.y + shapeIsoHalfExtent.y < cullBounds->min_.y ||
                        shapeIsoPosition.y - shapeIsoHalfExtent.y > cullBounds->max_.y) {
                        return;
                    }
                }

                CanvasId canvas = shape.canvasEntity_;
                if (canvas == IREntity::kNullEntity) {
                    canvas = IRRender::getActiveCanvasEntity();
                }

                auto &bucket = gpuShapesByCanvas[canvas];
                if (static_cast<int>(bucket.size()) >= kMaxShapeDescriptors) {
                    return;
                }
                GPUShapeDescriptor desc{};
                desc.worldPosition = vec4(pos.pos_, 1.0f);
                desc.params = shape.params_;
                desc.shapeType = static_cast<std::uint32_t>(shape.shapeType_);
                desc.color = shape.color_.toPackedRGBA();
                desc.entityId = entityId;
                desc.jointIndex = 0;
                desc.flags = shape.flags_;
                desc.lodLevel = shape.lodLevel_;
                bucket.push_back(desc);
            },
            []() {
                gpuShapesByCanvas.clear();

                IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
                auto texOpt =
                    IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvas);
                if (texOpt.has_value()) {
                    IRRender::updateCullViewport(
                        IRRender::getCameraPosition2DIso(),
                        IRRender::getCameraZoom(),
                        texOpt.value()->size_
                    );
                    constexpr int kMargin = 4;
                    cullBounds = IRRender::getCullViewport().isoViewport(kMargin);
                } else {
                    cullBounds.reset();
                }
            },
            []() {
                auto &timing = IRRender::gpuStageTiming();
                IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();

                for (auto &[canvasId, gpuShapes] : gpuShapesByCanvas) {
                    if (gpuShapes.empty()) {
                        continue;
                    }

                    auto texturesOpt =
                        IREntity::getComponentOptional<C_TriangleCanvasTextures>(canvasId);
                    if (!texturesOpt.has_value()) {
                        continue;
                    }
                    auto &canvasTextures = *texturesOpt.value();

                    if (canvasId == mainCanvas) {
                        frameData.cameraTrixelOffset = IRRender::getCameraPosition2DIso();
                    } else {
                        canvasTextures.clear();
                        vec3 entityPos = vec3(gpuShapes[0].worldPosition);
                        frameData.cameraTrixelOffset = -pos3DtoPos2DIso(entityPos);
                    }
                    frameData.trixelCanvasOffsetZ1 =
                        IRMath::trixelOriginOffsetZ1(canvasTextures.size_);
                    frameData.canvasSize = canvasTextures.size_;
                    frameData.shapeCount = static_cast<int>(gpuShapes.size());
                    frameData.voxelRenderOptions = ivec2(
                        static_cast<int>(IRRender::getVoxelRenderMode()),
                        IRRender::getVoxelRenderEffectiveSubdivisions()
                    );
                    if (cullBounds.has_value() && canvasId == mainCanvas) {
                        frameData.cullIsoMin = ivec2(glm::floor(cullBounds->min_));
                        frameData.cullIsoMax = ivec2(glm::ceil(cullBounds->max_));
                    } else {
                        frameData.cullIsoMin = ivec2(-999999);
                        frameData.cullIsoMax = ivec2(999999);
                    }

                    s_shapeDescBuf->subData(
                        0,
                        gpuShapes.size() * sizeof(GPUShapeDescriptor),
                        gpuShapes.data()
                    );

                    const auto renderMode = IRRender::getVoxelRenderMode();
                    const int effectiveSub = IRRender::getVoxelRenderEffectiveSubdivisions();

                    const int tileCount = buildAndUploadTileDescriptors(
                        gpuShapes, s_shapeTileDescBuf, effectiveSub, renderMode);
                    if (tileCount == 0) {
                        continue;
                    }

                    s_shapesProgram->use();
                    canvasTextures.getTextureDistances()->bindAsImage(
                        1, TextureAccess::READ_WRITE, TextureFormat::R32I
                    );

                    // Pass 0: depth via imageAtomicMin
                    frameData.passIndex = 0;
                    s_shapesFrameDataBuf->subData(0, sizeof(GPUShapesFrameData), &frameData);

                    IRRender::TimePoint pass0Start;
                    if (timing.enabled_) {
                        IRRender::device()->finish();
                        pass0Start = IRRender::SteadyClock::now();
                    }

                    IRRender::device()->dispatchCompute(
                        static_cast<std::uint32_t>(tileCount), 1, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                    if (timing.enabled_) {
                        IRRender::device()->finish();
                        timing.shapePass0Ms_ = IRRender::elapsedMs(
                            pass0Start, IRRender::SteadyClock::now());
                    }

                    // Pass 1: color + entity ID where depth matches
                    IRRender::TimePoint pass1Start;
                    if (timing.enabled_) {
                        pass1Start = IRRender::SteadyClock::now();
                    }

                    canvasTextures.getTextureColors()->bindAsImage(
                        0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA8
                    );
                    canvasTextures.getTextureEntityIds()->bindAsImage(
                        2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI
                    );
                    frameData.passIndex = 1;
                    s_shapesFrameDataBuf->subData(0, sizeof(GPUShapesFrameData), &frameData);

                    IRRender::device()->dispatchCompute(
                        static_cast<std::uint32_t>(tileCount), 1, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                    if (timing.enabled_) {
                        IRRender::device()->finish();
                        timing.shapePass1Ms_ = IRRender::elapsedMs(
                            pass1Start, IRRender::SteadyClock::now());
                        timing.visibleShapeCount_ = static_cast<std::uint32_t>(gpuShapes.size());
                        timing.shapeGroupsZ_ = 0;
                    }
                }
            }
        );
    }

    // Builds a per-tile descriptor stream for every visible shape, uploads
    // it to the SSBO, and returns the total tile count. The batched compute
    // dispatch then runs once per pass, with gl_WorkGroupID.x indexing this
    // buffer — one workgroup per 8×8 pixel tile.
    static int buildAndUploadTileDescriptors(
        const std::vector<GPUShapeDescriptor> &gpuShapes,
        Buffer *tileDescBuf,
        int effectiveSubdivisions,
        IRRender::VoxelRenderMode renderMode
    ) {
        static thread_local std::vector<ShapeTileDescriptor> tiles;
        tiles.clear();

        const bool smoothMode =
            (renderMode != IRRender::VoxelRenderMode::SNAPPED);
        const int sub = smoothMode ? effectiveSubdivisions : 1;

        for (int i = 0; i < static_cast<int>(gpuShapes.size()); ++i) {
            const auto &desc = gpuShapes[i];
            vec3 worldPos = vec3(desc.worldPosition);
            ivec3 origin = ivec3(glm::round(worldPos));

            vec3 boundingHalf;
            auto shapeType =
                static_cast<IRRender::ShapeType>(desc.shapeType);
            switch (shapeType) {
                case IRRender::ShapeType::BOX:
                    boundingHalf = (vec3(desc.params) - 1.0f) * 0.5f;
                    break;
                case IRRender::ShapeType::SPHERE:
                    boundingHalf = vec3(desc.params.x);
                    break;
                case IRRender::ShapeType::CYLINDER:
                case IRRender::ShapeType::CONE:
                    boundingHalf = vec3(
                        desc.params.x, desc.params.x,
                        desc.params.z * 0.5f);
                    break;
                case IRRender::ShapeType::ELLIPSOID:
                    boundingHalf = vec3(desc.params) * 0.5f;
                    break;
                case IRRender::ShapeType::TORUS: {
                    float xyR = desc.params.x + desc.params.y;
                    boundingHalf = vec3(xyR, xyR, desc.params.y);
                    break;
                }
                case IRRender::ShapeType::CURVED_PANEL: {
                    vec3 hs = vec3(desc.params) * 0.5f;
                    hs.z += std::abs(desc.params.w) * hs.x;
                    boundingHalf = hs;
                    break;
                }
                default:
                    boundingHalf = vec3(desc.params) * 0.5f;
                    break;
            }
            ivec2 originIso = IRMath::pos3DtoPos2DIso(origin);
            ivec2 isoHalfExtent = ivec2(IRMath::shapeIsoHalfExtent(boundingHalf * 2.0f));

            ivec2 isoMin = (originIso - isoHalfExtent) * sub - ivec2(2);
            ivec2 isoMax = (originIso + isoHalfExtent) * sub + ivec2(2);
            ivec2 isoSize = isoMax - isoMin;

            const int tilesX = IRMath::divCeil(std::max(isoSize.x, 1), kShapeTileSize);
            const int tilesY = IRMath::divCeil(std::max(isoSize.y, 1), kShapeTileSize);

            for (int ty = 0; ty < tilesY; ++ty) {
                for (int tx = 0; tx < tilesX; ++tx) {
                    if (static_cast<int>(tiles.size()) >= kMaxShapeTileDescriptors) {
                        goto upload;
                    }
                    ShapeTileDescriptor tile{};
                    tile.shapeIndex = i;
                    tile.tileIsoOrigin =
                        isoMin + ivec2(tx * kShapeTileSize, ty * kShapeTileSize);
                    tiles.push_back(tile);
                }
            }
        }

    upload:
        const int tileCount = static_cast<int>(tiles.size());
        if (tileCount == 0) {
            return 0;
        }
        tileDescBuf->subData(
            0, tileCount * sizeof(ShapeTileDescriptor), tiles.data());
        return tileCount;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SHAPES_TO_TRIXEL_H */
