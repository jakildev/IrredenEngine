/*
 * Project: Irreden Engine
 * File: ir_render.hpp
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: November 2023
 * -----
 * Modified By: <your_name> <Month> <YYYY>
 */

#ifndef IR_RENDER_H
#define IR_RENDER_H

#include <irreden/render/render_manager.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/rendering_rm.hpp>
#include <irreden/render/buffer.hpp>
#include <irreden/render/texture.hpp>
#include <irreden/render/vao.hpp>
#include <irreden/render/shapes_2d.hpp>
#include <irreden/render/shader.hpp>
#include <irreden/render/shader_names.hpp>
#include <irreden/render/image_data.hpp>

namespace IRRender {

    extern RenderingResourceManager* g_renderingResourceManager;
    RenderingResourceManager& getRenderingResourceManager();

    extern RenderManager* g_renderManager;
    RenderManager& getRenderManager();

    template <typename T, typename... Args>
    std::pair<ResourceId, T*> createResource(Args&&... args) {
        return getRenderingResourceManager().create<T>(std::forward<Args>(args)...);
    }

    template <typename T, typename... Args>
    std::pair<ResourceId, T*> createNamedResource(
        const std::string& name,
        Args&&... args
    ) {
        return getRenderingResourceManager().createNamed<T>(
            name,
            std::forward<Args>(args)...
        );
    }

    template <typename T>
    void destroyResource(ResourceId resource) {
        getRenderingResourceManager().destroy<T>(resource);
    }

    template <typename T>
    T* getResource(ResourceId resource) {
        return getRenderingResourceManager().get<T>(resource);
    }

    template <typename T>
    T* getNamedResource(std::string resourceName) {
        return getRenderingResourceManager().getNamed<T>(resourceName);
    }

    inline std::tuple<
        std::span<C_Position3D>,
        std::span<C_PositionOffset3D>,
        std::span<C_PositionGlobal3D>,
        std::span<C_Voxel>
    > allocateVoxels(
        unsigned int size,
        std::string canvasName = "main"
    )
    {
        return getRenderManager().allocateVoxels(size, canvasName);
    }

    inline void deallocateVoxels(
        std::span<C_Position3D> positions,
        std::span<C_PositionOffset3D> positionOffsets,
        std::span<C_PositionGlobal3D> positionGlobals,
        std::span<C_Voxel> voxels,
        std::string canvasName = "main"
    )
    {
        getRenderManager().deallocateVoxels(
            positions,
            positionOffsets,
            positionGlobals,
            voxels,
            canvasName
        );
    }

    inline IREntity::EntityId getCanvas(std::string canvasName) {
        return getRenderManager().getCanvas(canvasName);
    }

    vec2 getCameraPosition2DIso();
    vec2 getCameraZoom();
    vec2 getTriangleStepSizeScreen();
    ivec2 getViewport();
    ivec2 getOutputScaleFactor();
    vec2 getMousePositionOutputView();
    vec2 getGameResolution();
    vec2 getMainCanvasSizeTrixels();
    // Mouse position in iso coordinates as it appears on the screen
    vec2 mousePosition2DIsoScreenRender();
    // Mouse position in iso coordinates as it appears in the world
    // (so with camera offset)
    vec2 mousePosition2DIsoWorldRender();
    vec2 mousePosition2DIsoUpdate();
    ivec2 mouseTrixelPositionWorld();



} // namespace IRRender

#endif /* IR_RENDER_H */
