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

#include <irreden/system/system_base.hpp>

#include <irreden/render/components/component_viewport.hpp>
#include <irreden/input/components/component_cursor_position.hpp>
#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/render/components/component_camera.hpp>

#include <irreden/system/system_manager.hpp>

#include <irreden/input/systems/system_input_key_mouse.hpp>

using namespace IRComponents;
using namespace IRMath;
using namespace IRECS;
// using namespace IRCommands;

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
        ,   m_cameraFollowEntity{0}
        ,   m_camera{}
        {


            // TODO: Pull commands out somewhere else
            // so that all commands can be visualized and assigned
            // together.
            // registerCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonEqual,
            //     [this]() {
            //         zoomIn();
            //     }
            // );
            // registerCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonMinus,
            //     [this]() {
            //         zoomOut();
            //     }
            // );
            // registerCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonEscape,
            //     [this]() {
            //         closeWindow();
            //     }
            // );
            // registerCommand<kKeyMouseButtonPressed>(
            //     KeyMouseButtons::kKeyButtonLeftControl,
            //     [this]() {
            //         dragCanvasStart();
            //     }
            // );
            // registerCommand<kKeyMouseButtonReleased>(
            //     KeyMouseButtons::kKeyButtonLeftControl,
            //     [this]() {
            //         dragCanvasEnd();
            //     }
            // );
            // registerCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonW,
            //     [this]() {
            //         m_camera.get<C_Camera>().moveUp();
            //     }
            // );
            // registerCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonS,
            //     [this]() {
            //         m_camera.get<C_Camera>().moveDown();
            //     }
            // );
            // registerCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonA,
            //     [this]() {
            //         m_camera.get<C_Camera>().moveLeft();
            //     }
            // );
            // registerCommand<kKeyMouseButtonDown>(
            //     KeyMouseButtons::kKeyButtonD,
            //     [this]() {
            //         m_camera.get<C_Camera>().moveRight();
            //     }
            // );

            // make camera
            m_camera.set(
                C_Camera{
                    vec2(0.0f, 0.0f)
                }
            );
            m_camera.set(
                C_Position3D{
                    vec3(0.0f, 0.0f, 0.0f)
                }
            );

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
                    IRECS::getSystem<INPUT_KEY_MOUSE>().getMousePositionRender().pos_ -
                    m_mouseWheelClickedStart;
            }
            m_mousePositionRenderTriangles =
                (
                    IRECS::getSystem<INPUT_KEY_MOUSE>()
                        .getMousePositionRender().pos_ -
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
            m_camera.get<C_Camera>().zoomIn();
        }

        void zoomOut() {
            IRProfile::engLogInfo("Zooming out");
            m_camera.get<C_Camera>().zoomOut();
        }

        void dragCanvasStart() {
            m_mouseWheelClickedStart =
                IRECS::getSystem<INPUT_KEY_MOUSE>().getMousePositionUpdate().pos_;

            m_isWheelClicked = true;
        }

        void dragCanvasEnd() {
            m_isWheelClicked = false;
            m_cameraOffset += (
                IRECS::getSystem<INPUT_KEY_MOUSE>().getMousePositionUpdate().pos_ -
                m_mouseWheelClickedStart
            );
            m_tempCameraOffset = vec2(0, 0);
        }

        void closeWindow() {
            m_window.setShouldClose();
        }

        inline const vec2 getGlobalCameraOffsetScreen() {
            return m_camera.get<C_Camera>().pos2DScreen_;
        }
        inline const vec2 getTriangleZoom() const {
            return m_camera.get<C_Camera>().zoom_;
        }
        inline const vec2 getTriangleStepSizeScreen() const {
            return m_camera.get<C_Camera>().triangleStepSizeScreen_;
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
            m_camera = EntityHandle(entity);
        }

        void setCameraFollowEntity(const EntityId& entity) {
            m_camera.get<C_Camera>().setFollowEntity(entity);
        }

        void setCameraPositionToEntity(const EntityId& entity) {
            m_camera.get<C_Camera>().setTargetPosition(
                EntityHandle(entity).get<C_Position3D>().pos_
            );
        }

        void setCameraPositionScreen(const vec2& pos) {
            m_camera.get<C_Camera>().pos2DScreen_ = pos;
        }

        void setCameraPosition3D(const vec3& pos) {
            m_camera.get<C_Camera>().setPosScreenFromPos3D(pos);
        }

    private:
        IRInput::IRGLFWWindow& m_window;
        bool m_cameraFollow = false;
        EntityHandle m_cameraFollowEntity;
        C_Viewport m_viewport;
        C_CursorPosition m_cursorPosition;
        bool m_isWheelClicked;
        dvec2 m_mouseWheelClickedStart;
        dvec2 m_cameraOffset; // used in calculation
        dvec2 m_tempCameraOffset; // used in calculation
        dvec2 m_mousePositionRenderTriangles;
        int m_screenRenderResolutionWidth, m_screenRenderResolutionHeight;
        int m_screenScaleFactor;
        EntityHandle m_camera;


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

            m_camera.get<C_Camera>().setTriangleStepSize(
                vec2(m_screenRenderResolutionWidth, m_screenRenderResolutionHeight),
                vec2(IRConstants::kScreenTriangleMaxCanvasSize)
            );
            m_camera.get<C_Camera>().tick(); // TODO: kinda weird like this

        }

        virtual void endExecute() override {

        };

    };

} // namespace IRECS

#endif /* SYSTEM_UPDATE_SCREEN_VIEW_H */
