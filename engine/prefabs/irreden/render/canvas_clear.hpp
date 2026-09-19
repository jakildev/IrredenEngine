#ifndef CANVAS_CLEAR_H
#define CANVAS_CLEAR_H

#include <irreden/ir_constants.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_triangle_canvas_background.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <cstdint>

namespace IRSystem {

// Per-frame reset of a trixel canvas before its first producer writes: color
// (or the canvas's authored background), entity ids, and the distance buffer
// back to kTrixelDistanceMaxDistance. The distance reset goes through the
// device clearTexImage, never Texture2D::clear: on Metal Texture2D::clear does
// not reliably clear R32I textures, and only clearTexImage mirrors the
// sentinel into the image-atomic scratch that imageAtomicMin writers compare
// against. Every producer that owns a canvas's clear (the voxel raster, the
// shape raster of a shape-only canvas) resets through this one path.
inline void clearCanvasAndDistances(
    IREntity::EntityId canvasEntity, IRComponents::C_TriangleCanvasTextures &canvas
) {
    auto background =
        IREntity::getComponentOptional<IRComponents::C_TriangleCanvasBackground>(canvasEntity);
    if (background.has_value()) {
        (*background.value()).clearCanvasWithBackground(canvas);
    } else {
        canvas.clear();
    }
    // The distance buffer resets every frame regardless of whether the
    // background updated the color canvas: clearCanvasWithBackground may skip a
    // throttled frame or no-op, while producers write per-pixel distances every
    // frame.
    static constexpr std::int32_t kDistanceClear =
        static_cast<std::int32_t>(IRConstants::kTrixelDistanceMaxDistance);
    IRRender::device()->clearTexImage(canvas.getTextureDistances(), 0, &kDistanceClear);
}

} // namespace IRSystem

#endif /* CANVAS_CLEAR_H */
