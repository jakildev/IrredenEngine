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

// #include <irreden/ir_input.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/renderer_impl.hpp>

#include <irreden/common/components/component_position_3d.hpp>
#include <irreden/common/components/component_position_offset_3d.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/voxel/components/component_voxel.hpp>

#include <Metal/Metal.hpp>

#include <tuple>
#include <string>
#include <span>

using namespace IRComponents;
using namespace IREntity;

namespace IRRender {

    class RenderManager {
    public:
        RenderManager(
            ivec2 gameResolution,
            FitMode fitMode = FitMode::FIT
        );
        ~RenderManager() {}

        inline ivec2 getViewport() const { return m_viewport; }
        inline ivec2 getGameResolution() const { return m_gameResolution; }
        inline ivec2 getOutputResolution() const { return m_outputResolution; }
        inline ivec2 getOutputScaleFactor() const { return m_outputScaleFactor; }
        // TODO: Remove once a better render pipeline creator is in place
        // inline const Buffer& getBufferVoxelPositions() const { return m_bufferVoxelPositions; }
        // inline const Buffer& getBufferVoxelColors() const { return m_bufferVoxelColors; }

        EntityId getCanvas(std::string canvasName);
        vec2 getCameraPosition2DIso() const;
        vec2 getCameraZoom() const;
        vec2 getTriangleStepSizeScreen() const;
        vec2 getTriangleStepSizeGameResolution() const;
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
        // tmp
        GlobalConstantsGLSL m_globalConstantsGLSL;
        Buffer m_bufferUniformConstantsGLSL;
        // Buffer m_bufferVoxelPositions;
        // Buffer m_bufferVoxelColors;
        // EntityId m_backgroundCanvas;
        EntityId m_mainFramebuffer; // TODO: Left off here
        EntityId m_mainCanvas;
        // EntityId m_playerCanvas;
        EntityId m_camera;
        ivec2 m_viewport;
        ivec2 m_gameResolution;
        ivec2 m_outputResolution;
        ivec2 m_outputScaleFactor;
        std::unordered_map<std::string, EntityId> m_canvasMap;
        FitMode m_fitMode;
        std::unique_ptr<RenderImpl> m_renderImpl;

        void initRenderingSystems();
        void initRenderingResources();
        void updateOutputResolution();

        ivec2 calcOutputScaleByMode();


    };

} // namespace IRRender

#endif /* RENDER_MANAGER_H */
