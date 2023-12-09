/*
 * Project: Irreden Engine
 * File: renderer.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/render_manager.hpp>
#include <irreden/render/ir_gl_api.hpp>

#include <irreden/render/entities/entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>
#include <irreden/render/components/component_camera.hpp>

namespace IRRender {

    RenderManager::RenderManager(
        IRGLFWWindow& window
    )
    :   m_window{window}
    ,   m_bufferUniformConstantsGLSL{
            &kGlobalConstantsGLSL,
            sizeof(GlobalConstantsGLSL),
            GL_NONE,
            GL_UNIFORM_BUFFER,
            kBufferIndex_GlobalConstantsGLSL
        }
    // ,   m_backgroundCanvas{
    //         IRECS::createEntity<kCanvas>(
    //             "background",
    //             ivec2(
    //                 IRConstants::kScreenTriangleMaxCanvasSize /
    //                 uvec2(2)
    //             ),
    //             ivec3(8, 8, 8),
    //             IRConstants::kGameResolution,
    //             IRConstants::kSizeExtraPixelNoBuffer,
    //             2.0f
    //         )
    //     }
    ,   m_mainCanvas{
            IRECS::createEntity<kCanvas>(
                "main",
                ivec2(
                    IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
                ),
                IRConstants::kVoxelPoolSize,
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelBuffer
            )
        }
    // ,   m_playerCanvas{
    //         IRECS::createEntity<kCanvas>(
    //             "player",
    //             ivec2(
    //                 IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
    //             ),
    //             IRConstants::kVoxelPoolSize / ivec3(2),
    //             IRConstants::kGameResolution,
    //             IRConstants::kSizeExtraPixelBuffer
    //         )
    //     }
    ,   m_camera{
            createEntity(
                C_Camera{},
                C_Position2DIso{
                    vec2(0.0f, 0.0f)
                },
                C_Velocity2DIso{
                    vec2(0.0f, 0.0f)
                },
                C_ZoomLevel{1.0f}
            )
        }
    ,   m_viewport{0}
    ,   m_gameResolution{IRConstants::kGameResolution}
    ,   m_outputResolution{0}
    // ,   m_bufferVoxelPositions{
    //         nullptr,
    //         IRConstants::kMaxSingleVoxels * sizeof(C_Position3D),
    //         GL_DYNAMIC_STORAGE_BIT,
    //         GL_SHADER_STORAGE_BUFFER,
    //         kBufferIndex_SingleVoxelPositions
    //     }
    // ,   m_bufferVoxelColors{
    //         nullptr,
    //         IRConstants::kMaxSingleVoxels * sizeof(C_Voxel),
    //         GL_DYNAMIC_STORAGE_BIT,
    //         GL_SHADER_STORAGE_BUFFER,
    //         kBufferIndex_SingleVoxelColors
    //     }
    {

        IRECS::setName(m_camera, "camera");
        std::vector<Color> colorPalette = {
            kPinkTanOrange[1],
            IRColors::kBlack
        };
        // IRECS::setComponent(
        //     m_backgroundCanvas,
        //     C_TextureScrollPosition{
        //         vec2(0.0f)
        //     }
        // );
        // IRECS::setComponent(
        //     m_backgroundCanvas,
        //     C_TextureScrollVelocity{}
        // );
        // IRECS::setComponent(
        //     m_backgroundCanvas,
        //     C_TriangleCanvasBackground{
        //         BackgroundTypes::kGradientRandom,
        //         colorPalette,
        //         ivec2(IRConstants::kScreenTriangleMaxCanvasSize) /
        //             ivec2(2)
        //     }
        // );
        // m_canvasMap["background"] = m_backgroundCanvas;
        m_canvasMap["main"] = m_mainCanvas;
        // m_canvasMap["player"] = m_playerCanvas;

        initRenderingResources();
        initRenderingSystems();
        g_renderManager = this;
        IRE_LOG_INFO("Created renderer.");
    }

    void RenderManager::tick() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        IRInput::getWindowSize(m_viewport);
        updateOutputResolution();
        IRECS::executePipeline(SYSTEM_TYPE_RENDER);
        m_window.swapBuffers();
    }

    std::tuple<
            std::span<C_Position3D>,
            std::span<C_PositionOffset3D>,
            std::span<C_PositionGlobal3D>,
            std::span<C_Voxel>
    > RenderManager::allocateVoxels(
        unsigned int numVoxels,
        std::string canvasName
    )
    {
        if(canvasName == "main") {
            return IRECS::getComponent<C_VoxelPool>(m_mainCanvas).allocateVoxels(
                numVoxels
            );
        }

        IR_ASSERT(false, "only allocating main voxels for now.");
        return std::make_tuple(
            std::span<C_Position3D>{},
            std::span<C_PositionOffset3D>{},
            std::span<C_PositionGlobal3D>{},
            std::span<C_Voxel>{}
        );
    }

    vec2 RenderManager::getCameraPosition2DIso() const {
        return IRECS::getComponent<C_Position2DIso>(m_camera).pos_;
    }

    vec2 RenderManager::getTriangleStepSizeScreen() const {
        return IRMath::calcTriangleStepSizeScreen(
            m_gameResolution,
            getCameraZoom(),
            m_outputScaleFactor
        );
    }

    vec2 RenderManager::getTriangleStepSizeGameResolution() const {
        return IRMath::calcTriangleStepSizeGameResolution(
            m_gameResolution,
            getCameraZoom()
        );
    }

    vec2 RenderManager::getCameraZoom() const {
        return IRECS::getComponent<C_ZoomLevel>(m_camera).zoom_;
    }

    void RenderManager::deallocateVoxels(
        std::span<C_Position3D> positions,
        std::span<C_PositionOffset3D> positionOffsets,
        std::span<C_PositionGlobal3D> positionGlobals,
        std::span<C_Voxel> voxels,
        std::string canvasName
    )
    {
        if(canvasName == "main") {
            IRECS::getComponent<C_VoxelPool>(m_mainCanvas).deallocateVoxels(
                positions,
                positionOffsets,
                positionGlobals,
                voxels
            );
            return;
        }

        IR_ASSERT(false, "only deallocating main voxels for now.");
    }

    EntityId RenderManager::getCanvas(std::string canvasName) {
        return m_canvasMap[canvasName];
    }

    ivec2 RenderManager::getMainCanvasSizeTriangles() const {
        return IRECS::getComponent<C_SizeTriangles>(m_mainCanvas).size_;
    }

    void RenderManager::initRenderingResources() {
        Buffer* vertexBuffer = IRRender::createResource<Buffer>(
            IRShapes2D::kQuadVertices,
            sizeof(IRShapes2D::kQuadVertices),
            0
        ).second;
        Buffer* indexBuffer = IRRender::createResource<Buffer>(
            IRShapes2D::kQuadIndices,
            sizeof(IRShapes2D::kQuadIndices),
            0
        ).second;
        IRRender::createNamedResource<VAO>(
            "QuadVAO",
            vertexBuffer->getHandle(),
            indexBuffer->getHandle(),
            1,
            &kAttrFloat2
        );

        Buffer* vertexBufferTextured = IRRender::createResource<Buffer>(
            IRShapes2D::k2DQuadTextured,
            sizeof(IRShapes2D::k2DQuadTextured),
            0
        ).second;
        IRRender::createNamedResource<VAO>(
            "QuadVAOArrays",
            vertexBufferTextured->getHandle(),
            0,
            2,
            kAttrList2Float2
        );

    }

    void RenderManager::initRenderingSystems() {

    }

    void RenderManager::printGLSystemInfo() {
        int intAttr;
        ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
        IRE_LOG_INFO(
            "Maximum nr of vertex attributes supported: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
        IRE_LOG_INFO(
            "Max 3d texture size: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
        IRE_LOG_INFO(
            "Max uniform block size: {}",
            intAttr
        );
    }

    void RenderManager::updateOutputResolution() {
        m_outputScaleFactor =  glm::min(
            glm::floor(
                m_viewport.x /
                m_gameResolution.x
            ),
            glm::floor(
                m_viewport.y /
                m_gameResolution.y
            )
        );
        m_outputResolution = ivec2(
            m_gameResolution.x * m_outputScaleFactor,
            m_gameResolution.y * m_outputScaleFactor
        );
        // IRECS::getComponent<C_Camera>(m_camera).setTriangleStepSizeScreen(
        //     vec2(m_gameResolution), m_outputScaleFactor
        // );
    }

    vec2 RenderManager::screenToOutputWindowOffset() const {
        return vec2(
            m_viewport.x - m_outputResolution.x,
            m_viewport.y - m_outputResolution.y
        ) / vec2(2);
    }

} // namespace IRRender