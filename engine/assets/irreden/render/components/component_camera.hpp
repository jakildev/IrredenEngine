/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\game_components\component_camera.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_CAMERA_H
#define COMPONENT_CAMERA_H

#include <irreden/ir_math.hpp>
#include <irreden/math/easing_functions.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ecs/entity_handle.hpp>

#include <irreden/common/components/component_position_3d.hpp>

using namespace IRMath;
using namespace IRComponents;
using namespace IRECS;

namespace IRComponents {

    // TODO: Move interpolation stuff to seperate component from camera
    struct C_Camera {
        vec2 pos2DScreen_;
        vec2 targetPos2DScreen_;
        vec2 zoom_;
        vec2 targetZoom_;
        vec2 triangleStepSizeScreen_;
        EntityHandle followEntity_;
        static constexpr int zoomDurationFrames_ = 30;
        int zoomCurrentFrame_ = zoomDurationFrames_;
        static constexpr int snapDurationFrames_ = 180;
        int snapCurrentFrame_ = snapDurationFrames_;

        C_Camera(
            vec2 startPos,
            EntityId followEntity = 0
        )
        :   pos2DScreen_{startPos}
        ,   zoom_{1.0f}
        ,   followEntity_{followEntity}
        {

        }

        C_Camera(
            float x,
            float y,
            EntityId followEntity = 0
        )
        :   C_Camera{vec2(x, y), followEntity}
        {

        }

        // Default
        C_Camera()
        :   C_Camera{vec2(0, 0), 0}
        {

        }

        void tick()
        {
            if(zoomCurrentFrame_ < zoomDurationFrames_) {
                zoomCurrentFrame_++;
                zoom_ = glm::mix(
                    zoom_,
                    targetZoom_,
                    kEasingFunctions.at(IREasingFunctions::kExponentialEaseOut)(
                        (float)zoomCurrentFrame_ / zoomDurationFrames_
                    )
                );
            }
            if(snapCurrentFrame_ < snapDurationFrames_) {
                snapCurrentFrame_++;
                pos2DScreen_ = glm::mix(
                    pos2DScreen_,
                    targetPos2DScreen_,
                    kEasingFunctions.at(IREasingFunctions::kQuadraticEaseInOut)(
                        (float)snapCurrentFrame_ / snapDurationFrames_
                    )
                );
            }
            if(followEntity_.id_ != 0) {
                pos2DScreen_ = pos3DtoPos2DScreen(
                    followEntity_.get<C_Position3D>().pos_,
                    triangleStepSizeScreen_
                );
            }

            // if(cameraDrag)
        }

        void moveUp() {
            pos2DScreen_.y += 5;
        }
        void moveDown() {
            pos2DScreen_.y -= 5;
        }
        void moveLeft() {
            pos2DScreen_.x += 5;
        }
        void moveRight() {
            pos2DScreen_.x -= 5;
        }

        void setFollowEntity(EntityId followEntity) {
            followEntity_.id_ = followEntity;
        }


        // add lerp provided by chatgpt
        void zoomIn() {
            setTargetZoom(round(glm::clamp(
                zoom_ * vec2(2.0f),
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            )));
        }


        void zoomOut() {
            setTargetZoom(round(glm::clamp(
                zoom_ / vec2(2.0f),
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            )));
        }


        void setTargetZoom(vec2 targetZoom) {
            targetZoom_ = glm::clamp(
                targetZoom,
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            );
            zoomCurrentFrame_ = 0;
        }

        void setTargetPosition(vec2 targetPos) {
            targetPos2DScreen_ = targetPos;
            snapCurrentFrame_ = 0;
            followEntity_.id_ = 0;
        }

        void setTargetPosition(vec3 targetPos) {
            setTargetPosition(
                pos3DtoPos2DScreen(targetPos, triangleStepSizeScreen_)
            );
        }

        void setTriangleStepSize(vec2 resolution, vec2 triangleCanvasSize) {
            triangleStepSizeScreen_ =
                resolution /
                triangleCanvasSize *
                zoom_;
        }

        void setPosScreenFromPos3D(vec3 pos) {
            pos2DScreen_ = pos3DtoPos2DScreen(pos, triangleStepSizeScreen_);
        }

    };

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_H */
