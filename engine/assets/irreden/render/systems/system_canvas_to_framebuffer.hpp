/*
 * Project: Irreden Engine
 * File: \irreden-engine\src\systems\system_rendering_canvas_to_framebuffer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef SYSTEM_CANVAS_TO_FRAMEBUFFER_H
#define SYSTEM_CANVAS_TO_FRAMEBUFFER_H

#include <irreden/ecs/ir_system_base.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_constants.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/common/components/component_position_2d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include "..\components\component_zoom_level.hpp"
#include <irreden/render/components/component_triangle_framebuffer.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>

#include <irreden/update/systems/system_update_screen_view.hpp>

#include <glm/gtc/matrix_transform.hpp>

using namespace IRComponents;
using namespace IRRender;
using namespace IRMath;

// ADD ABLILITY TO TEXTURE OVER FACES!


namespace IRECS {

    template <>
    class IRSystem<RENDERING_CANVAS_TO_FRAMEBUFFER> : public IRSystemBase<
        RENDERING_CANVAS_TO_FRAMEBUFFER,
        C_TriangleCanvasTextures,
        C_TriangleCanvasFramebuffer,
        C_CameraPosition2DIso,
        C_ZoomLevel
    >   {
    public:
        IRSystem(
            ivec2 sceneSize
        )
        :   m_cameraOffset{- ivec2(m_isoTriangleScreenSize) / 2}
        ,   m_vertexBuffer {
                IRShapes2D::kQuadVertices,
                sizeof(IRShapes2D::kQuadVertices),
                0
            }
        ,   m_indexBuffer {
                IRShapes2D::kQuadIndices,
                sizeof(IRShapes2D::kQuadIndices),
                0
            }
        ,   m_frameDataBuffer{
                nullptr,
                kFrameDataUniformBufferSizeIsoTriangles,
                GL_DYNAMIC_STORAGE_BIT,
                GL_UNIFORM_BUFFER,
                kBufferIndex_FrameDataUniformIsoTriangles
            }
        ,   m_mpMatrix{mat4(1.0f)}
        ,   m_frameData{
                .mpMatrix_ = mat4(1)
            }
        ,   m_vao {
                m_vertexBuffer.getHandle(),
                m_indexBuffer.getHandle(),
                1,
                &kAttrFloat2
            }
        ,   m_shaderProgram {
                {
                    ShaderStage{
                        IRRender::kFileVertIsoTrianglesScreen,
                        GL_VERTEX_SHADER
                    }.getHandle(),
                    ShaderStage{
                        IRRender::kFileFragIsoTrianglesScreen,
                        GL_FRAGMENT_SHADER
                    }.getHandle()
                }
            }
        {
            IRProfile::engLogInfo(
                "Created system RENDERING_CANVAS_TO_FRAMEBUFFER"
            );
        }

        void tickWithArchetype(
            Archetype type,
            std::vector<EntityId>& entities,
            const std::vector<C_TriangleCanvasTextures>& triangleCanvasTextures,
            const std::vector<C_TriangleCanvasFramebuffer>& framebuffers,
            const std::vector<C_CameraPosition2DIso>& cameraPositions,
            const std::vector<C_ZoomLevel>& zoomLevels
        )
        {
            // Step 1: write all triangles to respective framebuffers
            for(int i = 0; i < entities.size(); i++) {
                vec2 framebufferResolution =
                    vec2(framebuffers[i].getResolutionPlusBuffer());
                mat4 projection = glm::ortho(
                    0.0f,
                    framebufferResolution.x,
                    0.0f,
                    framebufferResolution.y,
                    -1.0f,
                    100.0f
                );
                framebuffers[i].bindFramebuffer();
                framebuffers[i].clear();

                m_frameData.canvasZoomLevel_ =
                    IRECS::getSystem<SCREEN_VIEW>().getTriangleZoom() *
                    zoomLevels[i].zoom_;

                mat4 model = mat4(1.0f);
                model = glm::translate(
                    model,
                    glm::vec3(
                        framebufferResolution.x / 2,
                        framebufferResolution.y / 2,
                        0.0f
                    )
                );
                model = glm::scale(
                    model,
                    glm::vec3(
                        framebufferResolution.x *
                            m_frameData.canvasZoomLevel_.x,
                        framebufferResolution.y *
                            m_frameData.canvasZoomLevel_.y,
                        1.0f
                    )
                );
                m_frameData.mpMatrix_ = projection * model;
                m_frameData.canvasOffset_ = cameraPositions[i].pos_;


                m_frameData.textureOffset_ = vec2(0);
                m_frameDataBuffer.subData(
                    0,
                    kFrameDataUniformBufferSizeIsoTriangles,
                    &m_frameData
                );
                triangleCanvasTextures[i].bind(0, 1);

                ENG_API->glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
                ENG_API->glDrawElements(
                    GL_TRIANGLES,
                    IRShapes2D::kQuadIndicesLength,
                    GL_UNSIGNED_SHORT,
                    nullptr
                );
            }
        }
    private:
        uvec2 m_isoTriangleScreenSize;
        ivec2 m_cameraOffset;
        Buffer m_vertexBuffer;
        Buffer m_indexBuffer;
        Buffer m_frameDataBuffer;
        VAO m_vao;
        ShaderProgram m_shaderProgram;
        mat4 m_mpMatrix;
        FrameDataIsoTriangles m_frameData;
        int m_executeCount;

        virtual void beginExecute() override {
            m_shaderProgram.use();
            m_vao.bind();

        }


        virtual void endExecute() override {

        }
    };

} // namespace IRSystem


#endif /* SYSTEM_CANVAS_TO_FRAMEBUFFER_H */
