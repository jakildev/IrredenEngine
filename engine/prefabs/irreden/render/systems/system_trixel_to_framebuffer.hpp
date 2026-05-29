#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/gpu_stage_timing_observer.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!

namespace IRSystem {

template <> struct System<TRIXEL_TO_FRAMEBUFFER> {
    Buffer *frameDataBuf_ = nullptr;
    Buffer *hoveredIdBuf_ = nullptr;
    ShaderProgram *program_ = nullptr;
    VAO *quadVao_ = nullptr;

    // Smooth camera Z-yaw (T3 / #1310). Re-resolved every frame in beginTick,
    // never held across frames (.claude/rules/cpp-ecs.md). Non-null only on the
    // main world canvas AND only while the per-axis trixel canvases are
    // allocated (camera at a non-cardinal residual yaw). When set, the main
    // canvas's single trixel→framebuffer draw is replaced by a three-pass depth
    // composite of the X/Y/Z per-axis canvases (see drawPerAxisComposite). At a
    // cardinal these are released, this is null, and the byte-identical
    // single-canvas fast path runs.
    IREntity::EntityId perAxisCanvasEntity_ = IREntity::kNullEntity;
    const C_PerAxisTrixelCanvases *perAxisCanvases_ = nullptr;

    void tick(
        IREntity::EntityId entity,
        const C_TriangleCanvasTextures &triangleCanvasTextures,
        const C_Name &
    ) {
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        auto &frameData = IREntity::getComponent<C_FrameDataTrixelToFramebuffer>("mainFramebuffer");
        vec2 framebufferResolution = vec2(framebuffer.getResolutionPlusBuffer());
        const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
        const IRRender::SubdivisionMode renderMode = IRRender::getSubdivisionMode();
        auto renderBehavior = IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(entity);
        const C_TrixelCanvasRenderBehavior behavior =
            renderBehavior.has_value() ? (*renderBehavior.value()) : C_TrixelCanvasRenderBehavior{};
        auto zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(entity);
        const vec2 baseCanvasZoom =
            behavior.useCameraZoom_
                ? IRRender::getCameraZoom()
                : (zoomLevel.has_value() ? (*zoomLevel.value()).zoom_ : vec2(1.0f));

        frameData.frameData_.canvasZoomLevel_ = baseCanvasZoom;
        if (behavior.applyRenderSubdivisions_ && renderMode != IRRender::SubdivisionMode::NONE) {
            frameData.frameData_.canvasZoomLevel_ /= vec2(effectiveSubdivisions);
        }

        frameData.frameData_.cameraTrixelOffset_ =
            behavior.useCameraPositionIso_ ? IRRender::getCameraPosition2DIso() : vec2(0.0f);
        frameData.frameData_.cameraTrixelOffset_ +=
            vec2(behavior.parityOffsetIsoX_, behavior.parityOffsetIsoY_);
        if (behavior.applyRenderSubdivisions_ && renderMode != IRRender::SubdivisionMode::NONE) {
            frameData.frameData_.cameraTrixelOffset_ *= vec2(effectiveSubdivisions);
        }
        frameData.frameData_.textureOffset_ = vec2(0);
        frameData.frameData_.distanceOffset_ = 0;
        frameData.frameData_.mpMatrix_ = calcProjectionMatrix(framebufferResolution) *
                                         calcModelMatrix(
                                             framebufferResolution,
                                             frameData.frameData_.cameraTrixelOffset_,
                                             frameData.frameData_.canvasZoomLevel_
                                         );

        if (!behavior.mouseHoverEnabled_) {
            frameData.frameData_.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
            frameData.frameData_.effectiveSubdivisionsForHover_ = vec2(1.0f);
            frameData.frameData_.showHoverHighlight_ = 0.0f;
        } else {
            const ivec2 hoverSubdiv = IRRender::mouseTrixelPositionWorld();
            const float subdiv = static_cast<float>(effectiveSubdivisions);
            frameData.frameData_.mouseHoveredTriangleIndex_ = vec2(hoverSubdiv) / vec2(subdiv);
            frameData.frameData_.effectiveSubdivisionsForHover_ = vec2(subdiv, 0.0f);
            frameData.frameData_.showHoverHighlight_ =
                (IRRender::isHoveredTrixelVisible() ? 1.0f : 0.0f);
        }

        // Smooth camera Z-yaw composite (T3 / #1310). On the main world canvas
        // while rotating, replace the single cardinal-snapped draw with a
        // three-pass depth composite of the per-axis (X/Y/Z) canvases T2 (#1309)
        // populated. Each pass writes the shared world-space `pos3DtoDistance`
        // into the framebuffer depth buffer; the GL_LESS depth test (enabled on
        // framebuffer bind) resolves the nearest face per pixel — that is the
        // composite. The per-axis draw is REPLACE-not-add: the main canvas's
        // cardinal-snapped voxels sit at the same world depth as the smooth
        // copies, so drawing both would let depth ties ghost the snapped layer
        // through. Lighting / AO on the resolved composite is T4 (#1311); during
        // rotation the composite shows raw voxel color.
        if (entity == perAxisCanvasEntity_ && perAxisCanvases_ != nullptr &&
            perAxisCanvases_->isAllocated()) {
            drawPerAxisComposite(
                frameData, *perAxisCanvases_, triangleCanvasTextures.size_, framebufferResolution
            );
            return;
        }

        frameData.updateFrameData(frameDataBuf_);

        triangleCanvasTextures.bind(0, 1, 2);
        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        IRRender::device()->drawElements(
            DrawMode::TRIANGLES,
            IRShapes2D::kQuadIndicesLength,
            IndexType::UNSIGNED_SHORT
        );
        IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
    }

    // NOTE (T3 / #1310, design-blocked): this is the composite SCAFFOLD. The
    // three-pass depth composite, the perAxisSize/mainSize scale, and the
    // byte-identical cardinal fast path all work, but the per-canvas
    // trixel→framebuffer PARITY is unresolved — T2 (#1309) bakes the continuous
    // reposition (roundHalfUp(pos3DtoPos2DIsoYawed)) into each canvas, so trixel
    // centers land on mixed-parity iso cells, and the single-global-parity
    // de-tiling in f_trixel_to_framebuffer.glsl
    // (trixelFramebufferSamplePosition + trixelOriginModifier) cannot de-tile
    // them → the #1256 stripe/checkerboard artifact at every inter-cardinal yaw.
    // Resolving it is an architectural call (see the PR's NEEDS-DESIGN comment):
    // restructure to basis-at-expansion (design doc) vs even-parity-snapped
    // reposition vs basis-aware de-tiling. Do not treat this as finished.
    //
    // Composite the three per-axis trixel canvases into the framebuffer by depth
    // (T3 / #1310). Reuses the single-canvas trixel→framebuffer shader unchanged
    // — each per-axis canvas is a standard iso-pixel image T2 baked the
    // continuous reposition + per-face deform into, so no shader change is
    // needed (and OpenGL/Metal stay in parity for free). Draws one full-screen
    // quad per axis canvas; the framebuffer's GL_LESS depth test picks the
    // nearest `pos3DtoDistance` per pixel.
    //
    // The per-axis textures are sized to the bounded worst case
    // (IRMath::perAxisTrixelCanvasWorstCaseSize → ~(2W, W+H)), larger than the
    // main canvas, so the model-matrix zoom is scaled component-wise by
    // perAxisSize / mainSize. That keeps one per-axis texel mapping to the same
    // screen footprint (and the canvas-center iso origin to screen center) as a
    // main-canvas texel, so the composite lands at the right scale and position.
    void drawPerAxisComposite(
        C_FrameDataTrixelToFramebuffer &frameData,
        const C_PerAxisTrixelCanvases &axes,
        const ivec2 mainCanvasSize,
        const vec2 framebufferResolution
    ) {
        const vec2 zoomEff =
            frameData.frameData_.canvasZoomLevel_ * vec2(axes.size_) / vec2(mainCanvasSize);
        frameData.frameData_.mpMatrix_ =
            calcProjectionMatrix(framebufferResolution) *
            calcModelMatrix(
                framebufferResolution, frameData.frameData_.cameraTrixelOffset_, zoomEff
            );
        frameData.updateFrameData(frameDataBuf_);

        IRRender::device()->setPolygonMode(PolygonMode::FILL);
        for (int axis = 0; axis < C_PerAxisTrixelCanvases::kAxisCount; ++axis) {
            const C_PerAxisTrixelCanvases::AxisTextures &tex = axes.axes_[axis];
            tex.colors_.second->bind(0);
            tex.distances_.second->bind(1);
            tex.entityIds_.second->bind(2);
            IRRender::device()->drawElements(
                DrawMode::TRIANGLES,
                IRShapes2D::kQuadIndicesLength,
                IndexType::UNSIGNED_SHORT
            );
            IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);
        }
    }

