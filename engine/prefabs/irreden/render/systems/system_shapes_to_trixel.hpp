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
#include <irreden/render/camera.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

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
    using CanvasId = IREntity::EntityId;

    struct Params {
        ShaderProgram *shapesProgram_ = nullptr;
        Buffer *shapeDescBuf_ = nullptr;
        Buffer *shapesFrameDataBuf_ = nullptr;
        Buffer *shapeTileDescBuf_ = nullptr;
        GPUShapesFrameData frameData_{};
        std::unordered_map<CanvasId, std::vector<GPUShapeDescriptor>> gpuShapesByCanvas_;
        std::optional<IsoBounds2D> cullBounds_;
        // Camera Z-yaw snapshotted at beginTick so the cull pass and the
        // per-tile dispatch share byte-identical values even if a script
        // mutates yaw mid-frame. visualYaw_ is split via computeYawSplit
        // into rasterYaw_ (cardinal multiple of pi/2) + residualYaw_; the
        // shapes shader rasterizes at rasterYaw_ so its output aligns with
        // the voxel pool's cardinal-snap raster, and yawCos_/yawSin_ are
        // the cos/sin of rasterYaw_ (always exact ±1 / 0). yawZero_ is
        // the rasterYaw==0 fast path; it stays true for visualYaw inside
        // (-pi/4, +pi/4) where rasterYaw rounds to 0. residualYaw_ is
        // mirrored into the UBO for shaders downstream of this pass; the
        // CPU side of this system does not consume it.
        float visualYaw_ = 0.0f;
        float rasterYaw_ = 0.0f;
        float residualYaw_ = 0.0f;
        float yawCos_ = 1.0f;
        float yawSin_ = 0.0f;
        bool yawZero_ = true;
    };

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ShapesToTrixelProgram",
            std::vector{ShaderStage{IRRender::kFileCompShapesToTrixel, ShaderType::COMPUTE}}
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

        auto paramsOwner = std::make_unique<Params>();
        Params *p = paramsOwner.get();
        p->shapesProgram_ = IRRender::getNamedResource<ShaderProgram>("ShapesToTrixelProgram");
        p->shapeDescBuf_ = IRRender::getNamedResource<Buffer>("ShapeDescriptorBuffer");
        p->shapesFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer");
        p->shapeTileDescBuf_ = IRRender::getNamedResource<Buffer>("ShapeTileDescriptorBuffer");

        SystemId systemId = createSystem<C_ShapeDescriptor, C_PositionGlobal3D>(
            "ShapesToTrixel",
            [p](IREntity::EntityId &entityId,
                const C_ShapeDescriptor &shape,
                const C_PositionGlobal3D &pos) {
                if (p->cullBounds_.has_value()) {
                    vec3 viewPos = pos.pos_;
                    vec3 sizeForExtent = vec3(shape.params_);
                    if (!p->yawZero_) {
                        viewPos = vec3(
                            p->yawCos_ * pos.pos_.x + p->yawSin_ * pos.pos_.y,
                           -p->yawSin_ * pos.pos_.x + p->yawCos_ * pos.pos_.y,
                            pos.pos_.z);
                        const float absC = IRMath::abs(p->yawCos_);
                        const float absS = IRMath::abs(p->yawSin_);
                        sizeForExtent = vec3(
                            sizeForExtent.x * absC + sizeForExtent.y * absS,
                            sizeForExtent.x * absS + sizeForExtent.y * absC,
                            sizeForExtent.z);
                    }
                    vec2 shapeIsoPosition = IRMath::pos3DtoPos2DIso(viewPos);
                    vec2 shapeIsoHalfExtent = IRMath::shapeIsoHalfExtent(sizeForExtent);
                    if (shapeIsoPosition.x + shapeIsoHalfExtent.x < p->cullBounds_->min_.x ||
                        shapeIsoPosition.x - shapeIsoHalfExtent.x > p->cullBounds_->max_.x ||
                        shapeIsoPosition.y + shapeIsoHalfExtent.y < p->cullBounds_->min_.y ||
                        shapeIsoPosition.y - shapeIsoHalfExtent.y > p->cullBounds_->max_.y) {
                        return;
                    }
                }

                CanvasId canvas = shape.canvasEntity_;
                if (canvas == IREntity::kNullEntity) {
                    canvas = IRRender::getActiveCanvasEntity();
                }

                auto &bucket = p->gpuShapesByCanvas_[canvas];
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
            [p]() {
                p->gpuShapesByCanvas_.clear();

                // Snapshot camera yaw once for the whole tick so the cull
                // pass and the per-tile dispatch share the same value, even
                // if a script mutates yaw mid-frame. The shape SDF
                // rasterizes at rasterYaw (cardinal-snap) so it lines up
                // trixel-for-trixel with the voxel pool's cardinal-snap
                // raster (T-055); the screen-space residual rotate pass
                // (T-058) handles the leftover residualYaw on screen.
                p->visualYaw_ = IRPrefab::Camera::getYaw();
                const auto [rasterYaw, residualYaw] =
                    IRPrefab::Camera::computeYawSplit(p->visualYaw_);
                p->rasterYaw_ = rasterYaw;
                p->residualYaw_ = residualYaw;
                static constexpr float kCardinalCos[4] = {1.f, 0.f, -1.f, 0.f};
                static constexpr float kCardinalSin[4] = {0.f, 1.f, 0.f, -1.f};
                constexpr float kHalfPi = 1.5707963267948966f;
                const int q = IRMath::round(rasterYaw / kHalfPi);
                const int cardinalIdx = ((q % 4) + 4) % 4;
                p->yawCos_ = kCardinalCos[cardinalIdx];
                p->yawSin_ = kCardinalSin[cardinalIdx];
                p->yawZero_ = (p->rasterYaw_ == 0.0f);

                IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
                auto texOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvas);
                if (texOpt.has_value()) {
                    IRRender::updateCullViewport(
                        IRRender::getCameraPosition2DIso(),
                        IRRender::getCameraZoom(),
                        texOpt.value()->size_
                    );
                    constexpr int kMargin = 4;
                    p->cullBounds_ = IRRender::getCullViewport().isoViewport(kMargin);
                } else {
                    p->cullBounds_.reset();
                }
            },
            [p]() {
                IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
                const float visualYaw = p->visualYaw_;
                const float rasterYaw = p->rasterYaw_;
                const float residualYaw = p->residualYaw_;

                for (auto &[canvasId, gpuShapes] : p->gpuShapesByCanvas_) {
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
                        p->frameData_.cameraTrixelOffset = IRRender::getCameraPosition2DIso();
                    } else {
                        canvasTextures.clear();
                        vec3 entityPos = vec3(gpuShapes[0].worldPosition);
                        p->frameData_.cameraTrixelOffset = -pos3DtoPos2DIso(entityPos);
                    }
                    p->frameData_.trixelCanvasOffsetZ1 =
                        IRMath::trixelOriginOffsetZ1(canvasTextures.size_);
                    p->frameData_.canvasSize = canvasTextures.size_;
                    p->frameData_.shapeCount = static_cast<int>(gpuShapes.size());
                    const auto renderMode = IRRender::getSubdivisionMode();
                    const int effectiveSub = IRRender::getVoxelRenderEffectiveSubdivisions();
                    p->frameData_.voxelRenderOptions =
                        ivec2(static_cast<int>(renderMode), effectiveSub);
                    if (p->cullBounds_.has_value() && canvasId == mainCanvas) {
                        p->frameData_.cullIsoMin = ivec2(IRMath::floor(p->cullBounds_->min_));
                        p->frameData_.cullIsoMax = ivec2(IRMath::ceil(p->cullBounds_->max_));
                    } else {
                        p->frameData_.cullIsoMin = ivec2(-999999);
                        p->frameData_.cullIsoMax = ivec2(999999);
                    }
                    p->frameData_.visualYaw = visualYaw;
                    p->frameData_.rasterYaw = rasterYaw;
                    p->frameData_.residualYaw = residualYaw;

                    p->shapeDescBuf_->subData(
                        0,
                        gpuShapes.size() * sizeof(GPUShapeDescriptor),
                        gpuShapes.data()
                    );

                    // Tile bounds are computed at rasterYaw — same rotation
                    // the shader uses to rasterize each shape — so the
                    // per-tile iso footprint matches the SDF surface that
                    // pixel ends up writing.
                    const int tileCount = buildAndUploadTileDescriptors(
                        gpuShapes,
                        p->shapeTileDescBuf_,
                        effectiveSub,
                        renderMode,
                        rasterYaw,
                        p->yawCos_,
                        p->yawSin_
                    );
                    if (tileCount == 0) {
                        continue;
                    }

                    p->shapesProgram_->use();
                    p->shapeDescBuf_->bindBase(
                        BufferTarget::SHADER_STORAGE,
                        kBufferIndex_ShapeDescriptors
                    );
                    canvasTextures.getTextureDistances()
                        ->bindAsImage(1, TextureAccess::READ_WRITE, TextureFormat::R32I);

                    p->shapesFrameDataBuf_->bindBase(
                        BufferTarget::UNIFORM,
                        kBufferIndex_ShapesFrameData
                    );

                    // Pass 0: depth via imageAtomicMin
                    p->frameData_.passIndex = 0;
                    p->shapesFrameDataBuf_->subData(
                        0, sizeof(GPUShapesFrameData), &p->frameData_);

                    IRRender::device()
                        ->dispatchCompute(static_cast<std::uint32_t>(tileCount), 1, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                    // Pass 1: color + entity ID where depth matches
                    canvasTextures.getTextureColors()
                        ->bindAsImage(0, TextureAccess::WRITE_ONLY, TextureFormat::RGBA16F);
                    canvasTextures.getTextureEntityIds()
                        ->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);

                    p->frameData_.passIndex = 1;
                    p->shapesFrameDataBuf_->subData(
                        0, sizeof(GPUShapesFrameData), &p->frameData_);

                    IRRender::device()
                        ->dispatchCompute(static_cast<std::uint32_t>(tileCount), 1, 1);
                    IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

                    auto &timing = IRRender::gpuStageTiming();
                    timing.visibleShapeCount_ = static_cast<std::uint32_t>(gpuShapes.size());
                    timing.shapeGroupsZ_ = 0;
                }
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        // Per-system bracket covers both pass 0 (depth) and pass 1 (color/id);
        // the formerly-separate shapePass0 slot stays at 0.0f for API stability.
        IRRender::tagGpuStage(systemId, "shapePass1");
        return systemId;
    }

    // Builds a per-tile descriptor stream for every visible shape, uploads
    // it to the SSBO, and returns the total tile count. The batched compute
    // dispatch then runs once per pass, with gl_WorkGroupID.x indexing this
    // buffer — one workgroup per 8×8 pixel tile.
    //
    // @p rasterYaw is the cardinal-snap Z-yaw (radians, exact multiple of
    // pi/2). Each shape's worldPos is rotated by R_z(-rasterYaw) before iso
    // projection, and its XY bounding half-extent is grown to cover the
    // rotated AABB. At rasterYaw=0 both operations are identity and the
    // tile coverage is unchanged. The shader rasterizes at rasterYaw too,
    // so the iso footprint of each tile matches the pixels the shader
    // writes; residualYaw is handled downstream in screen space (T-058).
    // @p yawCos/@p yawSin are cos/sin of rasterYaw, snapshotted at frame
    // start so the cull pass and the per-tile dispatch see byte-identical
    // values even if a script mutates yaw mid-frame.
    static int buildAndUploadTileDescriptors(
        const std::vector<GPUShapeDescriptor> &gpuShapes,
        Buffer *tileDescBuf,
        int effectiveSubdivisions,
        IRRender::SubdivisionMode renderMode,
        float rasterYaw,
        float yawCos,
        float yawSin
    ) {
        static thread_local std::vector<ShapeTileDescriptor> tiles;
        tiles.clear();

        const int sub = (renderMode != IRRender::SubdivisionMode::NONE) ? effectiveSubdivisions : 1;
        const bool yawZero = (rasterYaw == 0.0f);
        const float absYawC = IRMath::abs(yawCos);
        const float absYawS = IRMath::abs(yawSin);

        for (int i = 0; i < static_cast<int>(gpuShapes.size()); ++i) {
            const auto &desc = gpuShapes[i];
            vec3 worldPos = vec3(desc.worldPosition);
            vec3 viewPos = yawZero
                ? worldPos
                : vec3( yawCos * worldPos.x + yawSin * worldPos.y,
                       -yawSin * worldPos.x + yawCos * worldPos.y,
                        worldPos.z);
            ivec3 origin = IRMath::roundVec3HalfUp(viewPos);

            // Canonical bounding half-extent lives in IRMath::SDF (shared with
            // the lighting / shadow pipeline). Renderer + shadow shader stay
            // in lockstep on what each shape's footprint is.
            vec3 boundingHalf = IRMath::SDF::boundingHalf(
                static_cast<IRMath::SDF::ShapeType>(desc.shapeType),
                desc.params
            );
            // Z-yaw expands the XY AABB by |c|·hX + |s|·hY (and symmetric).
            // Grow the iso footprint conservatively so every visible pixel
            // of the rotated shape is inside at least one dispatched tile.
            if (!yawZero) {
                boundingHalf = vec3(
                    boundingHalf.x * absYawC + boundingHalf.y * absYawS,
                    boundingHalf.x * absYawS + boundingHalf.y * absYawC,
                    boundingHalf.z);
            }
            ivec2 originIso = IRMath::pos3DtoPos2DIso(origin);
            ivec2 isoHalfExtent = ivec2(IRMath::shapeIsoHalfExtent(boundingHalf * 2.0f));

            ivec2 isoMin = (originIso - isoHalfExtent) * sub - ivec2(2);
            ivec2 isoMax = (originIso + isoHalfExtent) * sub + ivec2(2);
            ivec2 isoSize = isoMax - isoMin;

            const int tilesX = IRMath::divCeil(IRMath::max(isoSize.x, 1), kShapeTileSize);
            const int tilesY = IRMath::divCeil(IRMath::max(isoSize.y, 1), kShapeTileSize);

            for (int ty = 0; ty < tilesY; ++ty) {
                for (int tx = 0; tx < tilesX; ++tx) {
                    if (static_cast<int>(tiles.size()) >= kMaxShapeTileDescriptors) {
                        goto upload;
                    }
                    ShapeTileDescriptor tile{};
                    tile.shapeIndex = i;
                    tile.tileIsoOrigin = isoMin + ivec2(tx * kShapeTileSize, ty * kShapeTileSize);
                    tiles.push_back(tile);
                }
            }
        }

    upload:
        const int tileCount = static_cast<int>(tiles.size());
        if (tileCount == 0) {
            return 0;
        }
        tileDescBuf->subData(0, tileCount * sizeof(ShapeTileDescriptor), tiles.data());
        return tileCount;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SHAPES_TO_TRIXEL_H */
