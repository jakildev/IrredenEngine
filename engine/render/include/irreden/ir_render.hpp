#ifndef IR_RENDER_H
#define IR_RENDER_H

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

    template <typename T, typename... Args>
    std::pair<ResourceId, T*> createResource(Args&&... args) {
        return getRenderingResourceManager().create<T>(std::forward<Args>(args)...);
    }

    template <typename T>
    void destroyResource(ResourceId resource) {
        getRenderingResourceManager().destroy<T>(resource);
    }

} // namespace IRRender

#endif /* IR_RENDER_H */