    void beginTick() {
        HoveredEntityIdLayout resetData;
        hoveredIdBuf_->subData(0, sizeof(resetData), &resetData);
        program_->use();
        quadVao_->bind();
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        framebuffer.bindFramebuffer();
        framebuffer.clear();

        // Resolve the main canvas's per-axis trixel canvases once per frame for
        // the per-entity tick to consume without a getComponent on its own
        // iterating canvas (#1310). Re-resolved every frame; never held across
        // frames. Null unless the main canvas has the component AND it is
        // currently allocated (camera rotating) — VOXEL_TO_TRIXEL_STAGE_1's
        // beginTick already ran the per-frame allocate/release sync, so the
        // allocation state is current. At a cardinal these are released → the
        // tick takes the byte-identical single-canvas path.
        perAxisCanvasEntity_ = IRRender::getCanvas("main");
        perAxisCanvases_ = nullptr;
        if (perAxisCanvasEntity_ != IREntity::kNullEntity) {
            auto perAxis = IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(
                perAxisCanvasEntity_
            );
            if (perAxis.has_value() && (*perAxis.value()).isAllocated()) {
                perAxisCanvases_ = perAxis.value();
            }
        }
    }

    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "CanvasToFramebufferProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertTrixelToFramebuffer, ShaderType::VERTEX},
                ShaderStage{IRRender::kFileFragTrixelToFramebuffer, ShaderType::FRAGMENT}
            }
        );
        IRRender::createNamedResource<Buffer>(
            "TrixelToFramebufferFrameData",
            nullptr,
            sizeof(FrameDataTrixelToFramebuffer),
            BUFFER_STORAGE_DYNAMIC,
            BufferTarget::UNIFORM,
            kBufferIndex_FrameDataUniformIsoTriangles
        );
        HoveredEntityIdLayout initData;
        IRRender::createNamedResource<Buffer>(
            "HoveredEntityIdBuffer",
            &initData,
            sizeof(initData),
            BUFFER_STORAGE_DYNAMIC | BUFFER_STORAGE_MAP_READ | BUFFER_STORAGE_MAP_PERSISTENT |
                BUFFER_STORAGE_MAP_COHERENT,
            BufferTarget::SHADER_STORAGE,
            kBufferIndex_HoveredEntityId
        );

        // Exclude detached per-entity canvases: those are composited at the
        // owning entity's iso position by ENTITY_CANVAS_TO_FRAMEBUFFER. Without
        // the exclude this full-screen pass would also blit each detached
        // canvas across the whole framebuffer (a second camera-scaled copy).
        SystemId id = registerSystem<
            TRIXEL_TO_FRAMEBUFFER,
            C_TriangleCanvasTextures,
            C_Name,
            Exclude<C_DetachedCanvas>>("CanvasToFramebuffer");
        auto *sys = getSystemParams<System<TRIXEL_TO_FRAMEBUFFER>>(id);
        sys->frameDataBuf_ = IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData");
        sys->hoveredIdBuf_ = IRRender::getNamedResource<Buffer>("HoveredEntityIdBuffer");
        sys->program_ = IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram");
        sys->quadVao_ = IRRender::getNamedResource<VAO>("QuadVAO");
        IRRender::tagGpuStage(id, "trixelToFb");
        return id;
    }

    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        mat4 projection = ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
        return projection;
    }

    static mat4
    calcModelMatrix(const vec2 &resolution, const vec2 &cameraPositionIso, const vec2 &zoomLevel) {
        // Game-pixel half of the anti-vibration decomposition (see
        // `IRMath::cameraSubPixelOffsets`). `FRAMEBUFFER_TO_SCREEN` consumes
        // the matching `screenPxResidual_` from the same helper to keep the
        // two stages byte-for-byte consistent at game-pixel boundaries.
        const IRMath::CameraSubPixelOffsets sub =
            IRMath::cameraSubPixelOffsets(cameraPositionIso, zoomLevel, ivec2(1));
        const vec2 isoPixelOffset = vec2(sub.framebufferGamePxOffset_);
        mat4 model = mat4(1.0f);
        model = translate(
            model,
            vec3(resolution.x / 2 + isoPixelOffset.x, resolution.y / 2 + isoPixelOffset.y, 0.0f)
        );
        model = scale(model, vec3(resolution.x * zoomLevel.x, resolution.y * zoomLevel.y, 1.0f));
        return model;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_TRIXEL_TO_FRAMEBUFFER_H */
