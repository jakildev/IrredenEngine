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
    vec2 cameraIso_{};
    vec2 cameraZoom_{};
    float visualYaw_ = 0.0f;
    // Game-pixel half of the anti-vibration decomposition — see
    // IRMath::cameraSubPixelOffsets. Matches the TRIXEL_TO_FRAMEBUFFER call
    // site so detached canvases composite onto the same sub-pixel-snapped grid
    // as the main canvas. Derived from cameraIso_ / cameraZoom_.
    vec2 isoPixelOffset_{};

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
        cameraIso_ = IRRender::getCameraPosition2DIso();
        cameraZoom_ = IRRender::getCameraZoom();
        visualYaw_ = IRPrefab::Camera::getYaw();

        const IRMath::CameraSubPixelOffsets subPixelOffsets =
            IRMath::cameraSubPixelOffsets(cameraIso_, cameraZoom_, ivec2(1));
        isoPixelOffset_ = vec2(subPixelOffsets.framebufferGamePxOffset_);
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

        // The detached canvas texture is rasterized camera-yaw-zeroed in
        // the entity's own model space (buildVoxelFrameData's detached
        // branch), so its de-tile gather phase is keyed to the entity's
        // FIXED world iso position — constant under camera yaw, reused
        // below as `-entityIso` for the gather's `cameraTrixelOffset_`
        // parity (unchanged, so no new #1256-class stripe risk). The
        // screen PLACEMENT, by contrast, must orbit with the rotating
        // world: project the world position under the camera's continuous
        // Z-yaw (#1500), exactly as the world / SDF content does
        // (system_shapes_to_trixel via pos3DtoPos2DIsoYawed). The two
        // coincide at yaw == 0, so cardinal frames stay byte-identical.
        vec2 entityIso = pos3DtoPos2DIso(worldTransform.translation_);
        vec2 entityIsoPlacement = pos3DtoPos2DIsoYawed(worldTransform.translation_, visualYaw_);

        ivec2 mainCanvasSizeI = ivec2(mainCanvasSize_);
        vec2 canvasOriginZ1 = vec2(trixelOriginOffsetZ1(mainCanvasSizeI));
        vec2 entityOnMainCanvas = canvasOriginZ1 + IRMath::floor(cameraIso_) + entityIsoPlacement;
        vec2 normalizedPos = entityOnMainCanvas / mainCanvasSize_;

        vec2 entityAPos = vec2(normalizedPos.x - 0.5f, 0.5f - normalizedPos.y);
        vec2 entityFbCenter = vec2(
            fbRes_.x * 0.5f + isoPixelOffset_.x + entityAPos.x * fbRes_.x * cameraZoom_.x,
            fbRes_.y * 0.5f + isoPixelOffset_.y + entityAPos.y * fbRes_.y * cameraZoom_.y
        );

        vec2 entityScale = vec2(entityCanvas.canvasSize_) / mainCanvasSize_;
        // Placement only: the composite places each detached canvas
        // texture at the entity's iso position, axis-aligned. A
        // DETACHED entity's full SO(3) rotation is baked into the
        // canvas texture itself by the voxel emit (T-295, via
        // PROPAGATE_CANVAS_ROTATION → C_CanvasLocalRotation →
        // VOXEL_TO_TRIXEL_STAGE_1), so the composite TRS no longer
        // applies any rotation.
        mat4 model = translate(mat4(1.0f), vec3(entityFbCenter, 0.0f));
        model = scale(
            model,
            vec3(
                fbRes_.x * cameraZoom_.x * entityScale.x,
                fbRes_.y * cameraZoom_.y * entityScale.y,
                1.0f
            )
        );

        FrameDataTrixelToFramebuffer fd{};
        fd.mpMatrix_ = calcProjectionMatrix(fbRes_) * model;
        fd.canvasZoomLevel_ = cameraZoom_;
        fd.cameraTrixelOffset_ = -entityIso;
        fd.textureOffset_ = vec2(0.0f);
        fd.distanceOffset_ = 0;
        fd.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
        fd.effectiveSubdivisionsForHover_ = vec2(1.0f);
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
        for (auto &inst : instances_) {
            frameDataBuffer->subData(0, sizeof(FrameDataTrixelToFramebuffer), &inst.frameData_);
            inst.textures_->bind(0, 1, 2);
            IRRender::device()->drawElements(
                DrawMode::TRIANGLES,
                IRShapes2D::kQuadIndicesLength,
                IndexType::UNSIGNED_SHORT
            );
        }

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
