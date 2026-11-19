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
// back to kTrixelDistanceMaxDistance. The distance reset goes through
// C_TriangleCanvasTextures::clearDistancesToEmpty, never Texture2D::clear (see
// there). Every producer that owns a canvas's clear (the voxel raster, the
// shape raster of a shape-only canvas) resets through this one path.
inline void clearCanvasAndDistances(
    IREntity::EntityId canvasEntity, IRComponents::C_TriangleCanvasTextures &canvas
) {
    canvas.shapeGeometry_.invalidateSamples();
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
    canvas.clearDistancesToEmpty();
}

} // namespace IRSystem

#endif /* CANVAS_CLEAR_H */
