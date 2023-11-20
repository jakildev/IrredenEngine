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

    template <typename T>
    void destroyResource(ResourceId resource) {
        getRenderingResourceManager().destroy<T>(resource);
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

    inline IRECS::EntityId getCanvas(std::string canvasName) {
        return getRenderManager().getCanvas(canvasName);
    }

    vec2 getCameraPositionScreen();
    vec2 getCameraZoom();
    vec2 getTriangleStepSizeScreen();
    ivec2 getViewport();
    int getOutputScaleFactor();

} // namespace IRRender

#endif /* IR_RENDER_H */
