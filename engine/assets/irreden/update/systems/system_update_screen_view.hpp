/*
 * Project: Irreden Engine
 * File: system_update_screen_view.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_UPDATE_SCREEN_VIEW_H
#define SYSTEM_UPDATE_SCREEN_VIEW_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/render/components/component_viewport.hpp>
#include <irreden/input/components/component_cursor_position.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera.hpp>


#include <irreden/input/systems/system_input_key_mouse.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRECS;

namespace IRECS {

    template<>
    class System<SCREEN_VIEW> : public SystemBase<
        SCREEN_VIEW
    >   {
    public:
        System(
            IRInput::IRGLFWWindow& window
        )
        :   m_window{window}
        ,   m_screenRenderResolutionWidth(0)
        ,   m_screenRenderResolutionHeight(0)
        ,   m_viewport(0, 0)
        ,   m_screenScaleFactor(1)
        ,   m_isWheelClicked(false)
        ,   m_cameraOffset(vec2(0))
        ,   m_tempCameraOffset(vec2(0))
        ,   m_cursorPosition{}
        ,   m_cameraFollowEntity{kNullEntity}
        ,   m_camera{
                createEntity(
                    C_Camera{
                        vec2(0.0f, 0.0f)
                    },
                    C_Position3D{
                        vec3(0.0f, 0.0f, 0.0f)
                    }
                )
            }
        {


            // TODO: Pull commands out somewhere else
            // so that all commands can be visualized and assigned
            // together.
            // createCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonEqual,
            //     [this]() {
            //         zoomIn();
            //     }
            // );
            // createCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonMinus,
            //     [this]() {
            //         zoomOut();
            //     }
            // );
            // createCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonEscape,
            //     [this]() {
            //         closeWindow();
            //     }
            // );
            // createCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonLeftControl,
            //     [this]() {
            //         dragCanvasStart();
            //     }
            // );
            // createCommand<kKeyMouseButtonReleased>(
            //     KeyMouseButtons::kKeyButtonLeftControl,
            //     [this]() {
            //         dragCanvasEnd();
            //     }
            // );
            // createCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonW,
            //     [this]() {
            //         IRECS::getComponent<C_Camera>(m_camera).moveUp();
            //     }
            // );
            // createCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonS,
            //     [this]() {
            //         IRECS::getComponent<C_Camera>(m_camera).moveDown();
            //     }
            // );
            // createCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonA,
            //     [this]() {
            //         IRECS::getComponent<C_Camera>(m_camera).moveLeft();
            //     }
            // );
            // createCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonD,
            //     [this]() {
            //         IRECS::getComponent<C_Camera>(m_camera).moveRight();
            //     }
            // );




            IRProfile::engLogInfo("Created system SCREEN_VIEW");
        }

        virtual ~System() = default;

        void tickWithArchetype(
            Archetype archetype,
            std::vector<EntityId>& entities
            // std::vector<C_Camera>& cameras,
            // std::vector<C_Position3D>& positions3D
        )
        {

        }

        // TEMP to get to work for now
        void beginExecuteRender() {
            if(m_isWheelClicked) {
                m_tempCameraOffset =
                    IRInput::getMousePositionRender().pos_ -
                    m_mouseWheelClickedStart;
            }
            m_mousePositionRenderTriangles =
                (
                    IRInput::getMousePositionRender().pos_ -
                    dvec2(getCenterScreen())
                ) /
                dvec2(getTriangleStepSizeScreen())
            ;

            // IRProfile::engLogInfo(
            //     "Mouse position render triangles: {}, {}",
            //     m_mousePositionRenderTriangles.x,
            //     m_mousePositionRenderTriangles.y
            // );

        }

        void zoomIn() {
            IRProfile::engLogInfo("Zooming in");
            IRECS::getComponent<C_Camera>(m_camera).zoomIn();
        }

        void zoomOut() {
            IRProfile::engLogInfo("Zooming out");
            IRECS::getComponent<C_Camera>(m_camera).zoomOut();
        }

        void dragCanvasStart() {
            m_mouseWheelClickedStart =
                IRInput::getMousePositionUpdate().pos_;

            m_isWheelClicked = true;
        }

        void dragCanvasEnd() {
            m_isWheelClicked = false;
            m_cameraOffset += (
                IRInput::getMousePositionUpdate().pos_ -
                m_mouseWheelClickedStart
            );
            m_tempCameraOffset = vec2(0, 0);
        }

        inline const vec2 getGlobalCameraOffsetScreen() {
            return IRECS::getComponent<C_Camera>(m_camera).pos2DScreen_;
        }
        inline const vec2 getTriangleZoom() const {
            return IRECS::getComponent<C_Camera>(m_camera).zoom_;
        }
        inline const vec2 getTriangleStepSizeScreen() const {
            return IRECS::getComponent<C_Camera>(m_camera).triangleStepSizeScreen_;
        }
        inline const int getViewportX() const {
            return m_viewport.size_.x;
        }
        inline const int getViewportY() const {
            return m_viewport.size_.y;
        }
        inline const int getScaleFactor() const {
            return m_screenScaleFactor;
        }
        inline const vec2 getCenterScreen() const {
            return vec2(
                m_viewport.size_.x / 2,
                m_viewport.size_.y / 2
            );
        }

        void setCamera(const EntityId& entity) {
            m_camera = entity;
        }

        void setCameraFollowEntity(const EntityId& entity) {
            IRECS::getComponent<C_Camera>(m_camera).setFollowEntity(entity);
        }

        // TODO: Use heirarchies here!
        void setCameraPositionToEntity(const EntityId& entity) {
            IRECS::getComponent<C_Camera>(m_camera).setTargetPosition(
                IRECS::getComponent<C_Position3D>(entity).pos_
            );
        }

        void setCameraPositionScreen(const vec2& pos) {
           IRECS::getComponent<C_Camera>(m_camera).pos2DScreen_ = pos;
        }

        void setCameraPosition3D(const vec3& pos) {
            IRECS::getComponent<C_Camera>(m_camera).setPosScreenFromPos3D(pos);
        }

    private:
        IRInput::IRGLFWWindow& m_window;
        bool m_cameraFollow = false;
        EntityId m_cameraFollowEntity;
        C_Viewport m_viewport;
        C_CursorPosition m_cursorPosition;
        bool m_isWheelClicked;
        dvec2 m_mouseWheelClickedStart;
        dvec2 m_cameraOffset; // used in calculation
        dvec2 m_tempCameraOffset; // used in calculation
        dvec2 m_mousePositionRenderTriangles;
        int m_screenRenderResolutionWidth, m_screenRenderResolutionHeight;
        int m_screenScaleFactor;
        EntityId m_camera;


        virtual void beginExecute() override {
            m_window.getUpdateWindowSize(
                m_viewport.size_.x,
                m_viewport.size_.y
            );
            m_window.getUpdateCursorPos(
                m_cursorPosition.posX_,
                m_cursorPosition.posY_
            );
            m_screenScaleFactor = glm::min(
                glm::floor(
                    m_viewport.size_.x /
                    IRConstants::kGameResolution.x
                ),
                glm::floor(
                    m_viewport.size_.y /
                    IRConstants::kGameResolution.y
                )
            );
            m_screenRenderResolutionWidth =
                IRConstants::kGameResolution.x *
                m_screenScaleFactor;
            m_screenRenderResolutionHeight =
                IRConstants::kGameResolution.y *
                m_screenScaleFactor;
            int screenX1 = (m_viewport.size_.x - m_screenRenderResolutionWidth) / 2;
            int screenY1 = (m_viewport.size_.y - m_screenRenderResolutionHeight) / 2;

            IRECS::getComponent<C_Camera>(m_camera).setTriangleStepSize(
                vec2(m_screenRenderResolutionWidth, m_screenRenderResolutionHeight),
                vec2(IRConstants::kScreenTriangleMaxCanvasSize)
            );
            IRECS::getComponent<C_Camera>(m_camera).tick(); // TODO: kinda weird like this

        }

        virtual void endExecute() override {

        };

    };

} // namespace IRECS

#endif /* SYSTEM_UPDATE_SCREEN_VIEW_H */
