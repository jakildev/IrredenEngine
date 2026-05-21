#ifndef MASK_GRID_PAINTER_H
#define MASK_GRID_PAINTER_H

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_profile.hpp>

#include <optional>
#include <vector>

namespace IRRender {

// Per-system scratch storage for batched mask-grid uploads; reused across
// XZ/YZ calls to avoid per-frame allocation.
struct MaskGridPaintScratch {
    std::vector<IRMath::Color> colors_;
    std::vector<int> distances_;
};

// Paint a boolean-mask grid onto a canvas as a rectangle of cellPx-sized
// cells. Each mask cell expands to a cellPx × cellPx block; cells where
// mask[h + v * gridSize.x] is true take filledColor, false takes
// emptyColor. The mask is stored with row 0 = canvas-bottom (display
// orientation flipped vertically vs. the mask index), matching the
// authoring convention where a mask "row" is a slice along the +Z axis.
//
// origin is the canvas-space top-left pixel of the grid region.
// distance is the trixel-distance value written for every painted pixel;
// callers in the GUI canvas typically pass kWidgetBackgroundDistance.
//
// scratch is reused across calls so only the first call (or one growing
// past the prior largest size) allocates; subsequent calls of equal or
// smaller area pay no allocation cost.
//
// No-op when the requested rect falls entirely outside the canvas. A
// partial off-canvas region is rejected wholesale rather than partially
// clipped — the GUI grid layouts that drive this helper always fit by
// construction, so the simpler whole-rect guard is preferable to the
// per-row clip arithmetic.
inline void drawMaskGridOntoCanvas(
    IRComponents::C_TriangleCanvasTextures &canvas,
    const std::vector<bool> &mask,
    IRMath::ivec2 gridSize,
    IRMath::ivec2 origin,
    int cellPx,
    IRMath::Color filledColor,
    IRMath::Color emptyColor,
    int distance,
    MaskGridPaintScratch &scratch
) {
    if (cellPx <= 0 || gridSize.x <= 0 || gridSize.y <= 0) return;

    const int gridW = gridSize.x * cellPx;
    const int gridH = gridSize.y * cellPx;
    if (origin.x < 0 || origin.y < 0 ||
        origin.x + gridW > canvas.size_.x ||
        origin.y + gridH > canvas.size_.y)
        return;

    const std::size_t cellsExpected =
        static_cast<std::size_t>(gridSize.x) * static_cast<std::size_t>(gridSize.y);
    IR_ASSERT(mask.size() >= cellsExpected,
              "drawMaskGridOntoCanvas: mask undersized for gridSize ({} < {})",
              mask.size(), cellsExpected);
    if (mask.size() < cellsExpected) return;

    const std::size_t pixels = static_cast<std::size_t>(gridW) * static_cast<std::size_t>(gridH);
    scratch.colors_.resize(pixels);
    scratch.distances_.resize(pixels);

    for (int v = 0; v < gridSize.y; ++v) {
        // Mask row 0 sits at the canvas-bottom of the grid region.
        const int displayRow = (gridSize.y - 1 - v) * cellPx;
        for (int h = 0; h < gridSize.x; ++h) {
            const IRMath::Color c =
                mask[static_cast<std::size_t>(h + v * gridSize.x)] ? filledColor : emptyColor;
            for (int py = 0; py < cellPx; ++py) {
                const int rowBase = (displayRow + py) * gridW + h * cellPx;
                for (int px = 0; px < cellPx; ++px) {
                    const std::size_t idx = static_cast<std::size_t>(rowBase + px);
                    scratch.colors_[idx] = c;
                    scratch.distances_[idx] = distance;
                }
            }
        }
    }

    canvas.textureTriangleColors_.second->subImage2D(
        origin.x, origin.y, gridW, gridH,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        scratch.colors_.data()
    );
    canvas.textureTriangleDistances_.second->subImage2D(
        origin.x, origin.y, gridW, gridH,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        scratch.distances_.data()
    );
}

// Map a GUI-trixel mouse position to the (h, v) cell of a mask grid
// rendered with drawMaskGridOntoCanvas. The returned vertical index is
// flipped to match the mask's storage convention (row 0 = canvas-bottom),
// so the result can plug directly into mask[h + v * gridSize.x].
// Returns std::nullopt when mouseGui lies outside the grid rect.
inline std::optional<IRMath::ivec2> hitTestGridCell(
    IRMath::ivec2 mouseGui,
    IRMath::ivec2 origin,
    int cellPx,
    IRMath::ivec2 gridSize
) {
    if (cellPx <= 0 || gridSize.x <= 0 || gridSize.y <= 0) return std::nullopt;
    const int gridW = gridSize.x * cellPx;
    const int gridH = gridSize.y * cellPx;
    if (mouseGui.x < origin.x || mouseGui.x >= origin.x + gridW ||
        mouseGui.y < origin.y || mouseGui.y >= origin.y + gridH)
        return std::nullopt;
    const int h = (mouseGui.x - origin.x) / cellPx;
    const int v = gridSize.y - 1 - (mouseGui.y - origin.y) / cellPx;
    return IRMath::ivec2(h, v);
}

} // namespace IRRender

#endif /* MASK_GRID_PAINTER_H */
