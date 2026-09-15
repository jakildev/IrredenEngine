#ifndef SYSTEM_BAKE_SUN_SHADOW_MAP_H
#define SYSTEM_BAKE_SUN_SHADOW_MAP_H

// Bakes a sun-aligned 2D depth buffer from the iso canvas distance
// texture so COMPUTE_SUN_SHADOW resolves shadows with a single texel
// read per pixel.
//
// Pipeline: must run after the geometry stages (trixelDistances must be
// populated) and before COMPUTE_SUN_SHADOW (which reads the baked buffer).
// Owns the full FrameDataSun UBO upload — sun direction, basis, AABB,
// flags — so downstream consumers (COMPUTE_SUN_SHADOW, COMPUTE_VOXEL_AO,
// LIGHTING_TO_TRIXEL) read coherent state every frame.
//
// Slot 28 is shared with the occupancy grid (read by AO + light-volume).
// Both producers rebind the slot before their own dispatch, so the alias
// is safe.

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

// World sun-shadow cast for opt-in world-placed detached re-voxelize solids.
// The resolve-then-bake driver reuses the main-frame restore
// helpers and gathers the opt-in detached canvases off C_EntityCanvas (the
// same component the composite iterates).
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/voxel_frame_data.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/components/component_detached_revoxelize_buffer.hpp>

#include <utility>

#include <cstddef>
#include <cstdint>
#include <vector>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match `kSunShadowMapDim` in ir_sun_projection.glsl /
// c_clear_sun_shadow_map.glsl (and Metal counterparts).
constexpr int kSunShadowMapDim = 1024;
// `kSunShadowMaxDistance` (the AABB-sweep length, also consumed by the iso
// rasterizers' shadow-feeder cull) lives in `sun_shadow_constants.hpp` so
// both producers stay in lockstep.
using IRPrefab::SunShadow::kSunShadowMaxDistance;
constexpr int kBakeSunShadowGroupSize = 16;
constexpr int kSunShadowCascadeCount = 2;
constexpr float kCascadeSplitRatio = 0.4f;
// coverage-splat radius (sun texels): c_bake_sun_shadow_map atomicMin's
// each caster's depth into a (2·r+1)² box, filling the sun texels a grazing /
// point-scattered caster footprint leaves empty (the moth-eaten cast-shadow
// holes). Engaged for the cardinal main-canvas bake AND the world-placed cast
// resolve (its cast has the same defect). The PER-AXIS resolve zeros it via
// patchSunSplatRadius (structural byte-identity for invariant #1). Doubles as
// the shader kill switch — 0 forces the exact single-write path. r is the
// measured minimum on a decontaminated genuine cast, and 7 is also the nibble
// cap the displacement encoding can carry. A larger radius requires widening
// that encoding. Radius sweeps and the atomic
// cost rule: docs/design/sun-shadow-bake-coverage.md.
//
// Changing this radius is NOT output-neutral for world-placed detached
// solids: the world-placed cast resolve keeps the splat engaged, so
// their cast EDGES move with r. Only the per-axis resolve is radius-0 and
// therefore radius-invariant — the canvas_stress so3_* / revoxelize_* shots
// contain world-placed content and WILL diff on any radius change. Re-bless
// those references deliberately, never reflexively.
constexpr int kSunSplatMaxTexels = 7;
static_assert(kSunSplatMaxTexels < 8, "Splat offsets must reserve -8 for finite surface writes");

namespace detail {

struct ResolvedSun {
    vec3 direction_ = IRRender::getSunDirection();
    float intensity_ = IRRender::getSunIntensity();
    float ambient_ = IRRender::getSunAmbient();
    bool shadowsEnabled_ = IRRender::getSunShadowsEnabled();
    bool aoEnabled_ = IRRender::getAOEnabled();
};

// A single ECS entity carrying `C_LightSource{type=DIRECTIONAL}` overrides
// the global sun. The first three fields snapshot RenderManager state, so
// scenes without a directional light entity still get coherent values.
inline ResolvedSun resolveSun() {
    ResolvedSun sun{};
    const auto include = IREntity::getArchetype<C_LightSource>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);
    bool foundDirectional = false;
    for (auto *node : nodes) {
        auto &lights = IREntity::getComponentData<C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            const C_LightSource &light = lights[i];
            if (light.type_ != LightType::DIRECTIONAL)
                continue;
            IR_ASSERT(!foundDirectional, "Only one DIRECTIONAL light may drive the global sun");
            foundDirectional = true;

            IR_ASSERT(
                light.direction_.z <= 0.0f,
                "Directional light points from the world toward the sun; +Z is down, so z must be "
                "<= 0"
            );
            const float length = IRMath::length(light.direction_);
            if (length > 0.0f) {
                sun.direction_ = light.direction_ / length;
            }
            sun.intensity_ = IRMath::max(0.0f, light.intensity_);
            sun.ambient_ = IRMath::clamp(light.ambient_, 0.0f, 1.0f);
        }
    }
    sun.shadowsEnabled_ = IRRender::getSunShadowsEnabled();
    sun.aoEnabled_ = IRRender::getAOEnabled();
    return sun;
}

} // namespace detail

// World-placed re-voxelize caster gathered by gatherWorldPlacedCasters()
// below. Hoisted to namespace scope (mirrors LightGatherRecord
// in system_compute_light_volume.hpp) so a read-back consumer — the culling
// minimap's caster domain can name the type without reaching
// into System<BAKE_SUN_SHADOW_MAP>'s private members.
struct WorldPlacedCaster {
    const C_TriangleCanvasTextures *textures_ = nullptr;
    vec3 worldCellOffset_{0.0f};
};

struct DetachedShadowFrame {
    vec4 worldOriginAndDensity_;
    vec4 viewToWorld_;
};
static_assert(sizeof(DetachedShadowFrame) == 32, "DetachedShadowFrame must match the shader UBO");
static_assert(
    offsetof(DetachedShadowFrame, viewToWorld_) == 16, "Quaternion must begin at byte 16"
);

