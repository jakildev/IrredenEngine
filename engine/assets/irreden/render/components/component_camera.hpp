/*
 * Project: Irreden Engine
 * File: component_camera.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef COMPONENT_CAMERA_H
#define COMPONENT_CAMERA_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_ecs.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_position_3d.hpp>

using namespace IRMath;
using namespace IRECS;

namespace IRComponents {

    // TODO: Move interpolation stuff to seperate component from camera
    struct C_Camera {
        vec2 pos2DScreen_;
        vec2 targetPos2DScreen_;
        vec2 zoom_;
        vec2 startZoom_;
        vec2 targetZoom_;
        vec2 triangleStepSizeScreen_;
        EntityId followEntity_;
        static constexpr double zoomDurationSeconds_ = 0.5;
        double zoomCurrentTime_ = zoomDurationSeconds_;
        static constexpr int snapDurationFrames_ = 180;
        int snapCurrentFrame_ = snapDurationFrames_;
        static constexpr int moveSpeed_ = 200;
        bool moveRight_ = false;
        bool moveLeft_ = false;
        bool moveUp_ = false;
        bool moveDown_ = false;

        C_Camera(
            vec2 startPos,
            EntityId followEntity = 0
        )
        :   pos2DScreen_{startPos}
        ,   zoom_{1.0f}
        ,   startZoom_{1.0f}
        ,   targetZoom_{1.0f}
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

        // TODO: Delta time here
        void tick()
        {
            if(zoomCurrentTime_ < zoomDurationSeconds_) {
                zoomCurrentTime_ = glm::clamp(
                    zoomCurrentTime_ + IRTime::deltaTime(IRTime::RENDER),
                    0.0,
                    zoomDurationSeconds_
                );
                zoom_ = glm::mix(
                    startZoom_,
                    targetZoom_,
                    kEasingFunctions.at(IREasingFunctions::kExponentialEaseOut)(
                        zoomCurrentTime_ / zoomDurationSeconds_
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
            if(followEntity_ != 0) {
                pos2DScreen_ = pos3DtoPos2DScreen(
                    IRECS::getComponent<C_Position3D>(followEntity_).pos_,
                    triangleStepSizeScreen_
                );
            }
            move();
        }

        void moveUpStart() {
            moveUp_ = true;
        }
        void moveDownStart() {
            moveDown_ = true;
        }
        void moveLeftStart() {
            moveLeft_ = true;
        }
        void moveRightStart() {
            moveRight_ = true;
        }
        void moveUpStop() {
            moveUp_ = false;
        }
        void moveDownStop() {
            moveDown_ = false;
        }
        void moveLeftStop() {
            moveLeft_ = false;
        }
        void moveRightStop() {
            moveRight_ = false;
        }
        void move() {
            if(moveUp_) {
                moveUp();
            }
            if(moveDown_) {
                moveDown();
            }
            if(moveLeft_) {
                moveLeft();
            }
            if(moveRight_) {
                moveRight();
            }
        }
        void moveUp() {
            pos2DScreen_.y += moveSpeed_ * IRTime::deltaTime(IRTime::RENDER);
        }
        void moveDown() {
            pos2DScreen_.y -= moveSpeed_ * IRTime::deltaTime(IRTime::RENDER);
        }
        void moveLeft() {
            pos2DScreen_.x += moveSpeed_ * IRTime::deltaTime(IRTime::RENDER);
        }
        void moveRight() {
            pos2DScreen_.x -= moveSpeed_ * IRTime::deltaTime(IRTime::RENDER);
        }

        void setFollowEntity(EntityId followEntity) {
            followEntity_ = followEntity;
        }


        // add lerp provided by chatgpt
        void zoomIn() {
            startZoom_ = zoom_;
            setTargetZoom(round(glm::clamp(
                targetZoom_ * vec2(2.0f),
                IRConstants::kTriangleCanvasZoomMin,
                IRConstants::kTriangleCanvasZoomMax
            )));
        }


        void zoomOut() {
            startZoom_ = zoom_;
            setTargetZoom(round(glm::clamp(
                targetZoom_ / vec2(2.0f),
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
            zoomCurrentTime_ = 0.0f;
        }

        void setTargetPosition(vec2 targetPos) {
            targetPos2DScreen_ = targetPos;
            snapCurrentFrame_ = 0;
            followEntity_ = IRECS::kNullEntity;
        }

        void setTargetPosition(vec3 targetPos) {
            setTargetPosition(
                pos3DtoPos2DScreen(targetPos, triangleStepSizeScreen_)
            );
        }

        void setTriangleStepSizeScreen(vec2 gameResolution, int pixelScaleFactor) {
            triangleStepSizeScreen_ =
                IRMath::calcTriangleStepSizeScreen(
                    gameResolution,
                    zoom_,
                    pixelScaleFactor
                );
        }

        void setPosScreenFromPos3D(vec3 pos) {
            pos2DScreen_ = pos3DtoPos2DScreen(pos, triangleStepSizeScreen_);
        }

    };

} // namespace IRComponents

#endif /* COMPONENT_CAMERA_H */
