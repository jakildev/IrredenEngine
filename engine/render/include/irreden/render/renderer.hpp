/*
 * Project: Irreden Engine
 * File: renderer.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: October 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef RENDERER_H
#define RENDERER_H

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

namespace IRRender {

    class RenderManager {
    public:
        RenderManager(IRInput::IRGLFWWindow& window);
        ~RenderManager() {}

        IRECS::EntityId getCanvas(std::string canvasName);

        void tick();

        void printGLSystemInfo();

        std::tuple<
            std::span<C_Position3D>,
            std::span<C_PositionOffset3D>,
            std::span<C_PositionGlobal3D>,
            std::span<C_Voxel>
        > allocateVoxels(
            unsigned int size,
            std::string canvasName = "main");

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
        IRECS::EntityId m_backgroundCanvas;
        IRECS::EntityId m_mainCanvas;
        IRECS::EntityId m_playerCanvas;
        std::unordered_map<std::string, IRECS::EntityId> m_canvasMap;



        void initRenderingSystems();
    };

} // namespace IRRender

#endif /* RENDERER_H */
