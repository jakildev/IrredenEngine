#ifndef COMPONENT_CANVAS_DRAW_IMAGES_H
#define COMPONENT_CANVAS_DRAW_IMAGES_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/texture.hpp>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace IRComponents {

// World-scoped singleton owning the images `IRPrefab::CanvasDraw::loadImage`
// decoded, each one a `MipAtlas` texture `System<CANVAS_DRAW>` samples. A
// handle is never reused, so a draw queued against a released image finds
// nothing and is dropped. An image whose loader uploaded no texture draws
// nothing.
struct C_CanvasDrawImages {
    struct Image {
        std::uint32_t handle_ = 0;
        // Loads of `path_` not yet released; the texture is freed at zero.
        int references_ = 0;
        std::string path_;
        // The decoded file's pixel size, before any fit to the size limit.
        IRMath::ivec2 sourceSize_ = IRMath::ivec2(0);
        IRMath::ivec2 baseSize_ = IRMath::ivec2(0);
        int levelCount_ = 0;
        std::pair<IRRender::ResourceId, IRRender::Texture2D *> texture_{0, nullptr};
    };

    std::vector<Image> images_;
    std::uint32_t nextHandle_ = 1;

    void onDestroy() {
        for (Image &image : images_) {
            if (image.texture_.second != nullptr) {
                IRRender::destroyResource<IRRender::Texture2D>(image.texture_.first);
            }
        }
        images_.clear();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_DRAW_IMAGES_H */
