/*
 * Project: Irreden Engine
 * File: system_trixel_to_framebuffer_frame_data.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: December 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_TRIXEL_TO_FRAMEBUFFER_FRAME_DATA_H
#define SYSTEM_TRIXEL_TO_FRAMEBUFFER_FRAME_DATA_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/render/components/component_zoom_level.hpp>
#include <irreden/render/components/component_trixel_framebuffer.hpp>
#include <irreden/render/components/component_frame_data_trixel_to_framebuffer.hpp>

#include <glm/gtc/matrix_transform.hpp> // not this here

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

namespace IRECS {

    template <>
    struct System<TRIXEL_TO_FRAMEBUFFER_FRAME_DATA> {
        static SystemId create() {
            return createSystem<
                C_TrixelCanvasFramebuffer,
                C_FrameDataTrixelToFramebuffer,
                C_CameraPosition2DIso,
                C_ZoomLevel
            >(
                "CanvasToFramebufferFrameData",
                [](
                    const C_TrixelCanvasFramebuffer& framebuffer,
                    C_FrameDataTrixelToFramebuffer& frameData,
                    const C_CameraPosition2DIso& cameraPosition,
                    const C_ZoomLevel& zoomLevel
                )
                {
                    vec2 framebufferResolution =
                        vec2(framebuffer.getResolutionPlusBuffer());
                    frameData.frameData_.canvasZoomLevel_ =
                        IRRender::getCameraZoom() *
                        zoomLevel.zoom_;
                    frameData.frameData_.canvasOffset_ = cameraPosition.pos_;
                    frameData.frameData_.textureOffset_ = vec2(0);
                    frameData.frameData_.mouseHoveredTriangleIndex_ =
                        IRRender::mouseTrixelPositionWorld();
                    frameData.frameData_.mpMatrix_ =
                        calcProjectionMatrix(
                            framebufferResolution
                        ) *
                        calcModelMatrix(
                            framebufferResolution,
                            frameData.frameData_.canvasOffset_,
                            frameData.frameData_.canvasZoomLevel_
                        );

                }
            );
        }
    private:
        static mat4 calcProjectionMatrix(const vec2& resolution) {
            mat4 projection = glm::ortho(
                0.0f,
                resolution.x,
                0.0f,
                resolution.y,
                -1.0f,
                100.0f
            );
            return projection;
        }

        static mat4 calcModelMatrix(
            const vec2& resolution,
            const vec2& cameraPositionIso,
            const vec2& zoomLevel
        )
        {
            vec2 isoPixelOffset =
                glm::floor(
                    IRMath::pos2DIsoToPos2DGameResolution(
                        glm::fract(cameraPositionIso),
                        zoomLevel
                    )
                ) *
                vec2(1, -1);
            mat4 model = mat4(1.0f);
            model = glm::translate(
                model,
                glm::vec3(
                    resolution.x / 2 + isoPixelOffset.x,
                    resolution.y / 2 + isoPixelOffset.y,
                    0.0f
                )
            );
            model = glm::scale(
                model,
                glm::vec3(
                    resolution.x *
                        zoomLevel.x,
                    resolution.y *
                        zoomLevel.y,
                    1.0f
                )
            );
            return model;
        }

    };

} // namespace System

#endif /* SYSTEM_TRIXEL_TO_FRAMEBUFFER_FRAME_DATA_H */

