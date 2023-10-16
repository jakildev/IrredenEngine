/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_rendering_framebuffers_to_screen.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_RENDERING_FRAMEBUFFERS_TO_SCREEN_H
#define SYSTEM_RENDERING_FRAMEBUFFERS_TO_SCREEN_H

#include "..\ecs\ir_system_base.hpp"
#include "..\math\ir_math.hpp"

#include "..\rendering\ir_rendering.hpp"
#include "..\shaders\shader_names.hpp"
#include "..\rendering\shader.hpp"
#include "..\rendering\vao.hpp"
#include "..\rendering\shapes_2d.hpp"
#include "..\rendering\buffer.hpp"

#include "..\components\component_position_3d.hpp"
#include "..\components\component_rendering_triangle_framebuffer.hpp"
#include "..\components\component_camera_position_2d_iso.hpp"
#include "..\components\component_texture_scroll.hpp"
#include "system_update_screen_view.hpp"


using namespace IRMath;

// TODO: effects at this stage like blur, etc!!

namespace IRECS {

    template <>
    class IRSystem<RENDERING_FRAMEBUFFER_TO_SCREEN> : public IRSystemBase<
        RENDERING_FRAMEBUFFER_TO_SCREEN,
        C_TriangleCanvasFramebuffer,
        C_Position3D,
        C_CameraPosition2DIso
    >   {
    public:
        IRSystem()
        :   m_shaderProgram{  // FramebufferBasic
                ShaderStage{
                    IRShaders::kFileVertFramebufferScreen,
                    GL_VERTEX_SHADER
                }.getHandle(),
                ShaderStage{
                    IRShaders::kFileFragFramebufferScreen,
                    GL_FRAGMENT_SHADER
                }.getHandle()
            }
        ,   m_bufferTexturedQuad{
                IRShapes2D::k2DQuadTextured,
                sizeof(IRShapes2D::k2DQuadTextured),
                0
            }
        ,   m_vaoTexturedQuad{
                m_bufferTexturedQuad.getHandle(),
                0,
                2,
                kAttrList2Float2
            }
        ,   m_bufferFrameData{
                nullptr,
                sizeof(FrameDataFramebuffer),
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FramebufferFrameDataUniform
            }
        ,   m_frameData{}
        {
            ENG_LOG_INFO("Created system RENDERING_FRAMEBUFFER_TO_SCREEN");
        }

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            const std::vector<C_TriangleCanvasFramebuffer>& framebuffers,
            const std::vector<C_Position3D>& cameraPosition,
            const std::vector<C_CameraPosition2DIso>& cameraPositionIso
        )
        {
            for(int i = 0; i < entities.size(); i++) {
                // if(t)
                const C_TriangleCanvasFramebuffer& framebuffer =
                    framebuffers[i];

                framebuffer.bindTextures(0, 1);
                m_frameData.mvpMatrix =
                    calcProjectionMatrix() *
                    calcModelMatrix(
                        framebuffer.getResolution(),
                        framebuffer.getResolutionPlusBuffer(),
                        cameraPosition[i].pos_,
                        cameraPositionIso[i].pos_,
                        type
                    );
                // perhaps update someday
                  if(type.contains(
                    global.entityManager_->
                        getComponentType<C_TextureScrollPosition>()
                ))
                {
                    auto& textureScroll = EntityHandle{
                        entities[i]
                    }.get<C_TextureScrollPosition>();
                    // TODO: This should be updated elsewhere
                    m_frameData.textureOffset = textureScroll.position_;
                }
                else{
                    m_frameData.textureOffset = vec2(0);
                }
                m_bufferFrameData.subData(
                    0,
                    sizeof(FrameDataFramebuffer),
                    &m_frameData
                );
                ENG_API->glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                ENG_API->glDrawArrays(GL_TRIANGLES, 0, 6);
            }
        }
    private:
        ShaderProgram m_shaderProgram;
        Buffer m_bufferTexturedQuad;
        VAO m_vaoTexturedQuad;
        Buffer m_bufferFrameData;
        FrameDataFramebuffer m_frameData;

        ivec2 m_viewportThisFrame;

        virtual void beginExecute() override {

            m_viewportThisFrame = ivec2(
                global.systemManager_->get<SCREEN_VIEW>()->getViewportX(),
                global.systemManager_->get<SCREEN_VIEW>()->getViewportY()
            );
            bindDefaultFramebuffer();
            clearDefaultFramebuffer();
            m_shaderProgram.use();
            m_vaoTexturedQuad.bind();
        }

        virtual void endExecute() override {

        }

        void bindDefaultFramebuffer() {
            ENG_API->glBindFramebuffer(GL_FRAMEBUFFER, 0);
            ENG_API->glViewport(
                0,
                0,
                m_viewportThisFrame.x,
                m_viewportThisFrame.y
            );
            ENG_API->glEnable(GL_DEPTH_TEST);
            ENG_API->glDepthFunc(GL_LESS);
        }

        void clearDefaultFramebuffer() {
            ENG_API->glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            ENG_API->glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        }

        mat4 calcModelMatrix(
            ivec2 resolution,
            ivec2 resolutionPlusBuffer,
            vec3 cameraPosition,
            vec2 cameraPositionIso,
            Archetype type
        )
        {
            const int scaleFactor =
                global.systemManager_->get<SCREEN_VIEW>()->getScaleFactor();

            // also known as screen center
            float xOffset = m_viewportThisFrame.x / 2.0f;
            float yOffset = m_viewportThisFrame.y / 2.0f;
            vec2 offset =
                vec2(xOffset, yOffset) +
                (pos3DtoPos2DScreen(
                    cameraPosition,
                    global.systemManager_->get<SCREEN_VIEW>()->
                        getTriangleStepSizeScreen()
                ) * vec2(-1, 1) -
                ( global.systemManager_->get<SCREEN_VIEW>()->
                        getTriangleStepSizeScreen() / 2.0f
                ) * vec2(1, -1))
            ;

            mat4 model = glm::mat4(1.0f);

            // Pixel perfect part
            if(type.contains(
                global.entityManager_->
                    getComponentType<C_MainCanvas>()
            ))
            {
                vec2 fractComponentScaledNew =
                    glm::fract(cameraPositionIso) *
                    global.systemManager_->get<SCREEN_VIEW>()->
                        getTriangleStepSizeScreen() *
                        vec2(1, -1);

                offset += fractComponentScaledNew;
            }
            else if(type.contains(
                global.entityManager_->
                    getComponentType<C_BackgroundCanvas>()
            ))
            {
                // Need to offset by one pixel here but not exactly sure why atm
                offset += vec2(1.0f, -1.0f) * vec2(scaleFactor);
            }
            else{
                offset += (
                    global.systemManager_->get<SCREEN_VIEW>()->
                        getGlobalCameraOffsetScreen() *
                    vec2(1, -1)
                );
            }
            model = glm::translate(
                model,
                vec3(
                    offset.x,
                    offset.y,
                    0.0f
                )
            );
            model = glm::scale(
                model,
                vec3(
                    resolutionPlusBuffer.x * scaleFactor,
                    resolutionPlusBuffer.y * scaleFactor,
                    1.0f
                )
            );
            return model;
        }

        mat4 calcProjectionMatrix() {
            mat4 projection = glm::ortho(
                0.0f,
                (float)(m_viewportThisFrame.x),
                0.0f,
                (float)(m_viewportThisFrame.y),
                -1.0f,
                100.0f
            );
            return projection;
        }
    };

} // namespace IRECS

#endif /* SYSTEM_RENDERING_FRAMEBUFFERS_TO_SCREEN_H */
