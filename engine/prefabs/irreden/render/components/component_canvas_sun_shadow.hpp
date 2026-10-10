#ifndef COMPONENT_CANVAS_SUN_SHADOW_H
#define COMPONENT_CANVAS_SUN_SHADOW_H

// Canvas-sized directional shadow texture populated by COMPUTE_SUN_SHADOW
// and consumed by LIGHTING_TO_TRIXEL. Opt-in: attach alongside
// C_TriangleCanvasTextures + C_CanvasAOTexture to enable sun shadows for a
// canvas. Format is RGBA8 rather than R8 so Metal's rgba8 shader access
// path can share a single binding-layout with the AO texture. R is visibility;
// A is zero for legacy receivers or (FaceId + 1) / 255 for exact box normals,
// including frames with shadows disabled. G/B are reserved.

#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/texture.hpp>

#include <vector>

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

    C_CanvasSunShadow() = delete;

    void onDestroy() {
        IRRender::destroyResource<Texture2D>(textureShadow_.first);
    }

    const Texture2D *getTexture() const {
        return textureShadow_.second;
    }

    // RGBA8 shadow-factor texels after a full GPU flush, row-major over the
    // whole canvas. A full flush per call — debug probes only.
    void readFactorsSynced(std::vector<Color> &out) const {
        IRRender::device()->finish();
        const uvec2 size = textureShadow_.second->getSize();
        out.resize(static_cast<std::size_t>(size.x) * static_cast<std::size_t>(size.y));
        textureShadow_.second->getSubImage2D(
            0,
            0,
            size.x,
            size.y,
            PixelDataFormat::RGBA,
            PixelDataType::UNSIGNED_BYTE,
            out.data()
        );
    }
};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_SUN_SHADOW_H */
