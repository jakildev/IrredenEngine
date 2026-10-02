#ifndef CANVAS_COVERAGE_H
#define CANVAS_COVERAGE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

namespace IRPrefab::CanvasCoverage {

inline IRMath::ivec2 logicalSize(IREntity::EntityId canvas, IRMath::ivec2 backingSize) {
    return canvas == IRRender::getCanvas("main")
               ? IRMath::ivec2(IRRender::getMainCanvasSizeTrixels())
               : backingSize;
}

// Producers call before acquiring per-canvas references. Retained capacity keeps
// zoom/subdivision transitions from reallocating a previously covered viewport.
inline bool syncMainBacking() {
    const auto main = IRRender::getCanvas("main");
    auto &textures = IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(main);
    const auto required = IRMath::trixelCanvasBackingSize(
        IRMath::ivec2(IRRender::getMainCanvasSizeTrixels()),
        IRRender::getCameraZoom(),
        IRRender::getVoxelRenderEffectiveSubdivisions()
    );
    const auto size = IRMath::max(textures.size_, required);
    if (size == textures.size_)
        return false;
    textures.resizeBacking(size);
    auto ao = IREntity::getComponentOptional<IRComponents::C_CanvasAOTexture>(main);
    if (ao.has_value()) {
        auto replacement = IRComponents::C_CanvasAOTexture{size};
        ao.value()->onDestroy();
        *ao.value() = replacement;
    }
    auto shadow = IREntity::getComponentOptional<IRComponents::C_CanvasSunShadow>(main);
    if (shadow.has_value()) {
        auto replacement = IRComponents::C_CanvasSunShadow{size};
        shadow.value()->onDestroy();
        *shadow.value() = replacement;
    }
    return true;
}

} // namespace IRPrefab::CanvasCoverage

#endif /* CANVAS_COVERAGE_H */
