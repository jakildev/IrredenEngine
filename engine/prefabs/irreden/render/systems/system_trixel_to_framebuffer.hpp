#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_detached_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
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

    void beginTick() {
        HoveredEntityIdLayout resetData;
        hoveredIdBuf_->subData(0, sizeof(resetData), &resetData);
        program_->use();
        quadVao_->bind();
        auto &framebuffer = IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
        framebuffer.bindFramebuffer();
        framebuffer.clear();
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
