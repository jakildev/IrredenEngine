/*
 * Project: Irreden Engine
 * File: renderer.cpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#include <irreden/ir_render.hpp>

#include <irreden/ir_window.hpp>

#include <irreden/render/render_manager.hpp>
#include <irreden/render/ir_gl_api.hpp>

#include <irreden/render/entities/entity_voxel_pool_canvas.hpp>
#include <irreden/render/entities/entity_framebuffer.hpp>

#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_2d_iso.hpp>
#include <irreden/update/components/component_velocity_2d_iso.hpp>
#include <irreden/render/components/component_camera.hpp>

namespace IRRender {

    

    RenderManager::RenderManager(
        ivec2 gameResolution,
        FitMode fitMode
    )
    :   m_globalConstantsGLSL{

        }
    ,   m_bufferUniformConstantsGLSL{
            &m_globalConstantsGLSL,
            sizeof(GlobalConstantsGLSL),
            GL_NONE,
            GL_UNIFORM_BUFFER,
            kBufferIndex_GlobalConstantsGLSL
        }
    // ,   m_backgroundCanvas{
    //         IRECS::createEntity<kVoxelPoolCanvas>(
    //             "background",
    //             ivec2(
    //                 IRConstants::kScreenTrixelMaxCanvasSize /
    //                 uvec2(2)
    //             ),
    //             ivec3(8, 8, 8),
    //             IRConstants::kGameResolution,
    //             IRConstants::kSizeExtraPixelNoBuffer,
    //             2.0f
    //         )
    //     }
    ,   m_mainFramebuffer{
            IREntity::createEntity<kFramebuffer>(
                "mainFramebuffer",
                gameResolution,
                IRConstants::kSizeExtraPixelBuffer
            )
        }
    ,   m_mainCanvas{
            IREntity::createEntity<kVoxelPoolCanvas>(
                "main",
                IRConstants::kVoxelPoolSize,
                IRMath::gameResolutionToSize2DIso(
                    gameResolution + IRConstants::kSizeExtraPixelBuffer
                ),
                m_mainFramebuffer
            )
        }
    // ,   m_playerCanvas{
    //         IRECS::createEntity<kVoxelPoolCanvas>(
    //             "player",
    //             ivec2(
    //                 IRConstants::kScreenTrixelMaxCanvasSizeWithBuffer
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
    ,   m_gameResolution{gameResolution}
    ,   m_outputResolution{0}
    ,   m_fitMode{fitMode}
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
    ,   m_renderImpl{createRenderer()}
    {
        IRE_LOG_INFO("Fit mode: {}", static_cast<int>(fitMode));
        m_renderImpl->init();
        IREntity::setName(m_camera, "camera");
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
        //         ivec2(IRConstants::kScreenTrixelMaxCanvasSize) /
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

        IRWindow::getWindowSize(m_viewport);
        updateOutputResolution();
        IRSystem::executePipeline(IRTime::Events::RENDER);
        IRWindow::getWindow().swapBuffers();
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
            return IREntity::getComponent<C_VoxelPool>(m_mainCanvas).allocateVoxels(
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
        return IREntity::getComponent<C_Position2DIso>(m_camera).pos_;
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
        return IREntity::getComponent<C_ZoomLevel>(m_camera).zoom_;
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
            IREntity::getComponent<C_VoxelPool>(m_mainCanvas).deallocateVoxels(
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
        return IREntity::getComponent<C_SizeTriangles>(m_mainCanvas).size_;
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

    void RenderManager::printRenderInfo() {
        m_renderImpl->printInfo();
    }

    ivec2 RenderManager::calcOutputScaleByMode()
    {
        if(m_fitMode == FitMode::FIT) {
            return ivec2(glm::min(
                glm::floor(
                    m_viewport.x /
                    m_gameResolution.x
                ),
                glm::floor(
                    m_viewport.y /
                    m_gameResolution.y
                )
            ));
        }
        if(m_fitMode == FitMode::STRETCH) {
            return ivec2(
                glm::floor(
                    m_viewport.x /
                    m_gameResolution.x
                ),
                glm::floor(
                    m_viewport.y /
                    m_gameResolution.y
                )
            );
        }
        IR_ASSERT(false, "Unexpected FitMode type");
        return ivec2(1);

    }

    void RenderManager::updateOutputResolution() {
        m_outputScaleFactor = calcOutputScaleByMode();
        m_outputResolution = ivec2(
            m_gameResolution.x * m_outputScaleFactor.x,
            m_gameResolution.y * m_outputScaleFactor.y
        );
    }

    vec2 RenderManager::screenToOutputWindowOffset() const {
        return vec2(
            m_viewport.x - m_outputResolution.x,
            m_viewport.y - m_outputResolution.y
        ) / vec2(2);
    }

} // namespace IRRender