#ifndef SYSTEM_VIEWPORT_TO_FRAMEBUFFER_H
#define SYSTEM_VIEWPORT_TO_FRAMEBUFFER_H

// VIEWPORT_TO_FRAMEBUFFER — RENDER pipeline, after TRIXEL_TO_FRAMEBUFFER (which
// clears the framebuffer and composites the world and GUI canvases) and before
// FRAMEBUFFER_TO_SCREEN.
//
// Draws each secondary viewport's canvas into its GUI rectangle. The draw is
// an overlay: it neither tests nor writes the framebuffer's depth and never
// reports a hovered entity, so the world's depth attachment and hover id stay
// the world's.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_viewport_camera.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/viewport.hpp>

#include <vector>

namespace IRSystem {

template <> struct System<VIEWPORT_TO_FRAMEBUFFER> {
    struct Instance {
        IRRender::FrameDataTrixelToFramebuffer frameData_;
        const IRComponents::C_TriangleCanvasTextures *textures_;
    };

    // Gathered in tick, drawn in endTick; capacity is kept across frames.
    std::vector<Instance> instances_;
    IRPrefab::Viewport::detail::CompositeExtent extent_;

    void beginTick() {
        instances_.clear();
        extent_ = IRPrefab::Viewport::detail::compositeExtent();
    }

    void
    tick(const IRComponents::C_ViewportCamera &camera, const IRComponents::C_ZoomLevel &zoomLevel) {
        if (!camera.visible_ || camera.drawnSubject_ == IREntity::kNullEntity) {
            return;
        }
        // One lookup per viewport on its canvas entity — the canvas-iteration
        // pattern, as ENTITY_CANVAS_TO_FRAMEBUFFER resolves its canvases.
        auto texturesOpt = IREntity::getComponentOptional<IRComponents::C_TriangleCanvasTextures>(
            camera.canvasEntity_
        );
        if (!texturesOpt.has_value()) {
            return;
        }
        const IRComponents::C_TriangleCanvasTextures *textures = texturesOpt.value();

        const int subdivisions = IRMath::max(textures->renderedSubdivisions_, 1);
        const IRMath::vec2 texel =
            IRPrefab::Viewport::detail::texelFramebufferSize(zoomLevel.zoom_.x, subdivisions);
        const IRMath::vec2 quadSize = IRMath::vec2(textures->size_) * texel;
        const IRPrefab::Viewport::detail::FramebufferRect rect =
            IRPrefab::Viewport::detail::framebufferRect(
                camera.rectOrigin_,
                camera.rectSize_,
                extent_.guiCanvasSize_,
                extent_.framebufferSize_
            );
        // The canvas is the largest whole-texel size inside the rectangle;
        // center the leftover margin on whole pixels.
        const IRMath::vec2 quadOrigin =
            rect.origin_ + IRMath::floor((rect.size_ - quadSize) * 0.5f);
        // The rectangle is Y-down from the framebuffer's top edge; the
        // framebuffer's own axis is Y-up.
        const IRMath::vec2 quadCenter(
            quadOrigin.x + quadSize.x * 0.5f,
            static_cast<float>(extent_.framebufferSize_.y) - (quadOrigin.y + quadSize.y * 0.5f)
        );
        IRMath::mat4 model = IRMath::translate(IRMath::mat4(1.0f), IRMath::vec3(quadCenter, 0.0f));
        model = IRMath::scale(model, IRMath::vec3(quadSize, 1.0f));

        IRRender::FrameDataTrixelToFramebuffer frameData{};
        frameData.mpMatrix_ = IRMath::ortho(
                                  0.0f,
                                  static_cast<float>(extent_.framebufferSize_.x),
                                  0.0f,
                                  static_cast<float>(extent_.framebufferSize_.y),
                                  -1.0f,
                                  100.0f
                              ) *
                              model;
        frameData.canvasZoomLevel_ =
            zoomLevel.zoom_ / IRMath::vec2(static_cast<float>(subdivisions));
        frameData.cameraTrixelOffset_ = IRMath::vec2(0.0f);
        frameData.textureOffset_ = IRMath::vec2(0.0f);
        frameData.mouseHoveredTriangleIndex_ = IRMath::vec2(-1000000.0f);
        frameData.effectiveSubdivisionsForHover_ =
            IRMath::vec2(static_cast<float>(subdivisions), 1.0f);
        frameData.showHoverHighlight_ = 0.0f;
        frameData.trixelSampleLayout_ = static_cast<int>(textures->renderedSampleLayout_);

        instances_.push_back(Instance{frameData, textures});
    }

    void endTick() {
        if (instances_.empty()) {
            return;
        }
        IR_PROFILE_SCOPE("viewportToFb");
        IREntity::getComponent<IRComponents::C_TrixelCanvasFramebuffer>("mainFramebuffer")
            .bindFramebuffer();
        auto *frameDataBuffer =
            IRRender::getNamedResource<IRRender::Buffer>("TrixelToFramebufferFrameData");
        IRRender::getNamedResource<IRRender::VAO>("QuadVAO")->bind();
        IRRender::getNamedResource<IRRender::ShaderProgram>("CanvasToFramebufferProgram")->use();
        IRRender::device()->setPolygonMode(IRRender::PolygonMode::FILL);
        IRRender::device()->setDepthTest(false);
        IRRender::device()->setDepthWrite(false);
        for (const Instance &instance : instances_) {
            frameDataBuffer
                ->subData(0, sizeof(IRRender::FrameDataTrixelToFramebuffer), &instance.frameData_);
            instance.textures_->bind(0, 1, 2);
            IRRender::device()->drawElements(
                IRRender::DrawMode::TRIANGLES,
                IRShapes2D::kQuadIndicesLength,
                IRRender::IndexType::UNSIGNED_SHORT
            );
        }
        IRRender::device()->setDepthTest(true);
        IRRender::device()->setDepthWrite(true);
    }

    static SystemId create() {
        return registerSystem<
            VIEWPORT_TO_FRAMEBUFFER,
            IRComponents::C_ViewportCamera,
            IRComponents::C_ZoomLevel>("ViewportToFramebuffer");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VIEWPORT_TO_FRAMEBUFFER_H */
