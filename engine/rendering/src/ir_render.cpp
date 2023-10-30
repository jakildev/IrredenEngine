#include <irreden/ir_render.hpp>
#include <irreden/render/rendering_rm.hpp>

namespace IRRender {
    RenderingResourceManager& getRenderingResourceManager() {
        return RenderingResourceManager::instance();
    }

    template <typename T, typename... Args>
    std::pair<ResourceId, T*> createResource(Args&&... args) {
        return getRenderingResourceManager().create<T>(
            std::forward<Args>(args)...
        );
    }

    template <typename T>
    void destroyResource(ResourceId resource) {
        getRenderingResourceManager().destroy<T>(resource);
    }

} // namespace IRRender

