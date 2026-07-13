#ifndef SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H
#define SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/common/components/component_world_transform.hpp>

#include <vector>

// Future work: for shape-only entities, the compute-to-canvas step could
// be eliminated entirely by evaluating SDFs directly in the fragment shader.
// The analytical iso-projected surface math from c_shapes_to_trixel.glsl is
// reusable for this purpose. See the Renderer Performance Overhaul plan,
// Phase 3.

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRSystem {

constexpr int kMaxEntityCanvasInstances = 512;

template <> struct System<ENTITY_CANVAS_TO_FRAMEBUFFER> {
    struct CanvasInstance {
        FrameDataTrixelToFramebuffer frameData_;
        const C_TriangleCanvasTextures *textures_;
    };

    // Per-frame instance list, gathered in tick and consumed in endTick.
    // System-owned state lives on System<N> (registerSystem member form), not
    // in function-local statics (#1520).
    std::vector<CanvasInstance> instances_;

    // Frame constants snapshotted once in beginTick (#1520). They were
    // re-queried per visible detached entity in the old per-entity tick even
    // though they are constant across the frame; the tick now reads them from
    // `this`. fbRes_ comes from the "mainFramebuffer" named entity, fetched
    // non-optionally in beginTick (the named entity is stood up at pipeline
    // init and never destroyed during the render loop — the same precondition
    // the former per-entity getComponent carried).
    vec2 fbRes_{};
    vec2 mainCanvasSize_{};
    // The EFFECTIVE camera iso offset (`getEffectiveCameraIso()`), NOT the raw
    // `getCameraPosition2DIso()`. Placement must pivot with the GRID/world content
    // it composites against: the main canvas (TRIXEL_TO_FRAMEBUFFER / voxel raster
    // / SDF) positions everything relative to the effective offset, which applies
    // the RotationPivotMode focus correction (#1352/#1942) so camera Z-yaw pivots
    // about the on-screen focus. Reading the raw offset here (the pre-#1944 state)
    // made detached entities pivot about the world origin while GRID pivoted about
    // the focus, so they drifted apart as the camera yawed — the canvas_stress
    // detached canary's rotation jitter. The two coincide at yaw 0 (effective ==
    // raw), so cardinal frames stay byte-identical. (The de-tile GATHER parity
    // below stays keyed to the entity's FIXED world iso, `entityIso`, not this
    // camera offset — unchanged, so no new #1256-class stripe risk.)
    vec2 effectiveCameraIso_{};
    vec2 cameraZoom_{};
    float visualYaw_ = 0.0f;
    // Game-pixel half of the anti-vibration decomposition — see
    // IRMath::cameraSubPixelOffsets (the same value TRIXEL_TO_FRAMEBUFFER uses
    // for the native-resolution main canvas). Derived from effectiveCameraIso_ /
    // cameraZoom_. For this UPSCALED detached canvas the raw game-px value is
    // sub-texel and must be snapped to the canvas texel grid before use (#1883,
    // jitter fix) — see the tick.
    vec2 isoPixelOffset_{};
    // Global voxel subdivision factor the SHARED framebuffer depth buffer runs
    // at this frame (the main world canvas + SDF floor encode depth as
    // worldDepth × effSub × 4). A world-placed detached canvas rasters its pool
    // at its OWN (possibly capped) sub, so its model-frame depth must be
    // rescaled into these units before it can depth-sort against world geometry
    // — the #1624 world-placed depth fix.
    int effectiveSub_ = 1;

    void beginTick() {
        instances_.clear();
        instances_.reserve(kMaxEntityCanvasInstances);

        // Hoist the frame constants out of the per-entity tick (#1520). The
        // "mainFramebuffer" named entity is stood up at pipeline init and never
        // destroyed during the render loop, so the non-optional getComponent
        // carries the same precondition the per-entity read did.
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        fbRes_ = vec2(framebuffer.getResolutionPlusBuffer());
        mainCanvasSize_ = IRRender::getMainCanvasSizeTrixels();
        effectiveCameraIso_ = IRRender::getEffectiveCameraIso();
        cameraZoom_ = IRRender::getCameraZoom();
        visualYaw_ = IRPrefab::Camera::getYaw();

        const IRMath::CameraSubPixelOffsets subPixelOffsets =
            IRMath::cameraSubPixelOffsets(effectiveCameraIso_, cameraZoom_, ivec2(1));
        isoPixelOffset_ = vec2(subPixelOffsets.framebufferGamePxOffset_);

        effectiveSub_ = IRRender::getVoxelRenderEffectiveSubdivisions();
    }

    void tick(const C_EntityCanvas &entityCanvas, const C_WorldTransform &worldTransform) {
        if (!entityCanvas.visible_ || entityCanvas.canvasEntity_ == IREntity::kNullEntity ||
            static_cast<int>(instances_.size()) >= kMaxEntityCanvasInstances) {
            return;
        }

        auto texOpt =
            IREntity::getComponentOptional<C_TriangleCanvasTextures>(entityCanvas.canvasEntity_);
        if (!texOpt.has_value())
            return;
        auto *canvasTextures = texOpt.value();

        // Density compensation (#2043, Option A). The detached canvas rastered its
        // pool at `renderedSubdivisions_` (cubeSub) — possibly ABOVE the global
        // effSub when the #1570-D2 footprint cap left room on a generously-sized
        // canvas. The composite must mirror what the main world canvas does with
        // effSub (`canvasZoomLevel_ = cameraZoom / effSub`,
        // system_trixel_to_framebuffer.hpp): divide cubeSub out of the quad
        // scale + the de-tile gather density so apparent on-screen size and gather
        // sampling are functions of camera zoom × world extent ONLY, never of the
        // internal raster resolution. Without it the on-screen extent is
        // `footprint × cubeSub × fbRes × zoom / mainCanvasSize` — the cubeSub factor
        // oversizes the solid (the main canvas divides it back out; this path never
        // got the mirror) — and the gather minifies the cubeSub-density tiles
        // against a NEAREST 1:1-assuming parity reconstruction (see-through gaps).
        // cubeSub == 0 (no voxel raster this frame: a pure SDF/text overlay canvas)
        // or 1 ⇒ densityZoom == cameraZoom and every divide/scale below is identity,
        // so the existing tight-canvas / cardinal / overlay cases stay byte-identical.
        const float cubeSubDensity =
            static_cast<float>(IRMath::max(canvasTextures->renderedSubdivisions_, 1));
        const vec2 densityZoom = cameraZoom_ / vec2(cubeSubDensity);

        // The detached canvas texture is rasterized camera-yaw-zeroed in
        // the entity's own model space (buildVoxelFrameData's detached
        // branch), so its de-tile gather phase is keyed to the entity's
        // FIXED world iso position — constant under camera yaw, reused
        // below as `-entityIso` for the gather's `cameraTrixelOffset_`
        // parity (unchanged, so no new #1256-class stripe risk). The
        // screen PLACEMENT, by contrast, must orbit with the rotating
        // world: project the world position under the camera's continuous
        // Z-yaw (#1500) and add the EFFECTIVE camera offset (the pivot
        // correction), exactly as the world / SDF content does
        // (system_shapes_to_trixel via pos3DtoPos2DIsoYawed + the effective
        // offset). The two coincide at yaw == 0, so cardinal frames stay
        // byte-identical.
        vec2 entityIso = pos3DtoPos2DIso(worldTransform.translation_);
        vec2 entityIsoPlacement = pos3DtoPos2DIsoYawed(worldTransform.translation_, visualYaw_);

        ivec2 mainCanvasSizeI = ivec2(mainCanvasSize_);
        vec2 canvasOriginZ1 = vec2(trixelOriginOffsetZ1(mainCanvasSizeI));
        vec2 entityOnMainCanvas =
            canvasOriginZ1 + IRMath::floor(effectiveCameraIso_) + entityIsoPlacement;
        vec2 normalizedPos = entityOnMainCanvas / mainCanvasSize_;

        vec2 entityAPos = vec2(normalizedPos.x - 0.5f, 0.5f - normalizedPos.y);
        // Camera sub-pixel offset, SNAPPED to the detached canvas's texel grid
        // (#1883), re-expressed for the #2043 density divide. The detached canvas
        // is an upscaled pixel-art texture — one of its texels spans `texelFb`
        // framebuffer px: one main-canvas trixel (`fbRes × zoom / mainCanvasSize`)
        // at cubeSub == 1, and 1/cubeSub of that once the quad scale below divides
        // cubeSub out (the canvas rasters cubeSub texels per base unit, so each
        // texel is correspondingly finer). The snap target is the actual canvas
        // texel, not the base trixel, so it carries the `/cubeSubDensity` divide.
        // `isoPixelOffset_` is an integer GAME-px offset, which is texel-aligned
        // for the native-resolution main canvas (TRIXEL_TO_FRAMEBUFFER) but
        // SUB-texel for this upscaled canvas: applying it raw shifts the gather's
        // sample point a fraction of a texel, so the silhouette re-rasterizes every
        // frame and the solid shimmers / jitters under a smooth camera pan (the
        // world content stays put because it is texel-aligned). Snapping the offset
        // to whole texels makes the detached move in clean texel steps — no
        // sub-texel resample, so no shimmer — while still tracking the world to
        // within half a texel. At an integer camera offset isoPixelOffset_ is 0, so
        // the snap is a no-op and static / cardinal frames stay byte-identical (as
        // does the whole expression at cubeSub == 1).
        const vec2 texelFb = fbRes_ * cameraZoom_ / mainCanvasSize_ / vec2(cubeSubDensity);
        const vec2 texelSteps = isoPixelOffset_ / texelFb;
        const vec2 snappedCamOffset =
            vec2(IRMath::roundHalfUp(texelSteps.x), IRMath::roundHalfUp(texelSteps.y)) * texelFb;
        vec2 entityFbCenter = vec2(
            fbRes_.x * 0.5f + snappedCamOffset.x + entityAPos.x * fbRes_.x * cameraZoom_.x,
            fbRes_.y * 0.5f + snappedCamOffset.y + entityAPos.y * fbRes_.y * cameraZoom_.y
        );
        // Z1 origin-anchor compensation: the raster anchors the model origin at
        // trixelOriginOffsetZ1(canvasSize) = canvasCenter + (-1,-1) texels, but
        // this quad centers the CANVAS on the entity's iso screen position — so
        // the content renders one canvas texel left+up of its GRID twin, an
        // error that scales with zoom on screen (~2*zoom px in x, ~zoom px in
        // y). Shift the quad one texel so the model origin lands exactly where
        // the world path puts the entity's iso position.
        entityFbCenter += texelFb;

        vec2 entityScale = vec2(entityCanvas.canvasSize_) / mainCanvasSize_;
        // Placement only: the composite places each detached canvas
        // texture at the entity's iso position, axis-aligned. A
        // DETACHED entity's full SO(3) rotation is baked into the
        // canvas texture itself by the voxel emit (T-295, via
        // PROPAGATE_CANVAS_ROTATION → C_CanvasLocalRotation →
        // VOXEL_TO_TRIXEL_STAGE_1), so the composite TRS no longer
        // applies any rotation.
        //
        // WORLD-PLACED (default) vs SCREEN-LOCKED OVERLAY (opt-out) — #1624.
        // By DEFAULT every detached canvas — DETACHED and DETACHED_REVOXELIZE
        // alike — depth-participates: `distanceOffset_` (below) is the entity's
        // world iso depth (x+y+z), so the canvas's pool-centered trixel
        // distances land in the shared world depth band and depth-sort against
        // GRID solids, the floor, and each other on the GRID convention (P4b-1
        // mechanics, #1576). Re-voxelize solids additionally receive (P4b-2)
        // and cast (P4b-3) world sun-shadow + light-volume at that world pos;
        // the forward-scatter DETACHED deform has no faithful world-pos
        // recovery, so it depth-sorts only (see buildVoxelFrameData).
        // `C_EntityCanvas::screenLocked_` is the explicit opt-OUT for genuine
        // overlay cases (HUD props, billboards, floating showcases): the model
        // Z is a constant 0 and `distanceOffset_` stays 0, so the canvas
        // composites at the SAME fixed framebuffer depth regardless of the
        // entity's world iso depth — a cheap 2D overlay at the iso screen
        // position, byte-identical to the pre-#1624 default (the epic #1553
        // decision-1 / #1582 Option B contract, superseded as the default by
        // #1624 — see docs/design/detached-canvas-depth-default.md).
        // The quad SCALE divides cubeSub out via `densityZoom` (= cameraZoom /
        // cubeSub) so the on-screen extent is `footprint × fbRes × zoom /
        // mainCanvasSize` — independent of the internal raster resolution and equal
        // to the GRID twin's apparent size (#2043, Option A). Only the scale carries
        // the divide; `entityFbCenter` (the placement / translate) keeps full
        // cameraZoom so the canvas CENTER still tracks the entity's world iso
        // position. The cube sits at the canvas center (the pool is centered), so
        // shrinking the quad about entityFbCenter reduces the apparent size without
        // moving the solid. At cubeSub == 1 densityZoom == cameraZoom (byte-identical).
        mat4 model = translate(mat4(1.0f), vec3(entityFbCenter, 0.0f));
        model = scale(
            model,
            vec3(
                fbRes_.x * densityZoom.x * entityScale.x,
                fbRes_.y * densityZoom.y * entityScale.y,
                1.0f
            )
        );

        FrameDataTrixelToFramebuffer fd{};
        fd.mpMatrix_ = calcProjectionMatrix(fbRes_) * model;
        fd.canvasZoomLevel_ = densityZoom;
        // The de-tile gather parity anchor scales with the raster density, the
        // direct mirror of the main canvas's `cameraTrixelOffset_ *= effSub`
        // (system_trixel_to_framebuffer.hpp). `-entityIso` is in base world
        // units; the canvas rasters cubeSub texels per base unit, so the
        // `trixelOriginModifier` parity bit (computed from `floor(canvasOffset)` in
        // the gather) must see the offset in canvas-texel units. At cubeSub == 1
        // this is unchanged (byte-identical).
        fd.cameraTrixelOffset_ = -entityIso * vec2(cubeSubDensity);
        fd.textureOffset_ = vec2(0.0f);
        // Composite depth (#1576 P4b-1 mechanics, default since #1624). By
        // default, add the entity's WORLD iso depth so `model rawDist +
        // distanceOffset` reproduces the shared world trixelDistances value:
        // the detached canvas rasters its pool in the pool-centered MODEL
        // frame, so its rawDist is model-relative; pos3DtoDistance is linear,
        // so for an integer world translation the sum equals
        // pos3DtoDistance(world cell) exactly — the GRID-equivalence the
        // composite depth-sorts on (see detached_world_depth_test).
        // roundVec3HalfUp is the per-axis cell rounding GRID re-voxelize uses
        // (IRPrefab::GridRotation::worldCellForGridVoxel), so CPU and the
        // world voxel rasterizer classify the entity cell identically. With
        // screenLocked_ this stays 0 (the overlay opt-out above).
        // World-placed depth must land in the SHARED framebuffer depth units —
        // the main world canvas + SDF floor encode depth as
        // worldDepth × effSub × 4 (encodeDepthWithFace's ×4 face-bit shift in
        // ir_iso_common). This canvas rastered its pool in MODEL space at its
        // own renderedSubdivisions_ (≤ effSub when the #1570 D2 cap fired), so
        // its texture rawDist is modelDepth × cubeSub × 4. To depth-sort against
        // world geometry the composite (a) rescales rawDist by effSub / cubeSub
        // (depthScale, carried in the otherwise-unused
        // effectiveSubdivisionsForHover_.y and applied in f_trixel_to_framebuffer)
        // and (b) adds the entity's world iso depth at the same effSub × 4 scale.
        // The #1624 default left the offset in raw world units — under-scaled by
        // effSub × 4 — which sank world-placed solids behind the floor as zoom
        // (effSub) grew, clipping their lower faces at a horizontal line.
        // screenLocked overlays keep depthScale 1 + offset 0 (byte-identical to
        // the pre-#1624 fixed-depth overlay); a canvas that did not raster a
        // voxel pool this frame (renderedSubdivisions_ == 0, e.g. pure SDF/text
        // overlay) keeps the pre-existing raw offset.
        const int cubeSub = canvasTextures->renderedSubdivisions_;
        // Foreground depth priority (#1958 two-tier partition; the #1884 Bug-A
        // fix). A world-placed canvas with depthPriority_ set renders
        // unconditionally in front of the floor / any world geometry below it,
        // independent of world extent. screenLocked_ overlays already sit at a
        // fixed near depth, so priority is meaningless there (and stays the
        // byte-identical overlay path).
        const bool foregroundPriority =
            !entityCanvas.screenLocked_ && entityCanvas.depthPriority_ != 0;
        float depthScale = 1.0f;
        int compositeDistanceOffset = 0;
        if (!entityCanvas.screenLocked_) {
            // Model-frame rawDist → shared framebuffer depth units. Foreground and
            // world-placed alike rescale by effSub / cubeSub so the canvas's own
            // (possibly #1570-D2-capped) raster self-occludes at the right
            // resolution; they differ only in the OFFSET folded in below.
            if (cubeSub >= 1) {
                depthScale = static_cast<float>(effectiveSub_) / static_cast<float>(cubeSub);
            }
            if (foregroundPriority) {
                // Per-trixel priority tiers (#1960): the gather
                // (f_trixel_to_framebuffer) now CENTERS a foreground fragment on
                // its resolved tier's center (depthForegroundTierCenter) and clamps
                // into that tier's disjoint sub-range, so the CPU offset is no
                // longer the band-center carrier — leave it 0. (Pre-#1960 this set
                // kDepthForegroundBandCenter; centering moved into the shader so the
                // per-trixel-override tier can re-center a fragment bumped above the
                // entity tier.) The world iso-depth is still intentionally NOT added
                // — priority OVERRIDES world depth ordering; only the screen
                // PLACEMENT above tracks the world.
                compositeDistanceOffset = 0;
            } else {
                // Continuous-yaw world depth: place the solid by the SAME
                // yawedIsoDistance the SDF floor + per-axis voxel scatter sort on
                // (IRMath::pos3DtoDistanceYawed, the CPU twin), so a world-placed
                // detached solid co-sorts against the floor at EVERY yaw — not
                // just cardinals. The cardinal-only pos3DtoDistance offset stayed
                // fixed while the continuous floor drifted under rotation, so the
                // solid clipped behind the floor between cardinals (#1884's
                // wrong-winner root). Round the translation to its world cell
                // first (the GRID re-voxelize cell classification this co-sorts
                // with); at a cardinal pose pos3DtoDistanceYawed(cell, 0) ==
                // pos3DtoDistance(cell) exactly, so the cardinal fast path stays
                // byte-identical.
                const int worldDepth = pos3DtoDistanceYawed(
                    vec3(roundVec3HalfUp(worldTransform.translation_)),
                    visualYaw_
                );
                compositeDistanceOffset =
                    (cubeSub >= 1) ? worldDepth * effectiveSub_ * kDepthEncodeShift : worldDepth;
            }
        }
        fd.distanceOffset_ = compositeDistanceOffset;
        // Per-fragment depth tier (#1960). The gather resolves
        // tier = max(perEntityTier /*this*/, perTrixelTier /*id carrier*/). A
        // non-zero depthPriority_ selects the single ENTITY-foreground tier (1);
        // the per-trixel-override tier (2) is authored per voxel, never here. tier 0
        // (world) is clamped OUT of the reserved band, byte-identical to #1958.
        fd.depthPriorityMode_ = foregroundPriority ? 1 : 0;
        // No-priority perf fast-path (#2155): forward the child canvas's stamp so
        // a rotating/static detached solid that carries per-trixel priority (via
        // the re-voxelize source set, #2023) still decodes its tier, while a
        // no-priority detached solid takes the fast path. This composite never
        // hover-picks (mouseHoveredTriangleIndex below is off-screen), so the flag
        // is the sole gate here.
        fd.anyPerTrixelPriority_ = canvasTextures->anyPerTrixelPriority_;
        fd.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
        fd.effectiveSubdivisionsForHover_ = vec2(1.0f, depthScale);
        fd.showHoverHighlight_ = 0.0f;

        CanvasInstance inst{};
        inst.frameData_ = fd;
        inst.textures_ = canvasTextures;
        instances_.push_back(inst);
    }

    void endTick() {
        if (instances_.empty())
            return;

        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        framebuffer.bindFramebuffer();

        auto *frameDataBuffer = IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData");
        IRRender::getNamedResource<VAO>("QuadVAO")->bind();
        IRRender::device()->setPolygonMode(PolygonMode::FILL);

        // Gather pass: blit each detached canvas via the single-parity
        // de-tile gather. A rotating detached entity's voxels are
        // re-voxelized into this single canvas in its own model frame by
        // VOXEL_TO_TRIXEL_STAGE_1 (detached re-voxelize, #1555–#1559), so
        // the gather composites the full SO(3) solid plus any SDF / text;
        // at a cardinal/identity pose it renders byte-identically.
        IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram")->use();
        // Depth-participation (#1957, hardening #1624). The composite depth-TESTS
        // each detached canvas against the world depth the gather wrote (so world
        // geometry occludes a detached solid) and WRITES its winning fragment's
        // gl_FragDepth (f_trixel_to_framebuffer.glsl) so a detached solid occludes
        // the floor and other detached instances. The write already happened on
        // both backends — Metal's depth-stencil state at composite time is the
        // default-ENABLED state (g_runtime().depthWriteEnabled_ defaults true,
        // metal_runtime.cpp), OpenGL's glDepthMask defaults GL_TRUE, and the only
        // togglers (SPRITE_TO_SCREEN, debug overlay) run AFTER this pass and
        // restore. #1957 makes that implicit write EXPLICIT (setDepthWrite(true)
        // here = glDepthMask(GL_TRUE) on GL, depthWriteEnabled_=true on Metal) and
        // restores after, so a future pass that leaves depth-write off upstream
        // can't silently regress the composite — guarded by the canvas_stress
        // --depth-probe-assert canary (see depth_probe.hpp). All instances write
        // depth, including screen-locked overlays, keeping the screenLocked_ path
        // byte-identical to master (the #1957 acceptance).
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);
        for (auto &inst : instances_) {
            frameDataBuffer->subData(0, sizeof(FrameDataTrixelToFramebuffer), &inst.frameData_);
            inst.textures_->bind(0, 1, 2);
            IRRender::device()->drawElements(
                DrawMode::TRIANGLES,
                IRShapes2D::kQuadIndicesLength,
                IndexType::UNSIGNED_SHORT
            );
        }
        // Restore the pipeline's default depth-write so later passes are
        // unaffected — matches the setDepthWrite(true) / setDepthTest(true)
        // restore the sprite + debug-overlay passes use.
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);

        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    static SystemId create() {
        SystemId s = registerSystem<ENTITY_CANVAS_TO_FRAMEBUFFER, C_EntityCanvas, C_WorldTransform>(
            "EntityCanvasToFramebuffer"
        );
        IRRender::tagGpuStage(s, "entityCanvasToFb");
        return s;
    }

    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        return ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H */
