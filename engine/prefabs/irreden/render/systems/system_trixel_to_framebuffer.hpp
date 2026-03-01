#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_window.hpp>

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/common/components/component_name.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!

namespace IRSystem {

template <> struct System<TRIXEL_TO_FRAMEBUFFER> {
    static SystemId create() {
        IRRender::createNamedResource<ShaderProgram>(
            "CanvasToFramebufferProgram",
            std::vector{
                ShaderStage{IRRender::kFileVertTrixelToFramebuffer, GL_VERTEX_SHADER}.getHandle(),
                ShaderStage{IRRender::kFileFragTrixelToFramebuffer, GL_FRAGMENT_SHADER}.getHandle()
            }
        );
        IRRender::createNamedResource<Buffer>(
            "TrixelToFramebufferFrameData",
            nullptr,
            sizeof(FrameDataTrixelToFramebuffer),
            GL_DYNAMIC_STORAGE_BIT,
            GL_UNIFORM_BUFFER,
            kBufferIndex_FrameDataUniformIsoTriangles
        );
        struct { uvec2 entityId{0u, 0u}; float depth{1.0f}; float _pad{0.0f}; } initData;
        IRRender::createNamedResource<Buffer>(
            "HoveredEntityIdBuffer",
            &initData,
            sizeof(initData),
            GL_DYNAMIC_STORAGE_BIT | GL_MAP_READ_BIT,
            GL_SHADER_STORAGE_BUFFER,
            kBufferIndex_HoveredEntityId
        );

        return createSystem<C_TriangleCanvasTextures, C_Name>(
            "CanvasToFramebuffer",
            [](IREntity::EntityId &entity,
               const C_TriangleCanvasTextures &triangleCanvasTextures,
               const C_Name &) {
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                auto &frameData =
                    IREntity::getComponent<C_FrameDataTrixelToFramebuffer>("mainFramebuffer");
                vec2 framebufferResolution = vec2(framebuffer.getResolutionPlusBuffer());
                const int effectiveSubdivisions = IRRender::getVoxelRenderEffectiveSubdivisions();
                const IRRender::VoxelRenderMode renderMode = IRRender::getVoxelRenderMode();
                auto renderBehavior =
                    IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(entity);
                const C_TrixelCanvasRenderBehavior behavior =
                    renderBehavior.has_value() ? (*renderBehavior.value())
                                               : C_TrixelCanvasRenderBehavior{};
                auto zoomLevel = IREntity::getComponentOptional<C_ZoomLevel>(entity);
                const vec2 baseCanvasZoom = behavior.useCameraZoom_
                                                ? IRRender::getCameraZoom()
                                                : (zoomLevel.has_value() ? (*zoomLevel.value()).zoom_
                                                                         : vec2(1.0f));

                frameData.frameData_.canvasZoomLevel_ = baseCanvasZoom;
                if (behavior.applyRenderSubdivisions_ &&
                    renderMode != IRRender::VoxelRenderMode::SNAPPED) {
                    frameData.frameData_.canvasZoomLevel_ /= vec2(effectiveSubdivisions);
                }

                frameData.frameData_.cameraTrixelOffset_ =
                    behavior.useCameraPositionIso_ ? IRRender::getCameraPosition2DIso()
                                                   : vec2(0.0f);
                frameData.frameData_.cameraTrixelOffset_ +=
                    vec2(behavior.parityOffsetIsoX_, behavior.parityOffsetIsoY_);
                if (behavior.applyRenderSubdivisions_ &&
                    renderMode != IRRender::VoxelRenderMode::SNAPPED) {
                    frameData.frameData_.cameraTrixelOffset_ *= vec2(effectiveSubdivisions);
                }
                frameData.frameData_.textureOffset_ = vec2(0);
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
                }
                else {
                    const ivec2 hoverSubdiv = IRRender::mouseTrixelPositionWorld();
                    const float subdiv = static_cast<float>(effectiveSubdivisions);
                    frameData.frameData_.mouseHoveredTriangleIndex_ =
                        vec2(hoverSubdiv) / vec2(subdiv);
                    frameData.frameData_.effectiveSubdivisionsForHover_ = vec2(subdiv, 0.0f);
                    frameData.frameData_.showHoverHighlight_ =
                        (IRRender::isHoveredTrixelVisible() ? 1.0f : 0.0f);
                }

                frameData.updateFrameData(
                    IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData")
                );

                triangleCanvasTextures.bind(0, 1, 2);
                ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                ENG_API->glDrawElements(
                    GL_TRIANGLES,
                    IRShapes2D::kQuadIndicesLength,
                    GL_UNSIGNED_SHORT,
                    nullptr
                );
                glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
            },
            []() {
                struct { uvec2 entityId{0u, 0u}; float depth{1.0f}; float _pad{0.0f}; } resetData;
                IRRender::getNamedResource<Buffer>("HoveredEntityIdBuffer")
                    ->subData(0, sizeof(resetData), &resetData);
                IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram")->use();
                IRRender::getNamedResource<VAO>("QuadVAO")->bind();
                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                framebuffer.bindFramebuffer();
                framebuffer.clear();
            }
        );
    }

  private:
    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        mat4 projection = ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
        return projection;
    }

    static mat4
    calcModelMatrix(const vec2 &resolution, const vec2 &cameraPositionIso, const vec2 &zoomLevel) {
        vec2 isoPixelOffset =
            IRMath::floor(
                IRMath::pos2DIsoToPos2DGameResolution(IRMath::fract(cameraPositionIso), zoomLevel)
            ) *
            vec2(1, -1);
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
