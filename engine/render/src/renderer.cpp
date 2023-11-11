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

#include <irreden/render/renderer.hpp>
#include <irreden/render/ir_gl_api.hpp>

#include <irreden/render/entities/entity_canvas.hpp>
#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_texture_scroll.hpp>
#include <irreden/update/systems/system_update_screen_view.hpp>
#include <irreden/input/systems/system_input_key_mouse.hpp>

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
    ,   m_backgroundCanvas{
            IRECS::createPrefab<kCanvas>(
                "background",
                ivec2(
                    IRConstants::kScreenTriangleMaxCanvasSize /
                    uvec2(2)
                ),
                ivec3(8, 8, 8),
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelNoBuffer,
                2.0f
            )
        }
    ,   m_mainCanvas{
            IRECS::createPrefab<kCanvas>(
                "main",
                ivec2(
                    IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
                ),
                IRConstants::kVoxelPoolSize,
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelBuffer
            )
        }
    ,   m_playerCanvas{
            IRECS::createPrefab<kCanvas>(
                "player",
                ivec2(
                    IRConstants::kScreenTriangleMaxCanvasSizeWithBuffer
                ),
                IRConstants::kVoxelPoolSize / ivec3(2),
                IRConstants::kGameResolution,
                IRConstants::kSizeExtraPixelBuffer
            )
        }
    {
        std::vector<Color> colorPalette = {
            kPinkTanOrange[1],
            IRColors::kBlack
        };
        IRECS::setComponent(
            m_backgroundCanvas,
            C_TextureScrollPosition{
                vec2(0.0f)
            }
        );
        IRECS::setComponent(
            m_backgroundCanvas,
            C_TextureScrollVelocity{}
        );
        IRECS::setComponent(
            m_backgroundCanvas,
            C_TriangleCanvasBackground{
                BackgroundTypes::kGradientRandom,
                colorPalette,
                ivec2(IRConstants::kScreenTriangleMaxCanvasSize) /
                    ivec2(2)
            }
        );
        m_canvasMap["background"] = m_backgroundCanvas;
        m_canvasMap["main"] = m_mainCanvas;
        m_canvasMap["player"] = m_playerCanvas;

        initRenderingSystems();
        g_renderManager = this;
        IRProfile::engLogInfo("Created renderer.");
    }

    void RenderManager::tick() {
        IR_PROFILE_FUNCTION(IR_PROFILER_COLOR_RENDER);

        IRECS::getEngineSystem<INPUT_KEY_MOUSE>().beginRenderExecute(); // TODO: not like this after system CRTP
        IRECS::getEngineSystem<SCREEN_VIEW>().beginExecuteRender();

        IRECS::getSystemManager().executeGroup<SYSTEM_TYPE_RENDER>();
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

    void RenderManager::initRenderingSystems() {

    }

    void RenderManager::printGLSystemInfo() {
        int intAttr;
        ENG_API->glGetIntegerv(GL_MAX_VERTEX_ATTRIBS, &intAttr);
        IRProfile::engLogInfo(
            "Maximum nr of vertex attributes supported: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_3D_TEXTURE_SIZE, &intAttr);
        IRProfile::engLogInfo(
            "Max 3d texture size: {}",
            intAttr
        );
        ENG_API->glGetIntegerv(GL_MAX_UNIFORM_BLOCK_SIZE, &intAttr);
        IRProfile::engLogInfo(
            "Max uniform block size: {}",
            intAttr
        );
    }

} // namespace IRRender