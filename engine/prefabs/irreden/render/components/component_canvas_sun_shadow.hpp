#ifndef COMPONENT_CANVAS_SUN_SHADOW_H
#define COMPONENT_CANVAS_SUN_SHADOW_H

// Canvas-sized directional shadow texture populated by COMPUTE_SUN_SHADOW
// and consumed by LIGHTING_TO_TRIXEL. Opt-in: attach alongside
// C_TriangleCanvasTextures + C_CanvasAOTexture to enable sun shadows for a
// canvas. Format is RGBA8 rather than R8 so Metal's rgba8 shader access
// path can share a single binding-layout with the AO texture.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

using namespace IRMath;
using namespace IRRender;

namespace IRComponents {

struct C_CanvasSunShadow {
    std::pair<ResourceId, Texture2D *> textureShadow_;

    C_CanvasSunShadow(ivec2 size)
        : textureShadow_{IRRender::createResource<IRRender::Texture2D>(
              TextureKind::TEXTURE_2D,
              size.x,
              size.y,
              TextureFormat::RGBA8,
              TextureWrap::CLAMP_TO_EDGE,
              TextureFilter::NEAREST
          )} {}

    // Required by EntityManager::setComponent, which default-constructs the
    // destination slot before assigning the concrete sized component.
    C_CanvasSunShadow() = default;

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(textureShadow_.first);
    }

    const Texture2D *getTexture() const {
        IR_ASSERT(
            textureShadow_.second != nullptr,
            "C_CanvasSunShadow::getTexture() called on default-constructed "
            "instance — must be constructed with a size."
        );
        return textureShadow_.second;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_SUN_SHADOW_H */