struct VoxelSunFaceFrame {
    vec4 worldOrigin_;
    vec4 viewToWorld_;
    ivec4 dispatch_;
    ivec4 sourceMin_;
    ivec4 sourceDims_;
    vec4 sourceAnchor_;
};
static_assert(sizeof(VoxelSunFaceFrame) == 96, "Voxel sun-face frame must match the shader UBO");
static_assert(
    offsetof(VoxelSunFaceFrame, sourceMin_) == 48 &&
        offsetof(VoxelSunFaceFrame, sourceDims_) == 64 &&
        offsetof(VoxelSunFaceFrame, sourceAnchor_) == 80,
    "Source grid bounds must match GLSL and Metal offsets"
);

template <> struct System<BAKE_SUN_SHADOW_MAP> {
    bool voxelFaceCoverage_ = true;
    bool sourceFaceCoverage_ = true;
    bool coverageFrameOpen_ = false;
    bool frameUsesFiniteCoverage_ = false;
    Buffer *analyticCasterFrameBuf_ = nullptr;
    std::pair<ResourceId, Texture2D *> analyticCasterDepth_{0, nullptr};
    ivec2 analyticCasterSize_{0};
    bool analyticCasterReady_ = false;
    FrameDataVoxelToCanvas analyticCasterFrame_{};

    Texture2D *prepareAnalyticCasterDepth(const GPUShapesFrameData &shapeFrame) {
        const ivec2 size = shapeFrame.canvasSize;
        if (!frameUsesFiniteCoverage_ || frameData_.shadowsEnabled_ == 0) {
            return nullptr;
        }
        if (analyticCasterDepth_.second == nullptr || analyticCasterSize_.x < size.x ||
            analyticCasterSize_.y < size.y) {
            if (analyticCasterDepth_.second != nullptr) {
                IRRender::destroyResource<Texture2D>(analyticCasterDepth_.first);
            }
            analyticCasterSize_ = IRMath::max(analyticCasterSize_, size);
            analyticCasterDepth_ =
                IRComponents::detail::makeCanvasDistanceTexture(analyticCasterSize_);
        }
        const int empty = IRConstants::kTrixelDistanceMaxDistance;
        // Device clear also initializes the Metal image-atomic scratch buffer.
        IRRender::device()->clearTexImage(analyticCasterDepth_.second, 0, &empty);
        analyticCasterFrame_.cameraTrixelOffset_ = shapeFrame.cameraTrixelOffset;
        analyticCasterFrame_.trixelCanvasOffsetZ1_ = shapeFrame.trixelCanvasOffsetZ1;
        analyticCasterFrame_.canvasSizePixels_ = shapeFrame.canvasSize;
        analyticCasterFrame_.voxelRenderOptions_ = shapeFrame.voxelRenderOptions;
        analyticCasterFrame_.visualYaw_ = shapeFrame.visualYaw;
        analyticCasterFrame_.rasterYaw_ = shapeFrame.rasterYaw;
        analyticCasterFrame_.residualYaw_ =
            shapeFrame.smoothYawEnabled ? shapeFrame.residualYaw : 0.0f;
        analyticCasterReady_ = true;
        return analyticCasterDepth_.second;
    }

    void bakeAnalyticCasterDepth() {
        if (!analyticCasterReady_)
            return;
        analyticCasterFrameBuf_->subData(0, sizeof(analyticCasterFrame_), &analyticCasterFrame_);
        analyticCasterFrameBuf_->bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataVoxelToCanvas
        );
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        bakeProgram_->use();
        analyticCasterDepth_.second->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        IRRender::device()->dispatchCompute(
            IRMath::divCeil(analyticCasterFrame_.canvasSizePixels_.x, kBakeSunShadowGroupSize),
            IRMath::divCeil(analyticCasterFrame_.canvasSizePixels_.y, kBakeSunShadowGroupSize),
            1
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        analyticCasterReady_ = false;
    }

    ShaderProgram *boxSunProgram_ = nullptr;

    void bakeAnalyticBoxes(int count, int subdivisions) {
        if (!frameUsesFiniteCoverage_ || frameData_.shadowsEnabled_ == 0 || count == 0)
            return;
        const ivec2 grid = voxelDispatchGridForCount(count);
        const ivec4 params(count, grid.x, subdivisions, 0);
        voxelFaceFrameBuf_->subData(0, sizeof(params), &params);
        voxelFaceFrameBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_RevoxelizeDetachedParams);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        boxSunProgram_->use();
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        revoxelizeParamsBuf_->bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_RevoxelizeDetachedParams
        );
    }

    ShaderProgram *voxelFaceProgram_ = nullptr;
    Buffer *voxelFaceFrameBuf_ = nullptr;
    ShaderProgram *clearProgram_ = nullptr;
    ShaderProgram *bakeProgram_ = nullptr;
    Buffer *sunShadowDepthMap_ = nullptr;
    Buffer *sunShadowFrameDataBuf_ = nullptr;
    Buffer *voxelFrameDataBuf_ = nullptr;
    Buffer *detachedShadowFrameBuf_ = nullptr;
    Buffer *revoxelizeParamsBuf_ = nullptr;
    FrameDataSun frameData_{};

    // Smooth camera Z-yaw: main canvas + its per-axis voxel canvases,
    // re-resolved every frame in beginTick. Null unless allocated (rotating).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // World sun-shadow cast uses resolve-then-bake.
    // Opt-in world-placed detached re-voxelize solids gathered once per frame in
    // beginTick (off C_EntityCanvas, the same component the composite iterates).
    // The cast mirrors the per-axis resolve precedent above faithfully: scatter
    // every caster's model-frame distances (+ its world cell origin) into ONE
    // shared main-canvas-layout scratch, blit to the resolve texture, then ONE
    // extra bake dispatch through the cardinal recovery. The bake
    // only ever reads main-canvas-layout depth sources — a foreign model-frame
    // canvas texture is never a bake input because a direct read returns empty
    // through Metal's image-atomic scratch indirection. An empty caster list
    // leaves the main-canvas bake unchanged.
    // (`WorldPlacedCaster` itself lives at namespace scope above.)
    std::vector<WorldPlacedCaster> worldPlacedCasters_;
    FrameDataVoxelToCanvas voxelFrameScratch_{};
    const C_TriangleCanvasTextures *mainTextures_ = nullptr;
    const C_VoxelPool *mainPool_ = nullptr;
    const C_CanvasLocalRotation *mainRotation_ = nullptr;

    ShaderProgram *worldPlacedScatterProgram_ = nullptr;
    ShaderProgram *worldPlacedBlitProgram_ = nullptr;
    // Main-canvas-sized front-most iso-depth scratch + the resolve texture the
    // extra bake dispatch reads. Same buffer-not-texture rationale as the
    // per-axis resolve (Metal has no portable image-atomic syntax); the blit
    // self-resets the scratch each frame. (Re)allocated on canvas resize only.
    ResourceId worldPlacedScratchId_ = 0;
    Buffer *worldPlacedScratch_ = nullptr;
    std::pair<ResourceId, Texture2D *> worldPlacedResolveDepth_{0, nullptr};
    ivec2 worldPlacedResolveSize_ = ivec2(0, 0);

    // (Re)allocate the scratch SSBO + resolve texture to @p mainSize. Runs only
    // on first use and on a canvas resize, not per frame. One seed dispatch of
    // the blit kernel resets every scratch slot to the empty sentinel without a
    // canvas-sized CPU staging upload (the per-frame blit keeps it reset
    // thereafter). The texture texels that seed dispatch writes are garbage but
    // never observable: the extra bake only runs in frames where the per-frame
    // blit has just rewritten every texel.
    void ensureWorldPlacedResolve(ivec2 mainSize) {
        if (worldPlacedScratch_ != nullptr && worldPlacedResolveSize_ == mainSize) {
            return;
        }
        if (worldPlacedScratch_ != nullptr) {
            IRRender::destroyResource<Buffer>(worldPlacedScratchId_);
            worldPlacedScratch_ = nullptr;
            IRRender::destroyResource<Texture2D>(worldPlacedResolveDepth_.first);
            worldPlacedResolveDepth_ = {0, nullptr};
        }
        const std::size_t count =
            static_cast<std::size_t>(mainSize.x) * static_cast<std::size_t>(mainSize.y);
        auto created = IRRender::createResource<Buffer>(
            nullptr,
            count * sizeof(std::int32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
        worldPlacedScratchId_ = created.first;
        worldPlacedScratch_ = created.second;
        worldPlacedResolveSize_ = mainSize;
        worldPlacedResolveDepth_ = IRComponents::detail::makeCanvasDistanceTexture(mainSize);

        worldPlacedBlitProgram_->use();
        worldPlacedScratch_->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_PerAxisResolveScratch
        );
        worldPlacedResolveDepth_.second
            ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
        IRRender::device()->dispatchCompute(
            IRMath::divCeil(mainSize.x, kBakeSunShadowGroupSize),
            IRMath::divCeil(mainSize.y, kBakeSunShadowGroupSize),
            1
        );
        // ALL, not SHADER_STORAGE: the seed also image-writes the resolve
        // texture, which the per-frame blit image-writes again this tick.
        IRRender::device()->memoryBarrier(BarrierType::ALL);
    }

    // Patch the resident main frame's yaw split for a CARDINAL-layout bake
    // input, then restore the camera split afterward. The resolve
    // textures re-projected their content
    // into the cardinal main-canvas layout, but the resident main frame
    // carries the camera's residual yaw, which would route the bake shader's
    // recovery onto the smooth-yaw inverse meant for the SDF store.
    // subData orphans the UBO on Metal, so re-bind after patching (the
    // in-file convention).
    void patchFrameYawSplit(float visualYaw, float residualYaw) {
        voxelFrameDataBuf_
            ->subData(offsetof(FrameDataVoxelToCanvas, visualYaw_), sizeof(float), &visualYaw);
        voxelFrameDataBuf_
            ->subData(offsetof(FrameDataVoxelToCanvas, residualYaw_), sizeof(float), &residualYaw);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
    }

    // Scope-guard that restores the shared resident frame's yaw split on
    // destruction, so an early return or exception between
    // patchFrameYawSplit(rasterYaw, 0) and the end of a CARDINAL-layout
    // dispatch block can never leave residualYaw_ = 0 for downstream consumers.
    struct FrameYawRestoreGuard {
        System<BAKE_SUN_SHADOW_MAP> &sys_;
        float visualYaw_, residualYaw_;
        ~FrameYawRestoreGuard() {
            sys_.patchFrameYawSplit(visualYaw_, residualYaw_);
        }
    };

    // Patch the resident FrameDataSun UBO's coverage-splat radius (binding
    // kBufferIndex_FrameDataSun) and re-bind — subData orphans the buffer on
    // Metal, same convention as patchFrameYawSplit. Used to zero the radius for
    // the PER-AXIS resolve dispatch. The shader's splat gate
    // (`perAxisRoute == 0 && residualYaw == 0 && sunSplatMaxTexels > 0`) reads the
    // *decode-path* predicate, not camera cardinality: the per-axis resolve
    // deliberately zeros residualYaw to reuse the cardinal recovery, so
    // it would spuriously trip the splat while rotating and change the per-axis
    // smooth-yaw output. Zeroing the radius there makes that
    // a structural no-splat path (radius 0) instead of leaning
    // on the per-axis dense-footprint assumption. World-placed casting does not
    // use this — it has point-scatter holes the splat
    // must fill (measured). See docs/design/sun-shadow-bake-coverage.md
    // § "Byte-identity regimes".
    void patchSunSplatRadius(float radiusTexels) {
        sunShadowFrameDataBuf_
            ->subData(offsetof(FrameDataSun, sunSplatMaxTexels_), sizeof(float), &radiusTexels);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
    }

    // Scope-guard mate for patchSunSplatRadius — restores the canonical radius on
    // destruction so a resolve dispatch's zeroing never leaks into the resident
    // FrameDataSun downstream consumers read.
    struct SunSplatRestoreGuard {
        System<BAKE_SUN_SHADOW_MAP> &sys_;
        float radiusTexels_;
        ~SunSplatRestoreGuard() {
            sys_.patchSunSplatRadius(radiusTexels_);
        }
    };

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_CanvasSunShadow &,
        const C_TrixelCanvasRenderBehavior &behavior
    ) {
        if (!behavior.useCameraPositionIso_) {
            return;
        }

        sunShadowFrameDataBuf_->subData(0, sizeof(FrameDataSun), &frameData_);
        if (frameData_.shadowsEnabled_ == 0) {
            return;
        }

        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        if (frameUsesFiniteCoverage_) {
            sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
            sunShadowDepthMap_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_SunShadowDepthMap
            );
            return;
        }
        clearDepthMap();

        // Bake: each pixel projects into both cascades in one pass.
        bakeProgram_->use();
        canvasTextures.getTextureDistances()
            ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kBakeSunShadowGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kBakeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

        // Smooth camera Z-yaw: bake per-axis voxel sun shadows from the
        // screen-space resolve texture RESOLVE_PER_AXIS_SCREEN_DEPTH produced.
        // The resolve scattered the three face-local per-axis canvases into the
        // main canvas's cardinal distance layout (front-most per screen pixel),
        // so we cast it through the SAME cardinal path as the main canvas
        // (perAxisRoute stays 0, recovery via trixelCanvasPixelToWorld3D) — no
        // shader change, just a second source texture. This is what fixed the
        // cross-face self-occlusion: the per-screen-pixel flattening the
        // raw face-local store lacked. The per-axis RECEIVE
        // (COMPUTE_SUN_SHADOW, perAxisCellToWorld3DSubCell) recovers the
        // lattice origin PLUS the encoding's sub-cell frac, so the resolve
        // applies that same frac before it emits — but the agreement is
        // only as fine as the destination layout: the resolve deposits into an
        // INTEGER cardinal layout at effSub resolution, so the frac survives to
        // 1/effSub of a world cell and at effSub == 1 it is quantized away
        // entirely. Fractionally-positioned per-axis content at effSub == 1
        // therefore still casts from its lattice cell while receiving at its
        // true sub-cell position — a residual bounded by half a world cell,
        // documented in docs/design/per-axis-sun-shadow-resolve.md, not a
        // regression this dispatch can close without a finer layout. If the
        // resolve stage is not registered, resolveDepth_ stays cleared to the
        // empty sentinel (component allocate) so this dispatch casts nothing —
        // graceful no-op, not corruption. Cardinal (residualYaw == 0) is
        // byte-identical: the per-axis canvases are only allocated while
        // rotating, so this branch never runs at a cardinal.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            // The resolve texture is CARDINAL-layout — zero the resident
            // frame's residual for this dispatch so the bake shader keeps the
            // cardinal recovery, then restore the camera split for
            // downstream consumers (COMPUTE_SUN_SHADOW's smooth receive).
            const float cameraVisualYaw = IRPrefab::Camera::getYaw();
            const auto [cameraRasterYaw, cameraResidualYaw] =
                IRPrefab::Camera::computeYawSplit(cameraVisualYaw);
            patchFrameYawSplit(cameraRasterYaw, 0.0f);
            const FrameYawRestoreGuard restoreGuard{*this, cameraVisualYaw, cameraResidualYaw};
            // This resolve reuses the cardinal recovery (residualYaw == 0), which
            // would spuriously engage the coverage splat; the per-axis
            // resolve is already footprint-dense, so gate it off
            // structurally for this dispatch (byte-identical to master).
            patchSunSplatRadius(0.0f);
            const SunSplatRestoreGuard splatGuard{*this, frameData_.sunSplatMaxTexels_};
            // resolveDepth_ is allocated at the main canvas size, so dispatch
            // over canvasTextures.size_ (same domain as the main bake above).
            perAxisCanvases_->resolveDepth_.second
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            // Restore the main-canvas distance image so the persistent Metal
            // image-binding table doesn't dangle when release() frees the
            // resolve texture (mirrors the COMPUTE_SUN_SHADOW per-axis restore).
            canvasTextures.getTextureDistances()
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        }

        // World sun-shadow CAST for opt-in world-placed detached re-voxelize
        // solids through resolve-then-bake. Mirrors the
        // per-axis resolve precedent above: scatter every caster's model-frame
        // distances into ONE shared main-canvas-layout scratch (lifted to world
        // by its cell origin), blit to the resolve texture, then ONE extra bake
        // dispatch through the same cardinal recovery as the main canvas. The
        // resolve texture is imageStore-written (real texture memory), so the
        // bake's read works on Metal — unlike the canvases' own imageAtomicMin
        // distance textures, whose data lives behind the image-atomic scratch
        // indirection and reads empty in a second in-tick bake dispatch. A direct
        // second bake therefore cannot consume them. Requires a
        // voxel main canvas (the frame restore below re-authors its UBO state).
        if (entity == perAxisCanvasEntity_ && !worldPlacedCasters_.empty() &&
            mainTextures_ != nullptr && mainPool_ != nullptr && mainRotation_ != nullptr) {
            ensureWorldPlacedResolve(canvasTextures.size_);

            // Author the MAIN frame fresh so the scatter's output projection
            // (main canvas size, camera raster yaw, camera pan) is known-good
            // regardless of which canvas STAGE_1 left resident. Byte-identical
            // content to what the main bake above already read.
            restoreMainCanvasVoxelFrame(
                voxelFrameScratch_,
                voxelFrameDataBuf_,
                mainTextures_,
                mainPool_,
                mainRotation_
            );
            // Passes 1–3 below project / recover through the CARDINAL layout
            // (the scatter reads rasterYaw only; pass 3's bake recovery must
            // not take the smooth-yaw branch) — zero the resident residual for
            // the block and restore the camera split at the end.
            const float cameraVisualYaw = IRPrefab::Camera::getYaw();
            const auto [cameraRasterYaw, cameraResidualYaw] =
                IRPrefab::Camera::computeYawSplit(cameraVisualYaw);
            patchFrameYawSplit(cameraRasterYaw, 0.0f);
            const FrameYawRestoreGuard restoreGuard{*this, cameraVisualYaw, cameraResidualYaw};
            // NOTE: unlike the per-axis resolve above, the coverage splat
            // is left ENGAGED for pass 3's bake. The world-placed re-voxelize
            // cast's resolve texture carries the SAME screen-space point-scatter
            // as the main canvas (its sun-UV projection undersamples grazing
            // surfaces identically), so the splat is load-bearing here — measured
            // on shadow_overlay_floor, gating it off shatters the cube's cast to
            // 10 components (r6 splat → 1). The splat reuses the cardinal recovery
            // (residualYaw == 0) at every yaw, which is intentional: the cube's
            // cast is a coverage fix, not part of invariant #1's per-axis /
            // smooth-yaw byte-identity. See docs/design/sun-shadow-bake-coverage.md.

            // Slot 16 is free after voxel rasterization; restore its owner before leaving.
            worldPlacedScatterProgram_->use();
            worldPlacedScratch_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisResolveScratch
            );
            const vec4 viewToWorld = IRPrefab::Camera::getRotationQuat();
            for (const auto &caster : worldPlacedCasters_) {
                const DetachedShadowFrame casterFrame{
                    vec4(
                        caster.worldCellOffset_,
                        static_cast<float>(IRMath::max(caster.textures_->renderedSubdivisions_, 1))
                    ),
                    viewToWorld
                };
                detachedShadowFrameBuf_->subData(0, sizeof(casterFrame), &casterFrame);
                detachedShadowFrameBuf_->bindBase(
                    BufferTarget::UNIFORM,
                    kBufferIndex_RevoxelizeDetachedParams
                );
                caster.textures_->getTextureDistances()
                    ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
                IRRender::device()->dispatchCompute(
                    IRMath::divCeil(caster.textures_->size_.x, kBakeSunShadowGroupSize),
                    IRMath::divCeil(caster.textures_->size_.y, kBakeSunShadowGroupSize),
                    1
                );
            }
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            revoxelizeParamsBuf_->bindBase(
                BufferTarget::UNIFORM,
                kBufferIndex_RevoxelizeDetachedParams
            );

            // Pass 2 — blit the scratch into the resolve texture (and self-reset
            // the scratch for next frame). Shares the per-axis blit kernel.
            worldPlacedBlitProgram_->use();
            worldPlacedScratch_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisResolveScratch
            );
            worldPlacedResolveDepth_.second
                ->bindAsImage(1, TextureAccess::WRITE_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::ALL);

            // Pass 3 — ONE extra bake of the main-layout resolve texture through
            // the unchanged cardinal recovery (the per-axis resolveDepth_ bake
            // above is the in-file precedent). Re-bind the bake's full set: the
            // scratch aliased slot 28 and the UBO was orphaned by the subData.
            bakeProgram_->use();
            sunShadowDepthMap_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_SunShadowDepthMap
            );
            voxelFrameDataBuf_->bindBase(
                BufferTarget::UNIFORM,
                kBufferIndex_FrameDataVoxelToCanvas
            );
            sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
            worldPlacedResolveDepth_.second
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
            IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
            // Restore the main-canvas distance image so the persistent Metal
            // image-binding table doesn't dangle (mirrors the per-axis restore).
            canvasTextures.getTextureDistances()
                ->bindAsImage(0, TextureAccess::READ_ONLY, TextureFormat::R32I);
        }
    }

    // Collect world-placed detached re-voxelize solids for the cast resolve;
    // screenLocked_ canvases opt out. Iterates C_EntityCanvas — the same
    // component the composite
    // (ENTITY_CANVAS_TO_FRAMEBUFFER) reads screenLocked_ off — and
    // captures each world-placed canvas's distance texture + the world cell origin
    // PROPAGATE_CANVAS_ROTATION stamped on its C_CanvasLocalRotation (the same
    // propagated values the world-receive path consumes, so cast and receive share
    // one world origin per frame). Once per frame in beginTick (few canvases),
    // so the per-canvas getComponentOptional is canvas iteration, not the
    // per-voxel ECS footgun.
    void gatherWorldPlacedCasters() {
        worldPlacedCasters_.clear();
        const auto include = IREntity::getArchetype<C_EntityCanvas>();
        auto nodes = IREntity::queryArchetypeNodesSimple(include);
        for (auto *node : nodes) {
            auto &canvases = IREntity::getComponentData<C_EntityCanvas>(node);
            for (int i = 0; i < node->length_; ++i) {
                const C_EntityCanvas &entityCanvas = canvases[i];
                if (entityCanvas.screenLocked_ || !entityCanvas.visible_ ||
                    entityCanvas.canvasEntity_ == IREntity::kNullEntity) {
                    continue;
                }
                auto textures = IREntity::getComponentOptional<C_TriangleCanvasTextures>(
                    entityCanvas.canvasEntity_
                );
                auto rotation = IREntity::getComponentOptional<C_CanvasLocalRotation>(
                    entityCanvas.canvasEntity_
                );
                if (!textures.has_value() || !rotation.has_value()) {
                    continue;
                }
                // Only the re-voxelize path is world-placeable (the octahedral-
                // snap deform recovers pos differently — see buildVoxelFrameData);
                // skip canvases whose propagation hasn't run yet this frame.
                const C_CanvasLocalRotation &rot = *rotation.value();
                if (!rot.worldPlaced_ || !rot.reVoxelize_ || !rot.isDetached()) {
                    continue;
                }
                worldPlacedCasters_.push_back(
                    {textures.value(),
                     rot.worldCellOffset_ + IRMath::rotateVectorByQuat(
                                                textures.value()->renderedCellOffset_,
                                                IRPrefab::Camera::getRotationQuat()
                                            )}
                );
            }
        }
    }

    void endTick() {
        coverageFrameOpen_ = false;
    }

    void beginTick() {
        beginVoxelFaceCoverage();
        if (sunShadowFrameDataBuf_ == nullptr) {
            sunShadowFrameDataBuf_ =
                IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }

        // Resolve the main canvas + its per-axis voxel canvases. Done
        // before the shadowsEnabled early-return so the pointer is always fresh.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
        }

        // World sun-shadow cast: gather opt-in world-placed detached
        // re-voxelize solids + resolve the main canvas's voxel-frame inputs (for
        // the pre-scatter main-frame author). Done before the shadowsEnabled
        // early-return so the list is always fresh (tick won't cast when shadows
        // are off anyway). The query + per-canvas getComponentOptional are
        // once-per-frame canvas iteration (few canvases), not the per-voxel ECS
        // footgun — same pattern as resolveSun() above.
        gatherWorldPlacedCasters();
        {
            // Surface the caster count and widened shadow-feeder
            // AABB on the perf HUD's CULL block (same GpuStageTiming counter
            // pattern as COMPUTE_LIGHT_VOLUME's lightsSeeded_/lightsEligible_).
            // Uses the canonical gated helper (frameShadowFeederParams()) so
            // the reported extent collapses to the plain (no-widen) cull
            // viewport when shadows are off, matching every other feeder
            // consumer's convention.
            auto &timing = IRRender::gpuStageTiming();
            timing.worldPlacedCasterCount_ = static_cast<std::uint32_t>(worldPlacedCasters_.size());
            const IsoBounds2D feederVp = IRPrefab::SunShadow::shadowFeederCullViewport(
                0,
                IRPrefab::SunShadow::frameShadowFeederParams()
            );
            timing.shadowFeederMin_ = feederVp.min_;
            timing.shadowFeederMax_ = feederVp.max_;
        }
        resolveMainCanvasVoxelFrameInputs(
            perAxisCanvasEntity_,
            &mainTextures_,
            &mainPool_,
            &mainRotation_
        );

        if (!frameUsesFiniteCoverage_) {
            updateSunFrameData();
        }
    }

    void updateSunFrameData() {
        const detail::ResolvedSun sun = detail::resolveSun();
        vec3 sunDir = sun.direction_;
        const float sunLen = IRMath::length(sunDir);
        if (sunLen > 0.0f) {
            sunDir /= sunLen;
        }
        frameData_.sunDirection_ = vec4(sunDir, 0.0f);
        frameData_.sunIntensity_ = sun.intensity_;
        frameData_.sunAmbient_ = sun.ambient_;
        frameData_.shadowsEnabled_ = sun.shadowsEnabled_ ? 1 : 0;
        frameData_.aoEnabled_ = sun.aoEnabled_ ? 1 : 0;
        if (frameData_.shadowsEnabled_ == 0) {
            return;
        }

        vec3 uHat, vHat;
        IRMath::buildOrthonormalBasis(sunDir, uHat, vHat);
        frameData_.sunBasisU_ = vec4(uHat, 0.0f);
        frameData_.sunBasisV_ = vec4(vHat, 0.0f);

        const auto &cull = IRRender::getCullViewport();
        const ivec2 canvasSize = cull.canvasSize_;
        const IsoBounds2D isoBounds = cull.isoViewportForCanvas(canvasSize, 0);

        constexpr float kIsoDepthMin = -256.0f;
        constexpr float kIsoDepthMax = 256.0f;
        const float splitDepth = kIsoDepthMin + (kIsoDepthMax - kIsoDepthMin) * kCascadeSplitRatio;

        struct CascadeDepthRange {
            float min_;
            float max_;
        };
        const CascadeDepthRange cascadeRanges[kSunShadowCascadeCount] = {
            {kIsoDepthMin, splitDepth},
            {kIsoDepthMin, kIsoDepthMax},
        };

        const vec3 sweep = -sunDir * kSunShadowMaxDistance;
        constexpr float kAABBPad = 1.0f;
        const float dimF = static_cast<float>(kSunShadowMapDim);

        // isoPixelToPos3D returns corners in the rasterYaw-rotated canvas
        // frame; sunBakeFrustumUVBounds lifts them into world frame
        // (rotateCardinalZInv) before the per-cascade AABB sweep, so both sides
        // share the same coordinate frame. No-op at rasterYaw == 0.
        const auto cardinalIndex = IRMath::rasterYawCardinalIndex(IRPrefab::Camera::getRasterYaw());

        for (int ci = 0; ci < kSunShadowCascadeCount; ++ci) {
            const IsoBounds2D sunUV = IRPrefab::SunShadow::sunBakeFrustumUVBounds(
                isoBounds,
                cascadeRanges[ci].min_,
                cascadeRanges[ci].max_,
                uHat,
                vHat,
                sunDir,
                cardinalIndex,
                sweep
            );
            vec2 sunUVMin = sunUV.min_;
            vec2 sunUVMax = sunUV.max_;

            sunUVMin -= vec2(kAABBPad);
            sunUVMax += vec2(kAABBPad);
            vec2 extent = sunUVMax - sunUVMin;
            vec2 texelSize = vec2(extent.x / dimF, extent.y / dimF);

            // Snap both edges to the texel grid so bake and lookup share the
            // same phase; recompute texelSize so the buffer covers the full
            // snapped extent symmetrically (asymmetric snap eats high-side pad
            // at texelSize > kAABBPad on wide viewports).
            sunUVMin.x = IRMath::floor(sunUVMin.x / texelSize.x) * texelSize.x;
            sunUVMin.y = IRMath::floor(sunUVMin.y / texelSize.y) * texelSize.y;
            sunUVMax.x = IRMath::ceil(sunUVMax.x / texelSize.x) * texelSize.x;
            sunUVMax.y = IRMath::ceil(sunUVMax.y / texelSize.y) * texelSize.y;
            texelSize = vec2((sunUVMax.x - sunUVMin.x) / dimF, (sunUVMax.y - sunUVMin.y) / dimF);

            if (ci == 0) {
                frameData_.cascadeOriginUV_0_ = sunUVMin;
                frameData_.cascadeTexelSize_0_ = texelSize;
                frameData_.sunBufferOriginUV_ = sunUVMin;
                frameData_.sunBufferTexelSize_ = texelSize;
            } else {
                frameData_.cascadeOriginUV_1_ = sunUVMin;
                frameData_.cascadeTexelSize_1_ = texelSize;
            }
        }
        frameData_.cascadeSplitDepth_ = splitDepth;
        frameData_.cascadeCount_ = kSunShadowCascadeCount;

        // coverage-splat radius / kill switch. c_bake_sun_shadow_map
        // atomicMin's each caster's depth into a (2·r+1)² box to fill the
        // point-scattered cast-shadow holes; the atomicMin makes it a no-op where
        // geometry is already dense (saturated-host byte-identity). This resident
        // value drives the cardinal main-canvas bake AND the world-placed cast
        // resolve (whose cast carries the same defect). Only the PER-AXIS resolve
        // dispatch zeros it via patchSunSplatRadius — its spoofed residualYaw == 0
        // would otherwise trip the shader gate and break invariant #1's per-axis /
        // smooth-yaw byte-identity. Set 0 here to force the exact single-write
        // path everywhere (the byte-identity backstop). See
        // docs/design/sun-shadow-bake-coverage.md.
        frameData_.sunSplatMaxTexels_ = static_cast<float>(kSunSplatMaxTexels);
        // The receiver's shadow-throw window reads the SAME sweep distance the
        // feeder / bake AABB use, so a baked caster is receivable at its full
        // throw and the two cannot drift.
        frameData_.sunMaxShadowThrow_ = kSunShadowMaxDistance;
    }

    void clearDepthMap() {
        // Clear the full depth map (both cascades) to 0xFFFFFFFF.
        clearProgram_->use();
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        const int totalClearDim = kSunShadowMapDim;
        const int clearGroupsX = IRMath::divCeil(totalClearDim, kBakeSunShadowGroupSize);
        // Y covers kSunShadowMapDim * kSunShadowCascadeCount rows — cascades
        // are stacked vertically in the 1024×2048 linearised buffer.
        const int clearGroupsY =
            IRMath::divCeil(totalClearDim * kSunShadowCascadeCount, kBakeSunShadowGroupSize);
        IRRender::device()->dispatchCompute(clearGroupsX, clearGroupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    // All voxel canvases share one clear and one cascade frame.
    bool beginVoxelFaceCoverage() {
        if (coverageFrameOpen_)
            return frameUsesFiniteCoverage_;
        coverageFrameOpen_ = true;
        // Particle producers still publish casters through the canvas depth input.
        frameUsesFiniteCoverage_ =
            voxelFaceCoverage_ &&
            findSystem(RENDER_STATELESS_PARTICLES_TO_TRIXEL) == kNullSystemId &&
            findSystem(RENDER_GPU_PARTICLES_TO_TRIXEL) == kNullSystemId;
        if (!frameUsesFiniteCoverage_)
            return false;
        analyticCasterReady_ = false;
        if (sunShadowFrameDataBuf_ == nullptr) {
            sunShadowFrameDataBuf_ =
                IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        }
        const auto mainCanvas = IRRender::getCanvas("main");
        const auto textures = IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvas);
        if (textures.has_value()) {
            IRRender::updateCullViewport(
                IRRender::getEffectiveCameraIso(),
                IRRender::getCameraZoom(),
                textures.value()->size_
            );
        }
        updateSunFrameData();
        sunShadowFrameDataBuf_->subData(0, sizeof(frameData_), &frameData_);
        if (frameData_.shadowsEnabled_ != 0) {
            clearDepthMap();
        }
        return true;
    }

    // Positions/colors remain resident only until the next canvas upload.
    void bakeVoxelFaces(
        int count,
        int subdivisions,
        const C_CanvasLocalRotation &rotation,
        const C_DetachedRevoxelizeBuffer *source,
        vec3 rasterCellOffset = vec3(0.0f)
    ) {
        if (frameData_.shadowsEnabled_ == 0 || count == 0 ||
            (rotation.isDetached() && (!rotation.worldPlaced_ || !rotation.reVoxelize_))) {
            return;
        }
        const bool useSource = sourceFaceCoverage_ && rotation.isDetached() && source != nullptr &&
                               source->sourceGrid_.second != nullptr;
        if (useSource) {
            count =
                source->sourceGridDims_.x * source->sourceGridDims_.y * source->sourceGridDims_.z;
            source->sourceGrid_.second->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_RevoxelizeSourceGrid
            );
        }
        const ivec2 grid = voxelDispatchGridForCount(IRMath::divCeil(count, 64));
        vec4 orientation = rotation.isDetached() ? IRPrefab::Camera::getRotationQuat()
                                                 : vec4(0.0f, 0.0f, 0.0f, 1.0f);
        if (useSource) {
            orientation = IRMath::quatMul(orientation, rotation.rotation_);
        }
        const VoxelSunFaceFrame params{
            vec4(
                rotation.isDetached()
                    ? rotation.worldCellOffset_ +
                          (useSource ? vec3(0.0f)
                                     : IRMath::rotateVectorByQuat(rasterCellOffset, orientation))
                    : vec3(0.0f),
                0.0f
            ),
            orientation,
            ivec4(count, grid.x, subdivisions, useSource ? 1 : 0),
            ivec4(useSource ? source->sourceGridMin_ : ivec3(0), 0),
            ivec4(useSource ? source->sourceGridDims_ : ivec3(0), 0),
            vec4(useSource ? source->anchor_ : vec3(0.0f), 0.0f)
        };
        voxelFaceFrameBuf_->subData(0, sizeof(params), &params);
        voxelFaceFrameBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_RevoxelizeDetachedParams);
        sunShadowFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        sunShadowDepthMap_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_SunShadowDepthMap);
        voxelFaceProgram_->use();
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        revoxelizeParamsBuf_->bindBase(
            BufferTarget::UNIFORM,
            kBufferIndex_RevoxelizeDetachedParams
        );
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "ClearSunShadowMapProgram",
            std::vector{ShaderStage{IRRender::kFileCompClearSunShadowMap, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<ShaderProgram>(
            "BakeSunShadowMapProgram",
            std::vector{ShaderStage{IRRender::kFileCompBakeSunShadowMap, ShaderType::COMPUTE}}
        );
        // World-placed detached cast resolve. The blit reuses the
        // per-axis kernel FILE under a bake-owned program name so the cast does
        // not depend on RESOLVE_PER_AXIS_SCREEN_DEPTH being registered.
        IRRender::createNamedResource<ShaderProgram>(
            "ResolveWorldPlacedDepthProgram",
            std::vector{
                ShaderStage{IRRender::kFileCompResolveWorldPlacedDepth, ShaderType::COMPUTE}
            }
        );
        IRRender::createNamedResource<ShaderProgram>(
            "WorldPlacedDepthBlitProgram",
            std::vector{ShaderStage{IRRender::kFileCompResolvePerAxisBlit, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "SunShadowDepthMap",
            nullptr,
            static_cast<std::size_t>(kSunShadowMapDim) *
                static_cast<std::size_t>(kSunShadowMapDim) * kSunShadowCascadeCount *
                sizeof(std::uint32_t),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_SunShadowDepthMap
        );

        SystemId systemId = registerSystem<
            BAKE_SUN_SHADOW_MAP,
            C_TriangleCanvasTextures,
            C_CanvasSunShadow,
            C_TrixelCanvasRenderBehavior>("BakeSunShadowMap");
        auto *p = getSystemParams<System<BAKE_SUN_SHADOW_MAP>>(systemId);
        p->analyticCasterFrameBuf_ = IRRender::createNamedResource<Buffer>(
                                         "AnalyticCasterFrameBuffer",
                                         nullptr,
                                         sizeof(FrameDataVoxelToCanvas),
                                         BUFFER_STORAGE_DYNAMIC,
                                         BufferTarget::UNIFORM,
                                         kBufferIndex_FrameDataVoxelToCanvas
        )
                                         .second;
        p->boxSunProgram_ =
            IRRender::createNamedResource<ShaderProgram>(
                "BoxSunShadowProgram",
                std::vector{ShaderStage{IRRender::kFileCompBakeBoxSunShadow, ShaderType::COMPUTE}}
            ).second;
        p->voxelFaceProgram_ =
            IRRender::createNamedResource<ShaderProgram>(
                "VoxelSunFacesProgram",
                std::vector{ShaderStage{IRRender::kFileCompBakeVoxelSunFaces, ShaderType::COMPUTE}}
            ).second;
        p->voxelFaceFrameBuf_ = IRRender::createNamedResource<Buffer>(
                                    "VoxelSunFaceFrameBuffer",
                                    nullptr,
                                    sizeof(VoxelSunFaceFrame),
                                    BUFFER_STORAGE_DYNAMIC,
                                    BufferTarget::UNIFORM,
                                    kBufferIndex_RevoxelizeDetachedParams
        )
                                    .second;
        p->detachedShadowFrameBuf_ = IRRender::createNamedResource<Buffer>(
                                         "DetachedShadowFrameBuffer",
                                         nullptr,
                                         sizeof(DetachedShadowFrame),
                                         BUFFER_STORAGE_DYNAMIC,
                                         BufferTarget::UNIFORM,
                                         kBufferIndex_RevoxelizeDetachedParams
        )
                                         .second;
        p->revoxelizeParamsBuf_ =
            IRRender::getNamedResource<Buffer>("RevoxelizeDetachedParamsBuffer");
        p->clearProgram_ = IRRender::getNamedResource<ShaderProgram>("ClearSunShadowMapProgram");
        p->bakeProgram_ = IRRender::getNamedResource<ShaderProgram>("BakeSunShadowMapProgram");
        p->worldPlacedScatterProgram_ =
            IRRender::getNamedResource<ShaderProgram>("ResolveWorldPlacedDepthProgram");
        p->worldPlacedBlitProgram_ =
            IRRender::getNamedResource<ShaderProgram>("WorldPlacedDepthBlitProgram");
        p->worldPlacedCasters_.reserve(8);
        p->sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        IRRender::tagGpuStage(systemId, "bakeSunShadowMap");
        return systemId;
    }
};

// Read-back accessor mirroring lightGatherRecords's pattern
// (system_compute_light_volume.hpp). The culling minimap's caster domain
// reads this frame's world-placed casters back rather than re-running
// gatherWorldPlacedCasters() itself.
inline const std::vector<WorldPlacedCaster> &worldPlacedCasters(SystemId bakeSunShadowMapSystem) {
    return getSystemParams<System<BAKE_SUN_SHADOW_MAP>>(bakeSunShadowMapSystem)
        ->worldPlacedCasters_;
}

} // namespace IRSystem

#endif /* SYSTEM_BAKE_SUN_SHADOW_MAP_H */
