#ifndef SYSTEM_SHAPES_TO_TRIXEL_H
#define SYSTEM_SHAPES_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_constants.hpp>

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_active_lod_level.hpp>
#include <irreden/render/canvas_clear.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/lod_utils.hpp>
#include <irreden/render/sun_shadow_constants.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>

#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

#include <cstring>
#include <limits>
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
static_assert(
    std::uint64_t(kMaxShapeTileDescriptors) * kShapeTileSize * kShapeTileSize * 6 <
        std::numeric_limits<std::uint32_t>::max(),
    "Shape sample owners must fit below the empty uint32 sentinel"
);

template <> struct System<SHAPES_TO_TRIXEL> {
    using CanvasId = IREntity::EntityId;

    ShaderProgram *shapesProgram_ = nullptr;
    Buffer *shapeDescBuf_ = nullptr;
    Buffer *shapesFrameDataBuf_ = nullptr;
    Buffer *shapeTileDescBuf_ = nullptr;
    GPUShapesFrameData frameData_{};
    ResourceId winnerBufferId_ = 0;
    Buffer *winnerBuffer_ = nullptr;
    std::size_t winnerCapacityBytes_ = 0;
    Buffer *animationParamsBuf_ = nullptr;

    void prepareWinnerBuffer(ivec2 size) {
        const auto bytes = std::size_t(size.x) * std::size_t(size.y) * sizeof(std::uint32_t);
        if (bytes > winnerCapacityBytes_) {
            if (winnerBuffer_)
                IRRender::destroyResource<Buffer>(winnerBufferId_);
            const auto resource =
                IRRender::createResource<Buffer>(nullptr, bytes, BUFFER_STORAGE_DYNAMIC);
            winnerBufferId_ = resource.first;
            winnerBuffer_ = resource.second;
            winnerCapacityBytes_ = bytes;
        }
        // Buffer clears must observe the previous canvas/frame's shader writes on OpenGL.
        IRRender::device()->memoryBarrier(BarrierType::ALL);
        IRRender::device()->fillBuffer(winnerBuffer_, bytes, 0xFF);
        // Shapes do not consume animation parameters; restore this borrowed slot after dispatch.
        winnerBuffer_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_AnimationParams);
    }

    std::unordered_map<CanvasId, std::vector<GPUShapeDescriptor>> gpuShapesByCanvas_;
    // Owner translation per entity canvas, snapshotted at beginTick. An entity
    // canvas rasters in its owner's model frame and the composite places the
    // whole canvas at the owner's yawed iso position, so a shape targeting one
    // is positioned by its offset from the owner, never by its world position.
    std::unordered_map<CanvasId, vec3> entityCanvasOrigins_;
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
    // Cardinal snap index of rasterYaw_, reused by the cull-pass view rotation
    // (IRMath::rotateCardinalZ) so it shares the rasterizer's world->view
    // permutation instead of open-coding the same cos/sin product.
    IRMath::CardinalIndex cardinalIndex_ = IRMath::CardinalIndex::k0;
    bool yawZero_ = true;
    // Continuous-yaw snapshot for the smooth camera Z-yaw SDF path.
    // smoothYaw_ is true inside a residual bracket (residualYaw != 0); the cull
    // (any canvas — widening is conservative) and the main-canvas tile footprint
    // then project at the full visualYaw and grow by the continuous |cos|,|sin|
    // (up to the sqrt(2) footprint at +/-45deg) so off-center shapes inside the
    // rotated footprint are not dropped from the cardinal-snapped viewport.
    // yawCosVisual_/yawSinVisual_ are cos/sin(visualYaw_).
    bool smoothYaw_ = false;
    float yawCosVisual_ = 1.0f;
    float yawSinVisual_ = 0.0f;
    // LOD tier snapshotted at beginTick from the C_ActiveLodLevel singleton
    // (written by LOD_UPDATE in UPDATE phase). Per-entity tick skips shapes
    // whose [lodMax_ .. lodMin_] band does not contain activeLod_ — they want
    // more (or less) detail than this frame's zoom provides. Defaults to LOD_4
    // (no culling) so creations that don't register LOD_UPDATE keep their
    // pre-LOD behavior.
    IRRender::LodLevel activeLod_ = IRRender::LodLevel::LOD_4;

    void tick(
        IREntity::EntityId entityId, const C_ShapeDescriptor &shape, const C_WorldTransform &xform
    ) {
        if (IRRender::shouldSkipAtLod(shape.lodMin_, shape.lodMax_, activeLod_)) {
            return;
        }
        if (cullBounds_.has_value()) {
            vec3 sizeForExtent = vec3(shape.params_);
            const bool hasRotation = IRMath::abs(xform.rotation_.w) < 0.9999f;
            if (hasRotation) {
                vec3 ax = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(sizeForExtent.x, 0, 0), xform.rotation_)
                );
                vec3 ay = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(0, sizeForExtent.y, 0), xform.rotation_)
                );
                vec3 az = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(0, 0, sizeForExtent.z), xform.rotation_)
                );
                sizeForExtent = ax + ay + az;
            }
            vec2 shapeIsoPosition;
            if (smoothYaw_) {
                // Continuous-yaw cull: project the center at the full visualYaw
                // and grow the iso half-extent by the continuous |c|,|s| (up to
                // the sqrt(2) footprint at +/-45deg) so off-center shapes inside
                // the rotated footprint survive the cardinal-snapped viewport.
                sizeForExtent =
                    IRMath::yawGrownIsoHalfExtent(sizeForExtent, yawCosVisual_, yawSinVisual_);
                shapeIsoPosition = IRMath::pos3DtoPos2DIsoYawed(xform.translation_, visualYaw_);
            } else {
                vec3 viewPos = xform.translation_;
                if (!yawZero_) {
                    viewPos = IRMath::rotateCardinalZ(xform.translation_, cardinalIndex_);
                    sizeForExtent = IRMath::yawGrownIsoHalfExtent(sizeForExtent, yawCos_, yawSin_);
                }
                shapeIsoPosition = IRMath::pos3DtoPos2DIso(viewPos);
            }
            vec2 shapeIsoHalfExtent = IRMath::shapeIsoHalfExtent(sizeForExtent);
            if (shapeIsoPosition.x + shapeIsoHalfExtent.x < cullBounds_->min_.x ||
                shapeIsoPosition.x - shapeIsoHalfExtent.x > cullBounds_->max_.x ||
                shapeIsoPosition.y + shapeIsoHalfExtent.y < cullBounds_->min_.y ||
                shapeIsoPosition.y - shapeIsoHalfExtent.y > cullBounds_->max_.y) {
                return;
            }
        }

        CanvasId canvas = shape.canvasEntity_;
        if (canvas == IREntity::kNullEntity) {
            canvas = IRRender::getActiveCanvasEntity();
        }

        auto &bucket = gpuShapesByCanvas_[canvas];
        if (static_cast<int>(bucket.size()) >= kMaxShapeDescriptors) {
            return;
        }
        GPUShapeDescriptor desc{};
        desc.worldPosition = vec4(xform.translation_, 1.0f);
        if (auto origin = entityCanvasOrigins_.find(canvas); origin != entityCanvasOrigins_.end()) {
            desc.worldPosition = vec4(xform.translation_ - origin->second, 1.0f);
        }
        desc.params = shape.params_;
        desc.rotation = xform.rotation_;
        desc.shapeType = static_cast<std::uint32_t>(shape.shapeType_);
        desc.color = shape.color_.toPackedRGBA();
        desc.entityId = entityId;
        desc.jointIndex = 0;
        desc.flags = shape.flags_;
        desc.lodLevel = IRRender::toUnderlying(shape.lodMin_);
        bucket.push_back(desc);
    }

    void beginTick() {
        const auto bakeSystem = findSystem(BAKE_SUN_SHADOW_MAP);
        if (bakeSystem != kNullSystemId) {
            auto *baker = getSystemParams<System<BAKE_SUN_SHADOW_MAP>>(bakeSystem);
            baker->beginVoxelFaceCoverage();
        }
        gpuShapesByCanvas_.clear();

        // Dense per-archetype-column scan of the canvas owners (a handful of
        // entities), never a per-shape getComponent.
        entityCanvasOrigins_.clear();
        for (IREntity::ArchetypeNode *node : IREntity::queryArchetypeNodesSimple(
                 IREntity::getArchetype<C_EntityCanvas, C_WorldTransform>()
             )) {
            const std::vector<C_EntityCanvas> &canvases =
                IREntity::getComponentData<C_EntityCanvas>(node);
            const std::vector<C_WorldTransform> &transforms =
                IREntity::getComponentData<C_WorldTransform>(node);
            for (int i = 0; i < node->length_; ++i) {
                if (canvases[i].canvasEntity_ != IREntity::kNullEntity) {
                    entityCanvasOrigins_[canvases[i].canvasEntity_] = transforms[i].translation_;
                }
            }
        }

        // Snapshot the active LOD tier from the singleton written by
        // LOD_UPDATE in the UPDATE phase. singletonOrNull returns nullptr
        // when no creation has registered LOD_UPDATE — in that case the
        // default LOD_4 leaves every shape visible.
        if (auto *lod = IREntity::singletonOrNull<C_ActiveLodLevel>()) {
            activeLod_ = lod->current_;
        } else {
            activeLod_ = IRRender::LodLevel::LOD_4;
        }

        // Snapshot camera yaw once for the whole tick so the cull
        // pass and the per-tile dispatch share the same value, even
        // if a script mutates yaw mid-frame. The shape SDF
        // rasterizes at rasterYaw (cardinal-snap) so it lines up
        // trixel-for-trixel with the voxel pool's cardinal-snap
        // raster; the screen-space residual rotate pass
        // handles the leftover residualYaw on screen.
        visualYaw_ = IRPrefab::Camera::getYaw();
        const auto [rasterYaw, residualYaw] = IRPrefab::Camera::computeYawSplit(visualYaw_);
        rasterYaw_ = rasterYaw;
        residualYaw_ = residualYaw;
        cardinalIndex_ = IRMath::rasterYawCardinalIndex(rasterYaw);
        const vec2 cardinalCosSin = IRMath::cardinalYawCosSin(cardinalIndex_);
        yawCos_ = cardinalCosSin.x;
        yawSin_ = cardinalCosSin.y;
        yawZero_ = (rasterYaw_ == 0.0f);
        // Smooth camera Z-yaw: continuous-yaw cull/footprint inside a
        // residual bracket. cos/sin of the full visualYaw (not the cardinal
        // table) so the rotated footprint grows to the true sqrt(2) extent.
        smoothYaw_ = (residualYaw_ != 0.0f);
        yawCosVisual_ = IRMath::cos(visualYaw_);
        yawSinVisual_ = IRMath::sin(visualYaw_);

        IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
        auto texOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvas);
        if (texOpt.has_value()) {
            IRRender::updateCullViewport(
                IRRender::getEffectiveCameraIso(),
                IRRender::getCameraZoom(),
                texOpt.value()->size_
            );
            constexpr int kMargin = 4;
            // Widen to the shadow-feeder AABB (visible ∪ swept along
            // -sunDir by kSunShadowMaxDistance) so off-screen SDF
            // casters still write distances into trixelDistances and
            // feed BAKE_SUN_SHADOW_MAP. The same widened bounds are
            // uploaded as cullIsoMin/Max in endTick so per-tile
            // shaders never clip a shadow-feeder pixel. Sun-direction
            // resolution is gated on the shadow flag so a disabled-
            // shadow creation skips the C_LightSource archetype scan.
            const auto shadowFeeder = IRPrefab::SunShadow::frameShadowFeederParams();
            cullBounds_ = IRPrefab::SunShadow::shadowFeederCullViewport(kMargin, shadowFeeder);
        } else {
            cullBounds_.reset();
        }
    }

    void endTick() {
        IREntity::EntityId mainCanvas = IRRender::getActiveCanvasEntity();
        const float visualYaw = visualYaw_;
        const float rasterYaw = rasterYaw_;
        const float residualYaw = residualYaw_;

        for (auto &[canvasId, gpuShapes] : gpuShapesByCanvas_) {
            if (gpuShapes.empty()) {
                continue;
            }

            auto texturesOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(canvasId);
            if (!texturesOpt.has_value()) {
                continue;
            }
            auto &canvasTextures = *texturesOpt.value();

            const bool entityCanvas = entityCanvasOrigins_.contains(canvasId);
            if (canvasId == mainCanvas) {
                frameData_.cameraTrixelOffset = IRRender::getEffectiveCameraIso();
            } else if (entityCanvas) {
                // The owner's model frame: no camera term (the composite places
                // the canvas), and the voxel raster's color, depth and ids are
                // kept — VOXEL_TO_TRIXEL_STAGE_1 cleared this canvas when it
                // rastered a pool into it this frame (renderedSubdivisions_ > 0),
                // so this pass must run after it. A shape-only canvas is reset
                // here through the same sentinel + Metal scratch mirror.
                frameData_.cameraTrixelOffset = vec2(0.0f);
                if (canvasTextures.renderedSubdivisions_ == 0) {
                    clearCanvasAndDistances(canvasId, canvasTextures);
                }
            } else {
                clearCanvasAndDistances(canvasId, canvasTextures);
                vec3 entityPos = vec3(gpuShapes[0].worldPosition);
                frameData_.cameraTrixelOffset = -pos3DtoPos2DIso(entityPos);
            }
            frameData_.trixelCanvasOffsetZ1 = IRMath::trixelOriginOffsetZ1(canvasTextures.size_);
            frameData_.canvasSize = canvasTextures.size_;
            frameData_.shapeCount = static_cast<int>(gpuShapes.size());
            const auto renderMode = IRRender::getSubdivisionMode();
            // An entity canvas rasters at the density its voxel producer stored
            // and the composite divides out (renderedSubdivisions_, capped by the
            // canvas footprint); the global effective subdivision would scale
            // the shape and its offset by the cap ratio.
            const int effectiveSub = entityCanvas
                                         ? IRMath::max(canvasTextures.renderedSubdivisions_, 1)
                                         : IRRender::getVoxelRenderEffectiveSubdivisions();
            frameData_.voxelRenderOptions = ivec2(static_cast<int>(renderMode), effectiveSub);
            if (cullBounds_.has_value() && canvasId == mainCanvas) {
                frameData_.cullIsoMin = ivec2(IRMath::floor(cullBounds_->min_));
                frameData_.cullIsoMax = ivec2(IRMath::ceil(cullBounds_->max_));
            } else {
                frameData_.cullIsoMin = ivec2(-999999);
                frameData_.cullIsoMax = ivec2(999999);
            }
            frameData_.visualYaw = visualYaw;
            frameData_.rasterYaw = rasterYaw;
            frameData_.residualYaw = residualYaw;
            // Smooth camera Z-yaw: the continuous-yaw SDF path runs on the
            // rotating main world canvas and on entity canvases, whose content
            // is camera-composed (the composite applies no rotation, so the
            // owner-relative offset must be projected at the full visual yaw,
            // like the composite projects the owner). Other canvases keep the
            // cardinal rasterYaw + faceDeform path.
            const bool canvasSmoothYaw = smoothYaw_ && (canvasId == mainCanvas || entityCanvas);
            frameData_.smoothYawEnabled = canvasSmoothYaw ? 1 : 0;
            frameData_.latticeShapes = entityCanvas ? 1 : 0;
            // Residual yaw is folded into faceDeform per-face for the shapes
            // shader. Identity at residualYaw == 0.
            const mat2 fdX = IRMath::faceDeformationMatrix(IRMath::kXFace, residualYaw);
            const mat2 fdY = IRMath::faceDeformationMatrix(IRMath::kYFace, residualYaw);
            const mat2 fdZ = IRMath::faceDeformationMatrix(IRMath::kZFace, residualYaw);
            frameData_.faceDeform[IRMath::kXFace] = vec4(fdX[0], fdX[1]);
            frameData_.faceDeform[IRMath::kYFace] = vec4(fdY[0], fdY[1]);
            frameData_.faceDeform[IRMath::kZFace] = vec4(fdZ[0], fdZ[1]);

            if (entityCanvas) {
                // The voxel raster stores a revoxelized canvas's cells at
                // rounded lattice positions and the composite places the whole
                // canvas at the owner plus the half-cell phase
                // (renderedCellOffset_, view-local), so every displayed cell
                // sits at integer + phase. A shape rastered into the canvas
                // snaps to that same lattice: its owner offset loses the phase
                // (rotated into the world frame the offset is projected in),
                // the shader's per-axis rounding lands it on a lattice cell,
                // and the composite's phase puts the cell back where the voxels
                // are. Shifting the raster by the phase's iso projection
                // instead keeps the centroid but breaks the trixel parity the
                // local-triangle gather reads, so the shape's hexagons come out
                // as bow ties. Zero on a plain DETACHED or shape-only canvas.
                const vec3 phase = canvasTextures.renderedCellOffset_;
                const vec4 worldPhase(
                    yawCosVisual_ * phase.x - yawSinVisual_ * phase.y,
                    yawSinVisual_ * phase.x + yawCosVisual_ * phase.y,
                    phase.z,
                    0.0f
                );
                for (GPUShapeDescriptor &shape : gpuShapes) {
                    shape.worldPosition -= worldPhase;
                }
            }

            shapeDescBuf_
                ->subData(0, gpuShapes.size() * sizeof(GPUShapeDescriptor), gpuShapes.data());

            // Tile bounds are computed at rasterYaw — same rotation
            // the shader uses to rasterize each shape — so the
            // per-tile iso footprint matches the SDF surface that
            // pixel ends up writing.
            int gridX = 1;
            const int tileCount = buildAndUploadTileDescriptors(
                gpuShapes,
                shapeTileDescBuf_,
                effectiveSub,
                renderMode,
                rasterYaw,
                yawCos_,
                yawSin_,
                canvasSmoothYaw,
                visualYaw,
                yawCosVisual_,
                yawSinVisual_,
                entityCanvas,
                gridX
            );
            if (tileCount == 0) {
                continue;
            }
            const int gridY = IRMath::divCeil(tileCount, gridX);
            frameData_.tileGridX = gridX;

            prepareWinnerBuffer(canvasTextures.size_);
            shapesProgram_->use();
            shapeDescBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_ShapeDescriptors);
            canvasTextures.getTextureDistances()
                ->bindAsImage(1, TextureAccess::READ_WRITE, TextureFormat::R32I);
            // All three declared kernel slots are bound ahead of BOTH passes,
            // even though pass 0 reads only distances: Metal validates every
            // argument a kernel declares at each dispatch, and unit 0 still
            // holds an R32I distance texture from an earlier pass, so leaving
            // colors unbound for pass 0 aborts on a type mismatch under
            // `MTL_DEBUG_LAYER=1`. Colors is READ_WRITE (not WRITE_ONLY) so the
            // SHAPE_FLAG_GIZMO occluded-blend branch can imageLoad the existing
            // canvas color and blend the gizmo silhouette on top at reduced
            // alpha; non-gizmo writes are still pure stores.
            canvasTextures.getTextureColors()
                ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
            canvasTextures.getTextureEntityIds()
                ->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);

            shapesFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_ShapesFrameData);

            // Pass 0: depth via imageAtomicMin
            frameData_.passIndex = 0;
            shapesFrameDataBuf_->subData(0, sizeof(GPUShapesFrameData), &frameData_);

            IRRender::device()->dispatchCompute(
                static_cast<std::uint32_t>(gridX),
                static_cast<std::uint32_t>(gridY),
                1
            );
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

            frameData_.passIndex = 3;
            shapesFrameDataBuf_->subData(0, sizeof(GPUShapesFrameData), &frameData_);
            IRRender::device()->dispatchCompute(
                static_cast<std::uint32_t>(gridX),
                static_cast<std::uint32_t>(gridY),
                1
            );
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

            // Pass 1: color + entity ID from the elected depth winner. Colors is bound
            // READ_WRITE (not WRITE_ONLY) so the SHAPE_FLAG_GIZMO occluded-
            // blend branch in the shader can imageLoad the existing canvas
            // color and blend the gizmo silhouette on top at reduced alpha.
            // Non-gizmo writes are still pure stores; the read
            // capability is only exercised on the occluded gizmo path.
            canvasTextures.getTextureColors()
                ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
            canvasTextures.getTextureEntityIds()
                ->bindAsImage(2, TextureAccess::WRITE_ONLY, TextureFormat::RG32UI);

            frameData_.passIndex = 1;
            shapesFrameDataBuf_->subData(0, sizeof(GPUShapesFrameData), &frameData_);

            IRRender::device()->dispatchCompute(
                static_cast<std::uint32_t>(gridX),
                static_cast<std::uint32_t>(gridY),
                1
            );
            IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

            const auto shadow = IREntity::getComponentOptional<C_CanvasSunShadow>(canvasId);
            const auto behavior =
                IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(canvasId);
            if (shadow.has_value() && behavior.has_value() &&
                behavior.value()->useCameraPositionIso_) {
                const auto bakeSystem = findSystem(BAKE_SUN_SHADOW_MAP);
                if (bakeSystem != kNullSystemId) {
                    auto *baker = getSystemParams<System<BAKE_SUN_SHADOW_MAP>>(bakeSystem);
                    if (auto *casterDepth = baker->prepareAnalyticCasterDepth(frameData_)) {
                        baker->bakeAnalyticBoxes(
                            static_cast<int>(gpuShapes.size()),
                            renderMode == SubdivisionMode::NONE ? 1 : effectiveSub
                        );
                        shapesProgram_->use();
                        // Non-box analytic casters retain their separate depth input.
                        casterDepth->bindAsImage(1, TextureAccess::READ_WRITE, TextureFormat::R32I);
                        frameData_.passIndex = 2;
                        shapesFrameDataBuf_->subData(0, sizeof(GPUShapesFrameData), &frameData_);
                        shapesFrameDataBuf_->bindBase(
                            BufferTarget::UNIFORM,
                            kBufferIndex_ShapesFrameData
                        );
                        IRRender::device()->dispatchCompute(
                            static_cast<std::uint32_t>(gridX),
                            static_cast<std::uint32_t>(gridY),
                            1
                        );
                        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
                        IRRender::device()->resolveImageAtomicScratch(casterDepth);
                        baker->bakeAnalyticCasterDepth();
                        canvasTextures.getTextureColors()
                            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
                        canvasTextures.getTextureDistances()
                            ->bindAsImage(1, TextureAccess::READ_WRITE, TextureFormat::R32I);
                    }
                }
            }

            animationParamsBuf_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_AnimationParams
            );
            auto &timing = IRRender::gpuStageTiming();
            timing.visibleShapeCount_ = static_cast<std::uint32_t>(gpuShapes.size());
            timing.shapeGroupsZ_ = 0;
        }
    }

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
        // Binding 21 mirrors c_shapes_to_trixel.glsl's declared jointData[];
        // shapes currently leave jointIndex at 0. It is not used by the voxel
        // skinning path (which uses EntityTransformBuffer at binding 18 and
        // per-voxel bone-slot indices at binding 17 via seedVoxelBoneSlots).
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

        SystemId systemId =
            registerSystem<SHAPES_TO_TRIXEL, C_ShapeDescriptor, C_WorldTransform>("ShapesToTrixel");
        auto *p = getSystemParams<System<SHAPES_TO_TRIXEL>>(systemId);
        p->shapesProgram_ = IRRender::getNamedResource<ShaderProgram>("ShapesToTrixelProgram");
        p->shapeDescBuf_ = IRRender::getNamedResource<Buffer>("ShapeDescriptorBuffer");
        p->shapesFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ShapesFrameDataBuffer");
        p->shapeTileDescBuf_ = IRRender::getNamedResource<Buffer>("ShapeTileDescriptorBuffer");
        p->animationParamsBuf_ = IRRender::getNamedResource<Buffer>("AnimationParamsBuffer");
        // Per-system bracket covers depth, owner election and color/id;
        // shapePass0 stays at 0.0f for API stability.
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
    // writes; residualYaw is handled downstream in screen space.
    // @p yawCos/@p yawSin are cos/sin of rasterYaw, snapshotted at frame
    // start so the cull pass and the per-tile dispatch see byte-identical
    // values even if a script mutates yaw mid-frame.
    //
    // Smooth camera Z-yaw: when @p smoothYaw is set the tile footprint
    // is centered on the FULL-visualYaw iso projection (matching the shader's
    // continuous originIsoScaled) and grown by the continuous |cos|,|sin| up to
    // the sqrt(2) extent. @p visualYaw / @p yawCosVisual / @p yawSinVisual are
    // the continuous angle and its cos/sin. At @p smoothYaw=false the cardinal
    // rasterYaw footprint is used unchanged (byte-identical).
    static int buildAndUploadTileDescriptors(
        const std::vector<GPUShapeDescriptor> &gpuShapes,
        Buffer *tileDescBuf,
        int effectiveSubdivisions,
        IRRender::SubdivisionMode renderMode,
        float rasterYaw,
        float yawCos,
        float yawSin,
        bool smoothYaw,
        float visualYaw,
        float yawCosVisual,
        float yawSinVisual,
        bool latticeShapes,
        int &gridXOut
    ) {
        static thread_local std::vector<ShapeTileDescriptor> tiles;
        tiles.clear();

        const int sub = (renderMode != IRRender::SubdivisionMode::NONE) ? effectiveSubdivisions : 1;
        const bool yawZero = (rasterYaw == 0.0f);
        const IRMath::CardinalIndex cardinalIndex = IRMath::rasterYawCardinalIndex(rasterYaw);

        for (int i = 0; i < static_cast<int>(gpuShapes.size()); ++i) {
            const auto &desc = gpuShapes[i];
            vec3 worldPos = vec3(desc.worldPosition);
            vec3 viewPos = IRMath::rotateCardinalZ(worldPos, cardinalIndex);
            ivec3 origin = IRMath::roundVec3HalfUp(viewPos);

            // Canonical bounding half-extent lives in IRMath::SDF (shared with
            // the lighting / shadow pipeline). Renderer + shadow shader stay
            // in lockstep on what each shape's footprint is.
            vec3 boundingHalf = IRMath::SDF::boundingHalf(
                static_cast<IRMath::SDF::ShapeType>(desc.shapeType),
                desc.params
            );
            const bool hasRotation = IRMath::abs(desc.rotation.w) < 0.9999f;
            if (hasRotation) {
                vec3 ax = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(boundingHalf.x, 0, 0), desc.rotation)
                );
                vec3 ay = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(0, boundingHalf.y, 0), desc.rotation)
                );
                vec3 az = IRMath::abs(
                    IRMath::rotateVectorByQuat(vec3(0, 0, boundingHalf.z), desc.rotation)
                );
                boundingHalf = ax + ay + az;
            }
            // Z-yaw expands the XY AABB by |c|·hX + |s|·hY (and symmetric).
            // Grow the iso footprint conservatively so every visible pixel
            // of the rotated shape is inside at least one dispatched tile.
            ivec2 isoMin;
            ivec2 isoMax;
            if (smoothYaw) {
                // Continuous-yaw footprint: center on the full-visualYaw iso
                // projection (matches the shader's originIsoScaled) and grow by
                // the continuous |c|,|s| (sqrt(2) extent at +/-45deg). A lattice
                // shape at density 1 anchors on its snapped view cell instead,
                // the origin the kernel's lattice walk keys its parity on.
                boundingHalf =
                    IRMath::yawGrownIsoHalfExtent(boundingHalf, yawCosVisual, yawSinVisual);
                ivec2 originIsoScaled;
                if (latticeShapes && sub == 1) {
                    const vec3 viewPosYawed(
                        yawCosVisual * worldPos.x + yawSinVisual * worldPos.y,
                        -yawSinVisual * worldPos.x + yawCosVisual * worldPos.y,
                        worldPos.z
                    );
                    originIsoScaled =
                        IRMath::pos3DtoPos2DIso(IRMath::roundVec3HalfUp(viewPosYawed));
                } else {
                    const vec2 originIsoF =
                        IRMath::pos3DtoPos2DIsoYawed(worldPos * static_cast<float>(sub), visualYaw);
                    originIsoScaled =
                        ivec2(IRMath::roundHalfUp(originIsoF.x), IRMath::roundHalfUp(originIsoF.y));
                }
                const ivec2 isoHalfExtent =
                    ivec2(IRMath::shapeIsoHalfExtent(boundingHalf * 2.0f)) * sub;
                isoMin = originIsoScaled - isoHalfExtent - ivec2(2);
                isoMax = originIsoScaled + isoHalfExtent + ivec2(2);
            } else {
                if (!yawZero) {
                    boundingHalf = IRMath::yawGrownIsoHalfExtent(boundingHalf, yawCos, yawSin);
                }
                const ivec2 originIso = IRMath::pos3DtoPos2DIso(origin);
                const ivec2 isoHalfExtent = ivec2(IRMath::shapeIsoHalfExtent(boundingHalf * 2.0f));
                isoMin = (originIso - isoHalfExtent) * sub - ivec2(2);
                isoMax = (originIso + isoHalfExtent) * sub + ivec2(2);
            }
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
            gridXOut = 1;
            return 0;
        }
        const ivec2 grid = voxelDispatchGridForCount(tileCount);
        const int paddedCount = grid.x * grid.y;
        ShapeTileDescriptor sentinel{};
        sentinel.shapeIndex = -1;
        while (static_cast<int>(tiles.size()) < paddedCount) {
            tiles.push_back(sentinel);
        }
        tileDescBuf->subData(0, paddedCount * sizeof(ShapeTileDescriptor), tiles.data());
        gridXOut = grid.x;
        return tileCount;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SHAPES_TO_TRIXEL_H */
