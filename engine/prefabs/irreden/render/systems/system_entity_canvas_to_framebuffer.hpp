#ifndef SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H
#define SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_platform.hpp>

#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/gpu_stage_timing.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>

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

    static std::vector<CanvasInstance> &getInstances() {
        static std::vector<CanvasInstance> instances;
        return instances;
    }

    static SystemId create() {
        return createSystem<C_EntityCanvas, C_PositionGlobal3D, C_PositionOffset3D>(
            "EntityCanvasToFramebuffer",
            [](IREntity::EntityId entityId,
               const C_EntityCanvas &entityCanvas,
               const C_PositionGlobal3D &globalPos,
               const C_PositionOffset3D &offsetPos) {
                if (!entityCanvas.visible_ ||
                    entityCanvas.canvasEntity_ == IREntity::kNullEntity ||
                    static_cast<int>(getInstances().size()) >= kMaxEntityCanvasInstances) {
                    return;
                }

                auto texOpt = IREntity::getComponentOptional<C_TriangleCanvasTextures>(
                    entityCanvas.canvasEntity_
                );
                if (!texOpt.has_value()) return;
                auto *canvasTextures = texOpt.value();

                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                vec2 fbRes = vec2(framebuffer.getResolutionPlusBuffer());
                vec2 mainCanvasSize = IRRender::getMainCanvasSizeTrixels();
                vec2 cameraIso = IRRender::getCameraPosition2DIso();
                vec2 cameraZoom = IRRender::getCameraZoom();

                vec3 pos3D = globalPos.pos_ + offsetPos.pos_;
                vec2 entityIso = pos3DtoPos2DIso(pos3D);

                ivec2 mainCanvasSizeI = ivec2(mainCanvasSize);
                vec2 canvasOriginZ1 = vec2(trixelOriginOffsetZ1(mainCanvasSizeI));
                vec2 entityOnMainCanvas =
                    canvasOriginZ1 + IRMath::floor(cameraIso) + entityIso;
                vec2 normalizedPos = entityOnMainCanvas / mainCanvasSize;

                vec2 isoPixelOffset =
                    IRMath::floor(
                        pos2DIsoToPos2DGameResolution(IRMath::fract(cameraIso), cameraZoom)
                    ) * IRPlatform::kIsoToScreenSign;

                vec2 entityAPos = vec2(
                    normalizedPos.x - 0.5f,
                    0.5f - normalizedPos.y
                );
                vec2 entityFbCenter = vec2(
                    fbRes.x * 0.5f + isoPixelOffset.x +
                        entityAPos.x * fbRes.x * cameraZoom.x,
                    fbRes.y * 0.5f + isoPixelOffset.y +
                        entityAPos.y * fbRes.y * cameraZoom.y
                );

                vec2 entityScale = vec2(entityCanvas.canvasSize_) / mainCanvasSize;
                mat4 model = translate(
                    mat4(1.0f), vec3(entityFbCenter, 0.0f)
                );
                model = scale(
                    model,
                    vec3(
                        fbRes.x * cameraZoom.x * entityScale.x,
                        fbRes.y * cameraZoom.y * entityScale.y,
                        1.0f
                    )
                );

                FrameDataTrixelToFramebuffer fd{};
                fd.mpMatrix_ =
                    calcProjectionMatrix(fbRes) * model;
                fd.canvasZoomLevel_ = cameraZoom;
                fd.cameraTrixelOffset_ = -entityIso;
                fd.textureOffset_ = vec2(0.0f);
                fd.distanceOffset_ = 0;
                fd.mouseHoveredTriangleIndex_ = vec2(-1000000.0f);
                fd.effectiveSubdivisionsForHover_ = vec2(1.0f);
                fd.showHoverHighlight_ = 0.0f;

                CanvasInstance inst{};
                inst.frameData_ = fd;
                inst.textures_ = canvasTextures;
                getInstances().push_back(inst);
            },
            []() {
                getInstances().clear();
                IRRender::gpuStageTiming().entityCanvasToFbMs_ = 0.0f;
            },
            []() {
                auto &allInstances = getInstances();
                if (allInstances.empty()) return;

                auto &timing = IRRender::gpuStageTiming();
                IRRender::TimePoint drawStart;
                if (timing.enabled_) {
                    IRRender::device()->finish();
                    drawStart = IRRender::SteadyClock::now();
                }

                auto &framebuffer =
                    IREntity::getComponent<C_TrixelCanvasFramebuffer>("mainFramebuffer");
                framebuffer.bindFramebuffer();

                IRRender::getNamedResource<ShaderProgram>("CanvasToFramebufferProgram")->use();
                IRRender::getNamedResource<VAO>("QuadVAO")->bind();
                auto *frameDataBuffer =
                    IRRender::getNamedResource<Buffer>("TrixelToFramebufferFrameData");

                for (auto &inst : allInstances) {
                    frameDataBuffer->subData(
                        0, sizeof(FrameDataTrixelToFramebuffer), &inst.frameData_
                    );
                    inst.textures_->bind(0, 1, 2);
                    IRRender::device()->setPolygonMode(PolygonMode::FILL);
                    IRRender::device()->drawElements(
                        DrawMode::TRIANGLES,
                        IRShapes2D::kQuadIndicesLength,
                        IndexType::UNSIGNED_SHORT
                    );
                }

                IRRender::device()->memoryBarrier(BarrierType::SHADER_STORAGE);

                if (timing.enabled_) {
                    IRRender::device()->finish();
                    timing.entityCanvasToFbMs_ = IRRender::elapsedMs(
                        drawStart, IRRender::SteadyClock::now());
                }
            }
        );
    }

    static mat4 calcProjectionMatrix(const vec2 &resolution) {
        return ortho(0.0f, resolution.x, 0.0f, resolution.y, -1.0f, 100.0f);
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ENTITY_CANVAS_TO_FRAMEBUFFER_H */
