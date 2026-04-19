#ifndef COMPONENT_CANVAS_AO_TEXTURE_H
#define COMPONENT_CANVAS_AO_TEXTURE_H

// Canvas-sized ambient-occlusion texture populated by COMPUTE_VOXEL_AO and
// consumed by LIGHTING_TO_TRIXEL. Opt-in: attach alongside
// C_TriangleCanvasTextures to enable AO for a canvas.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

struct C_CanvasAOTexture {
    std::pair<ResourceId, Texture2D *> textureAO_;

    C_CanvasAOTexture(ivec2 size)
        : textureAO_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              size.x,
              size.y,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )} {}

    C_CanvasAOTexture() {}

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(textureAO_.first);
    }

    const Texture2D *getTexture() const {
        IR_ASSERT(
            textureAO_.second != nullptr,
            "C_CanvasAOTexture::getTexture() called on default-constructed "
            "instance — must be constructed with a size."
        );
        return textureAO_.second;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_AO_TEXTURE_H */
