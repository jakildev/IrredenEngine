#ifndef SYSTEM_LIGHTING_TO_TRIXEL_H
#define SYSTEM_LIGHTING_TO_TRIXEL_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>
#include <irreden/render/per_axis_canvas.hpp>
#include <irreden/render/voxel_dispatch_grid.hpp>
#include <irreden/render/voxel_frame_data.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRRender;

namespace IRSystem {

// Must match local_size in c_lighting_to_trixel.glsl / .metal.
constexpr int kLightingToTrixelGroupSize = 16;

// CPU-side mirror of the GLSL/MSL `FrameDataLightingToTrixel` UBO.
// `lutEnabled_` activates palette LUT shading, which replaces the plain
// grayscale AO multiplication with a luminance-indexed palette lookup
// whose X-axis is driven by the per-pixel AO value.
// `lightVolumeEnabled_` activates flood-fill light-volume sampling: the
// per-pixel world voxel is recovered from the distance texture and the
// bound 3D light volume is sampled and additively combined with the AO
// base.
// `debugLightLevel_` is reserved for future shadow-preview use; it is
// kept in the UBO for std140 layout stability but the shader currently
// uses AO.r as the LUT X-axis input.
// `debugOverlayMode_` mirrors `IRRender::DebugOverlayMode`. Non-zero
// values short-circuit the artistic path and write false-color into
// `trixelColors` instead — see ir_render_enums.hpp for the encoding.
// std140 note: eight scalars pack at offsets 0..28 (32 bytes), then
// skyColor_ (vec4) lands at offset 32 (already 16-byte aligned) for a
// 48-byte UBO. Both C++ and the GLSL/MSL structs lay out identically —
// no explicit padding is needed.
struct FrameDataLightingToTrixel {
    int lightingEnabled_ = 0;
    int lutEnabled_ = 0;
    int lightVolumeEnabled_ = 0;
    float debugLightLevel_ = 0.0f;
    int debugOverlayMode_ = 0;
    int hdrEnabled_ = 0;
    float exposure_ = 1.0f;
    float skyIntensity_ = 0.0f;
    vec4 skyColor_ = vec4(0.5f, 0.7f, 1.0f, 0.0f);
};

// Screen-space lighting application pass. Inserts between the final
// geometry stage and the compositing stage; reads the canvas distance
// texture plus per-pixel lighting inputs (currently AO via
// C_CanvasAOTexture) and modulates canvas color in place.
//
// GUI pixels are left untouched: canvases with
// `C_TrixelCanvasRenderBehavior::useCameraPositionIso_ == false` early-
// return in the tick.
template <> struct System<LIGHTING_TO_TRIXEL> {
    ShaderProgram *program_ = nullptr;
    // View-visibility overflow-face relight kernel (#2334): a bounded compute
    // dispatch at the tail of the per-axis lighting that relights the C1 (#2333)
    // overflow entries at their world pos and rewrites their stored colour in
    // place, so the framebuffer scatter draws LIT slivers while rotating.
    ShaderProgram *overflowLightingProgram_ = nullptr;
    // IR_OVERFLOW_LIGHTING_DISABLE in the environment skips the relight dispatch
    // (entries stay C1 albedo) — the A/B kill switch for the lit-vs-albedo
    // screenshot pair and the GPU-delta measurement (#2334 acceptance).
    bool overflowLightingDisabled_ = false;
    static constexpr int kOverflowLightingGroupSize = 64; // matches local_size_x
    Buffer *frameDataBuf_ = nullptr;
    // Reuse the voxel pipeline's per-frame UBO so we can recover the
    // world voxel position of each pixel via the same iso math the AO
    // pass uses. Created by VOXEL_TO_TRIXEL_STAGE_1; this system runs
    // later in the pipeline so the buffer is always populated.
    Buffer *voxelFrameDataBuf_ = nullptr;
    Buffer *sunFrameDataBuf_ = nullptr;
    // Phase 1c (#360): camera-anchored light-volume params UBO. Owned
    // + uploaded by COMPUTE_LIGHT_VOLUME; the lighting pass needs to
    // know the volume's world origin to map a pixel's world voxel
    // back into the volume texel.
    Buffer *lightVolumeParamsBuf_ = nullptr;
    // Light list SSBO (#2318): bound transiently at slot 4 during this pass so
    // the spot-cone factor can look up the winning light's axis / aperture /
    // apex. Owned + uploaded by COMPUTE_LIGHT_VOLUME (which binds it at slot 4
    // for its own seed pass); nothing downstream in the frame reads slot 4, so
    // no restore is needed. Only read on the has-SPOT path.
    Buffer *lightSourceBuf_ = nullptr;
    // Baked sun-aligned depth map (slot 28), created by BAKE_SUN_SHADOW_MAP.
    // The opt-in detached world-receive path (#1576 P4b-2) re-runs the cascade
    // lookup against it at a world-placed voxel's pos. Resolved lazily (it exists
    // by the time the pipeline runs); the shader declares the SSBO unconditionally
    // (Metal kernel arg), so it must be bound every tick — the default path just
    // never reads it. Whenever LIGHTING_TO_TRIXEL runs the main canvas carries
    // C_CanvasSunShadow (asserted below), so BAKE ran and the map exists.
    Buffer *sunShadowDepthMap_ = nullptr;
    Texture2D *paletteLUT_ = nullptr;
    FrameDataLightingToTrixel frameData_{};

    // Smooth camera Z-yaw (#1311): main canvas + per-axis voxel canvases,
    // re-resolved every frame in beginTick. Null unless allocated (rotating).
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    // Lazily-resolved voxel-compaction buffers (#1961/#2256), restored onto
    // slots 25/26 after dispatchPerAxisLighting borrows them for its own
    // per-axis cell list. See IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots.
    Buffer *voxelCompactedBuf_ = nullptr;
    Buffer *voxelIndirectBuf_ = nullptr;

    // Per-pass voxel-frame author/restore + main-canvas placeholders for the
    // relaxed multi-lit-canvas archetype (re-voxelize P4 / #1558). Resolved
    // once per frame in beginTick; never held across frames.
    FrameDataVoxelToCanvas scratchVoxelFrame_{};
    const C_TriangleCanvasTextures *mainCanvasTextures_ = nullptr;
    const C_VoxelPool *mainVoxelPool_ = nullptr;
    const C_CanvasLocalRotation *mainCanvasRotation_ = nullptr;
    // The detached re-voxelize canvas carries no C_CanvasSunShadow /
    // C_CanvasLightVolume (its lighting is AO + directional sun + sky only).
    // The shader's isDetachedCanvas branch never samples slots 4/5 for it, but
    // Metal still requires both setTexture slots populated — so bind the main
    // canvas's as inert placeholders. Resolved in beginTick.
    const C_CanvasSunShadow *mainCanvasSunShadow_ = nullptr;
    const C_CanvasLightVolume *mainCanvasLightVolume_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &canvasTextures,
        const C_TrixelCanvasRenderBehavior &behavior,
        const C_CanvasAOTexture &ao
    ) {
        if (!behavior.useCameraPositionIso_) {
            return;
        }

        // Author THIS canvas's voxel frame data so the Lambert + sky terms read
        // its own visible-triplet world normals and isDetachedCanvas flag, not
        // whatever canvas STAGE_1 left resident (#1558). No-op for a pure-SDF
        // canvas with no voxel pool.
        authorIteratingCanvasVoxelFrame(
            scratchVoxelFrame_,
            voxelFrameDataBuf_,
            entity,
            canvasTextures
        );

        // Relaxed archetype (#1558): sun-shadow + light-volume are optional, so
        // a detached re-voxelize canvas (which has neither) can still be lit.
        // Per-canvas component if present, else the main canvas's as an inert
        // placeholder — the shader's isDetachedCanvas branch forces shadow = 1.0
        // and disables the light volume for it, so the placeholders are never
        // sampled; they exist only to satisfy Metal's bound-slot requirement.
        auto shadowOpt = IREntity::getComponentOptional<C_CanvasSunShadow>(entity);
        auto lightVolumeOpt = IREntity::getComponentOptional<C_CanvasLightVolume>(entity);
        const C_CanvasSunShadow *shadow =
            shadowOpt.has_value() ? shadowOpt.value() : mainCanvasSunShadow_;
        const C_CanvasLightVolume *lightVolume =
            lightVolumeOpt.has_value() ? lightVolumeOpt.value() : mainCanvasLightVolume_;

        canvasTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
        canvasTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        ao.getTexture()->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        // Texture/image unit layout (must match GLSL + MSL):
        //   3: paletteLUT (sampler2D)
        //   4: canvasSunShadow (image2D, R/O)
        //   5: lightVolume (sampler3D)
        // Metal flattens the sampler and image namespaces into a
        // shared setTexture slot space, so all three slots must be
        // unique across both kinds.
        paletteLUT_->bind(3);
        // Metal requires every setTexture slot to be populated (it does not
        // default-bind a null slot). `shadow` is the per-canvas component when
        // present, else the main canvas's as an inert placeholder — the main canvas
        // always carries C_CanvasSunShadow when LIGHTING_TO_TRIXEL is registered.
        IR_ASSERT(
            shadow != nullptr,
            "Metal requires slot 4 bound — main canvas must carry C_CanvasSunShadow "
            "when LIGHTING_TO_TRIXEL iterates"
        );
        if (shadow != nullptr) {
            shadow->getTexture()->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        }
        // `getReadTexture()` returns whichever ping-pong texture
        // the GPU light-volume producer last wrote to, so this
        // sampler always sees the latest dilation result.
        if (lightVolume != nullptr) {
            lightVolume->getReadTexture()->bind(5);
            // Winning-light ID volume (image unit 7, #2318). Bound every tick so
            // Metal's slot table is populated; only fetched on the has-SPOT
            // path. Stays resident across the per-axis dispatches below (they
            // never rebind unit 7), so per-axis canvases get spot cones too.
            lightVolume->getIdReadTexture()
                ->bindAsImage(7, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        }
        // Entity-id image (unit 6, R/O): the lighting shader reads it ONLY to
        // recover the fog cut-face flag (bit 29) and force those faces fully lit
        // (#2124 lit-cross-section follow-up). Bound every tick so Metal's slot
        // table is populated; the per-axis dispatch below leaves it resident and
        // its perAxisRoute != 0 skips the read.
        canvasTextures.getTextureEntityIds()
            ->bindAsImage(6, TextureAccess::READ_ONLY, TextureFormat::RG32UI);
        frameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataLightingToTrixel);
        voxelFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataVoxelToCanvas);
        sunFrameDataBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_FrameDataSun);
        lightVolumeParamsBuf_->bindBase(BufferTarget::UNIFORM, kBufferIndex_LightVolumeParams);
        // Light list (SSBO slot 4) for the spot-cone factor (#2318). SSBO and
        // image bindings are independent namespaces on both backends, so slot 4
        // here does not collide with the image-unit-4 sun-shadow texture. Stays
        // resident across the per-axis dispatches (they never rebind slot 4).
        lightSourceBuf_->bindBase(BufferTarget::SHADER_STORAGE, kBufferIndex_LightSourceBuffer);
        // Sun-depth map (slot 28) for the opt-in detached world-receive path
        // (#1576 P4b-2). Bound every tick — the shader declares the SSBO
        // unconditionally (Metal kernel arg); only a world-placed detached solid
        // reads it. Persists across the per-axis lighting dispatches below (they
        // only rebind the colour/dist/AO/sun-shadow images).
        if (sunShadowDepthMap_ != nullptr) {
            sunShadowDepthMap_->bindBase(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_SunShadowDepthMap
            );
        }

        const int groupsX = IRMath::divCeil(canvasTextures.size_.x, kLightingToTrixelGroupSize);
        const int groupsY = IRMath::divCeil(canvasTextures.size_.y, kLightingToTrixelGroupSize);
        IRRender::device()->dispatchCompute(groupsX, groupsY, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);

        // Smooth camera Z-yaw (#1311): apply lighting to each per-axis voxel
        // canvas (AO x sun-shadow x face Lambert + shared world light volume) so
        // the framebuffer scatter composites LIT colours while rotating. Only
        // the main canvas allocates per-axis canvases, and it always carries
        // sun-shadow, so `shadow` is non-null on this path.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated() && shadow != nullptr) {
            dispatchPerAxisLighting(*perAxisCanvases_, canvasTextures, ao, *shadow);
        }
    }

    // Light each per-axis canvas in place. The palette LUT (3) and the shared
    // light volume (5) plus all four UBOs stay bound from the main pass; only the
    // per-axis colour/distance/AO/sun-shadow images and the perAxisRoute selector
    // change. perAxisRoute is restored to 0 on exit.
    void dispatchPerAxisLighting(
        C_PerAxisTrixelCanvases &axes,
        const C_TriangleCanvasTextures &mainTextures,
        const C_CanvasAOTexture &mainAO,
        const C_CanvasSunShadow &mainShadow
    ) {
        // perAxisRoute is a boolean route flag on the lighting path (any nonzero
        // = per-axis canvas); the shader recovers the axis per-pixel from faceId,
        // NOT from this field — distinct from stage-1's 1/2/3 = X/Y/Z axis selector.
        const int kPerAxisRoute = 1;
        voxelFrameDataBuf_
            ->subData(offsetof(FrameDataVoxelToCanvas, perAxisRoute_), sizeof(int), &kPerAxisRoute);
        // Recover world-pos with the SAME #1431-capped lattice density the store
        // wrote (perAxisCellToWorld3D reads voxelRenderOptions.y); restored below.
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRPrefab::PerAxisCanvas::subdivisionDensity()
        );
        // #2256: dispatch indirectly over only each axis's OCCUPIED cells
        // (compacted by the STAGE_1 per-axis pre-pass) instead of sweeping the
        // full worst-case per-axis grid. Each kernel recovers its cell from the
        // compacted list (slot 25) and reads visibleCount for its 1-D bound guard
        // from the indirect-args region (slot 26).
        Buffer *cellCompacted = axes.cellCompacted_.second;
        Buffer *cellIndirect = axes.cellIndirect_.second;
        const int regionStride = axes.cellRegionStride_;
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            auto &tex = axes.axes_[axis];
            tex.colors_.second->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
            tex.distances_.second->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
            tex.ao_.second->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            tex.sunShadow_.second->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
            cellCompacted->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisCellCompacted,
                static_cast<std::ptrdiff_t>(axis) * regionStride *
                    static_cast<int>(sizeof(std::uint32_t)),
                static_cast<size_t>(regionStride) * sizeof(std::uint32_t)
            );
            cellIndirect->bindRange(
                BufferTarget::SHADER_STORAGE,
                kBufferIndex_PerAxisCellIndirect,
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes,
                kPerAxisCellIndirectStrideBytes
            );
            IRRender::device()->dispatchComputeIndirect(
                cellIndirect,
                static_cast<std::ptrdiff_t>(axis) * kPerAxisCellIndirectStrideBytes +
                    kPerAxisCellDispatchArgsOffsetBytes
            );
        }
        // One barrier after the 3 independent per-axis dispatches (each axis
        // writes its own colour image texture in place — disjoint outputs, so
        // dispatch order doesn't matter) so they overlap on the GPU instead of
        // serializing per axis (#1311).
        IRRender::device()->memoryBarrier(BarrierType::SHADER_IMAGE_ACCESS);
        // #2334: relight the overflow entries the C1 lane appended albedo-only,
        // at their recovered world pos, while the sun-depth map (slot 28) + light
        // volume are still bound from the cell pass above. Switches the compute
        // program, so restore the lighting program for any remaining per-canvas
        // ticks this frame (beginTick's program_->use() runs once per frame).
        dispatchOverflowLighting(axes);
        program_->use();
        const int kSingleCanvasRoute = 0;
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, perAxisRoute_),
            sizeof(int),
            &kSingleCanvasRoute
        );
        // Restore the uncapped density for downstream single-canvas passes.
        IRPrefab::PerAxisCanvas::setUboSubdivisionDensity(
            voxelFrameDataBuf_,
            IRRender::getVoxelRenderEffectiveSubdivisions()
        );
        // Restore slots 25/26 to the voxel-compaction buffers (#1961/#2256) the
        // per-axis loop above borrowed via bindRange — see the restore-slots
        // note in system_compute_voxel_ao.hpp for the corruption mode this avoids.
        IRPrefab::PerAxisCanvas::restoreVoxelCompactionSlots(voxelCompactedBuf_, voxelIndirectBuf_);
        // Restore the main-canvas image bindings the loop overwrote. This is the
        // critical one: LIGHTING_TO_TRIXEL is the last image-binding compute stage,
        // so without this the freed per-axis textures linger in the persistent
        // Metal image-binding table and dangle when release() frees them at the
        // next cardinal frame — the #1311 mid-rotation crash. Same restore
        // discipline as the other per-axis lighting passes.
        mainTextures.getTextureColors()
            ->bindAsImage(0, TextureAccess::READ_WRITE, TextureFormat::RGBA8);
        mainTextures.getTextureDistances()
            ->bindAsImage(1, TextureAccess::READ_ONLY, TextureFormat::R32I);
        mainAO.getTexture()->bindAsImage(2, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
        mainShadow.getTexture()->bindAsImage(4, TextureAccess::READ_ONLY, TextureFormat::RGBA8);
    }

    // #2334 (epic #2331 C2): relight the view-visibility overflow entries the C1
    // (#2333) lane appended albedo-only. A bounded compute dispatch over the
    // overflow list recovers each entry's world pos + face normal and rewrites
    // its stored colour with the same world sample the per-axis cells got
    // (sun cascade + light volume + Lambert, AO = 1.0); the unchanged framebuffer
    // scatter then draws LIT slivers. Reuses every resource the cell pass bound —
    // sun-depth map (28), light volume (5), light list (4), and the four UBOs —
    // adding only the overflow scratch at kBufferIndex_OverflowLightingScratch (a
    // buffer slot dead during lighting; slot 28 holds the sun-depth map). Runs
    // only while rotating (per-axis canvases allocated), so yaw-0 is byte-identical.
    void dispatchOverflowLighting(C_PerAxisTrixelCanvases &axes) {
        if (overflowLightingDisabled_ || axes.overflowCap_ <= 0 ||
            axes.winnerIds_.second == nullptr) {
            return;
        }
        overflowLightingProgram_->use();
        // The kernel indexes entries + the ctrl-block count via overflowScratchLayout
        // read from the voxel-frame UBO (slot 7). Only VOXEL_TO_TRIXEL_STAGE_1 sets
        // that field, and this system re-authors the shared UBO per canvas (#1558),
        // so it reads back zero here — republish it from the canvas's own scratch
        // offsets before the dispatch (matches the ivec4 order the store uploads).
        const ivec4 overflowLayout(
            axes.viewMaskBaseUints_,
            axes.ctrlBaseUints_,
            axes.entriesBaseUints_,
            axes.overflowCap_
        );
        voxelFrameDataBuf_->subData(
            offsetof(FrameDataVoxelToCanvas, overflowScratchLayout_),
            sizeof(ivec4),
            &overflowLayout
        );
        // Whole-buffer bind; the kernel offsets into it via overflowScratchLayout.
        axes.winnerIds_.second->bindBase(
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_OverflowLightingScratch
        );
        // Sized to the worst-case cap; threads past the live entry count
        // early-return (the count lives in the scratch ctrl block). The cap can
        // exceed 1024 groups, so wrap into the 2-D group grid the shader flattens.
        const int groupCount = IRMath::divCeil(axes.overflowCap_, kOverflowLightingGroupSize);
        const ivec2 grid = voxelDispatchGridForCount(groupCount);
        IRRender::device()->dispatchCompute(grid.x, grid.y, 1);
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    void beginTick() {
        program_->use();
        // Resolve the baked sun-depth map once (created by BAKE_SUN_SHADOW_MAP,
        // registered ahead of LIGHTING_TO_TRIXEL). Lazy single-init: the resolve
        // is deferred to beginTick so BAKE_SUN_SHADOW_MAP has already registered
        // the resource by the time LIGHTING_TO_TRIXEL first runs. getNamedResource
        // asserts on a missing key — a pipeline without the bake is a config error,
        // not a graceful fallback (all 13 registered pipelines co-register both
        // systems under the same !noLighting_ guard).
        if (sunShadowDepthMap_ == nullptr) {
            sunShadowDepthMap_ = IRRender::getNamedResource<Buffer>("SunShadowDepthMap");
        }
        // Modes above SHADOW (PER_AXIS_ID / PER_AXIS_ORIGIN / UNLIT) belong to
        // other passes — the lighting shader's overlay fallback would otherwise
        // misread them as SHADOW. UNLIT additionally disables the modulation
        // itself so raw rasterized colors flow through (#1457 instrumentation).
        const int overlayMode = static_cast<int>(IRRender::getDebugOverlay());
        frameData_.lightingEnabled_ =
            overlayMode == static_cast<int>(IRRender::DebugOverlayMode::UNLIT) ? 0 : 1;
        frameData_.lightVolumeEnabled_ = 1;
        frameData_.debugOverlayMode_ =
            overlayMode <= static_cast<int>(IRRender::DebugOverlayMode::SHADOW) ? overlayMode : 0;
        frameData_.hdrEnabled_ = IRRender::getHDREnabled() ? 1 : 0;
        frameData_.exposure_ = IRRender::getExposure();
        frameData_.skyIntensity_ = IRRender::getSkyIntensity();
        const vec3 sc = IRRender::getSkyColor();
        frameData_.skyColor_ = vec4(sc, 0.0f);
        frameDataBuf_->subData(0, sizeof(FrameDataLightingToTrixel), &frameData_);

        // Resolve the main canvas + its per-axis voxel canvases (#1311), plus
        // its voxel-frame inputs and sun-shadow / light-volume placeholders for
        // the relaxed multi-lit-canvas archetype (#1558). beginTick lookups are
        // once-per-frame, not the per-entity footgun.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        mainCanvasSunShadow_ = nullptr;
        mainCanvasLightVolume_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis =
                IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(perAxisCanvasEntity_);
            if (perAxis.has_value()) {
                perAxisCanvases_ = perAxis.value();
            }
            // Sun-shadow + light-volume placeholders are LIGHTING-specific (AO
            // needs neither), so resolve them here; the shared 3-component voxel-
            // frame inputs come from resolveMainCanvasVoxelFrameInputs below.
            auto shadow = IREntity::getComponentOptional<C_CanvasSunShadow>(perAxisCanvasEntity_);
            auto lv = IREntity::getComponentOptional<C_CanvasLightVolume>(perAxisCanvasEntity_);
            if (shadow.has_value())
                mainCanvasSunShadow_ = shadow.value();
            if (lv.has_value())
                mainCanvasLightVolume_ = lv.value();
        }
        resolveMainCanvasVoxelFrameInputs(
            perAxisCanvasEntity_,
            &mainCanvasTextures_,
            &mainVoxelPool_,
            &mainCanvasRotation_
        );
    }

    // Restore the main world canvas's voxel frame data so FOG_TO_TRIXEL /
    // TRIXEL_TO_FRAMEBUFFER (which run after lighting and read the shared voxel
    // UBO) see the world frame even when a detached re-voxelize canvas authored
    // its own above (#1558). Byte-identical render output for single-lit-canvas
    // scenes (cullIso, the only field buildVoxelFrameData omits, is unused
    // downstream).
    void endTick() {
        restoreMainCanvasVoxelFrame(
            scratchVoxelFrame_,
            voxelFrameDataBuf_,
            mainCanvasTextures_,
            mainVoxelPool_,
            mainCanvasRotation_
        );
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "LightingToTrixelProgram",
            std::vector{ShaderStage{IRRender::kFileCompLightingToTrixel, ShaderType::COMPUTE}}
        );
        // #2334: overflow-face relight kernel, dispatched at the tail of the
        // per-axis lighting (see dispatchOverflowLighting).
        IRRender::createNamedResource<ShaderProgram>(
            "LightOverflowFacesProgram",
            std::vector{ShaderStage{IRRender::kFileCompLightOverflowFaces, ShaderType::COMPUTE}}
        );
        IRRender::createNamedResource<Buffer>(
            "LightingToTrixelFrameData",
            nullptr,
            sizeof(FrameDataLightingToTrixel),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataLightingToTrixel
        );
        IRRender::createNamedResource<Texture2D>(
            "PaletteLUT_Nearest",
            TextureKind::TEXTURE_2D,
            256,
            16,
            TextureFormat::RGBA8,
            TextureWrap::CLAMP_TO_EDGE,
            TextureFilter::NEAREST
        );
        IRRender::createNamedResource<Texture2D>(
            "PaletteLUT_Linear",
            TextureKind::TEXTURE_2D,
            256,
            16,
            TextureFormat::RGBA8,
            TextureWrap::CLAMP_TO_EDGE,
            TextureFilter::LINEAR
        );

        // Upload default LUT: cool-shadow (x=0) → full-white (x=255) gradient.
        // Same data for both filter variants; the difference is sampling mode.
        {
            std::array<std::uint8_t, 256 * 16 * 4> data{};
            for (int row = 0; row < 16; ++row) {
                for (int col = 0; col < 256; ++col) {
                    const float t = static_cast<float>(col) / 255.0f;
                    const std::size_t idx = static_cast<std::size_t>(row * 256 + col) * 4;
                    data[idx + 0] = static_cast<std::uint8_t>((0.15f + 0.85f * t) * 255.0f);
                    data[idx + 1] = static_cast<std::uint8_t>((0.20f + 0.80f * t) * 255.0f);
                    data[idx + 2] = static_cast<std::uint8_t>((0.35f + 0.65f * t) * 255.0f);
                    data[idx + 3] = 255u;
                }
            }
            IRRender::getNamedResource<Texture2D>("PaletteLUT_Nearest")
                ->subImage2D(
                    0,
                    0,
                    256,
                    16,
                    PixelDataFormat::RGBA,
                    PixelDataType::UNSIGNED_BYTE,
                    data.data()
                );
            IRRender::getNamedResource<Texture2D>("PaletteLUT_Linear")
                ->subImage2D(
                    0,
                    0,
                    256,
                    16,
                    PixelDataFormat::RGBA,
                    PixelDataType::UNSIGNED_BYTE,
                    data.data()
                );
        }

        // Relaxed archetype (#1558): C_CanvasSunShadow / C_CanvasLightVolume are
        // resolved per canvas via getComponentOptional in the tick (canvas-
        // iteration pattern), NOT required template params — so the detached
        // re-voxelize canvas (AO + directional sun + sky only, no sun-shadow /
        // light-volume) is lit too. The main canvas still carries both.
        SystemId systemId = registerSystem<
            LIGHTING_TO_TRIXEL,
            C_TriangleCanvasTextures,
            C_TrixelCanvasRenderBehavior,
            C_CanvasAOTexture>("LightingToTrixel");
        auto *p = getSystemParams<System<LIGHTING_TO_TRIXEL>>(systemId);
        p->program_ = IRRender::getNamedResource<ShaderProgram>("LightingToTrixelProgram");
        p->overflowLightingProgram_ =
            IRRender::getNamedResource<ShaderProgram>("LightOverflowFacesProgram");
        // A/B kill switch for the lit-vs-albedo overflow screenshots + GPU delta.
        p->overflowLightingDisabled_ = std::getenv("IR_OVERFLOW_LIGHTING_DISABLE") != nullptr;
        p->frameDataBuf_ = IRRender::getNamedResource<Buffer>("LightingToTrixelFrameData");
        p->voxelFrameDataBuf_ = IRRender::getNamedResource<Buffer>("SingleVoxelFrameData");
        p->sunFrameDataBuf_ = IRRender::getNamedResource<Buffer>("ComputeSunShadowFrameData");
        // LightVolumeParamsBuffer is created by COMPUTE_LIGHT_VOLUME,
        // which is registered ahead of LIGHTING_TO_TRIXEL in the render
        // pipeline; safe to look up at init time.
        p->lightVolumeParamsBuf_ = IRRender::getNamedResource<Buffer>("LightVolumeParamsBuffer");
        // LightSourceBuffer is also created by COMPUTE_LIGHT_VOLUME (registered
        // ahead of LIGHTING_TO_TRIXEL); safe to resolve here (#2318).
        p->lightSourceBuf_ = IRRender::getNamedResource<Buffer>("LightSourceBuffer");
        p->paletteLUT_ = IRRender::getNamedResource<Texture2D>("PaletteLUT_Nearest");
        IRRender::tagGpuStage(systemId, "lightingToTrixel");
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LIGHTING_TO_TRIXEL_H */
