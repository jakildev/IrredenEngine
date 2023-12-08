/*
 * Project: Irreden Engine
 * File: renderer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef RENDER_MANAGER_H
#define RENDER_MANAGER_H

#include <irreden/ir_ecs.hpp>
#include <irreden/ir_input.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/buffer.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <tuple>
#include <string>
#include <span>

using namespace IRComponents;
using IRECS::EntityId;

namespace IRRender {

    class RenderManager {
    public:
        RenderManager(IRInput::IRGLFWWindow& window);
        ~RenderManager() {}

        inline ivec2 getViewport() const { return m_viewport; }
        inline ivec2 getGameResolution() const { return m_gameResolution; }
        inline ivec2 getOutputResolution() const { return m_outputResolution; }
        inline int getOutputScaleFactor() const { return m_outputScaleFactor; }
        // TODO: Remove once a better render pipeline creator is in place
        // inline const Buffer& getBufferVoxelPositions() const { return m_bufferVoxelPositions; }
        // inline const Buffer& getBufferVoxelColors() const { return m_bufferVoxelColors; }

        EntityId getCanvas(std::string canvasName);
        vec2 getCameraPositionScreen() const;
        vec2 getCameraZoom() const;
        vec2 getCameraOffset2DIso() const;
        vec2 getTriangleStepSizeScreen() const;
        ivec2 getMainCanvasSizeTriangles() const;
        vec2 screenToOutputWindowOffset() const;

        void tick();

        void printGLSystemInfo();

        std::tuple<
            std::span<C_Position3D>,
            std::span<C_PositionOffset3D>,
            std::span<C_PositionGlobal3D>,
            std::span<C_Voxel>
        > allocateVoxels(
            unsigned int size,
            std::string canvasName = "main"
        );

        void deallocateVoxels(
            std::span<C_Position3D> positions,
            std::span<C_PositionOffset3D> positionsOffset,
            std::span<C_PositionGlobal3D> positionsGlobal,
            std::span<C_Voxel> voxels,
            std::string canvasName = "main"
        );


    private:
        IRInput::IRGLFWWindow& m_window;
        Buffer m_bufferUniformConstantsGLSL;
        // Buffer m_bufferVoxelPositions;
        // Buffer m_bufferVoxelColors;
        // EntityId m_backgroundCanvas;
        EntityId m_mainCanvas;
        // EntityId m_playerCanvas;
        EntityId m_camera;
        ivec2 m_viewport;
        ivec2 m_gameResolution;
        ivec2 m_outputResolution;
        int m_outputScaleFactor;
        std::unordered_map<std::string, IRECS::EntityId> m_canvasMap;

        void initRenderingSystems();
        void initRenderingResources();
        void updateOutputResolution();
    };

} // namespace IRRender

#endif /* RENDER_MANAGER_H */
