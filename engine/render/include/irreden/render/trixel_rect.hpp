#ifndef TRIXEL_RECT_H
#define TRIXEL_RECT_H

#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/ir_math.hpp>

#include <vector>

namespace IRRender {

// Distance band reserved for widget visuals on the GUI canvas. Each widget
// layer writes to the canvas via subImage2D (unconditional overwrite), so
// these distances do NOT enforce a depth order at write time — they only
// govern the framebuffer composite. Widget systems are responsible for
// drawing layers in painter order (background first, border on top, label
// on top of that) within a single tick.
constexpr int kWidgetBackgroundDistance = IRConstants::kTrixelDistanceMinDistance + 4;
constexpr int kWidgetBorderDistance = IRConstants::kTrixelDistanceMinDistance + 3;
constexpr int kWidgetLabelDistance = IRConstants::kTrixelDistanceMinDistance + 2;

// Clip `[pos, pos + size)` to `[0, canvasSize)` and return the clipped
// rect via lo/hi (lo inclusive, hi exclusive). Returns false if the result
// is empty (off-canvas or zero-sized input).
inline bool clipRectToCanvas(
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::ivec2 canvasSize,
    IRMath::ivec2 &loOut,
    IRMath::ivec2 &hiOut
) {
    const int x0 = IRMath::max(0, pos.x);
    const int y0 = IRMath::max(0, pos.y);
    const int x1 = IRMath::min(canvasSize.x, pos.x + size.x);
    const int y1 = IRMath::min(canvasSize.y, pos.y + size.y);
    if (x1 <= x0 || y1 <= y0) return false;
    loOut = IRMath::ivec2(x0, y0);
    hiOut = IRMath::ivec2(x1, y1);
    return true;
}

// Per-system scratch storage for batched rect uploads. One vector for
// color, one for distance, reused across rects to avoid per-frame
// allocations. A widget render system constructs one of these in its
// params and threads it through every rect draw of the frame.
struct RectFillScratch {
    std::vector<IRMath::Color> colors_;
    std::vector<int> distances_;
};

// Fill a solid rectangle on a canvas via one subImage2D per channel
// (color + distance). `scratch` is reused across calls so allocations
// only grow to the largest rect ever drawn; subsequent rects of equal
// or smaller area reuse the existing capacity.
inline void fillRect(
    IRComponents::C_TriangleCanvasTextures &canvas,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color,
    int distance,
    RectFillScratch &scratch
) {
    IRMath::ivec2 lo, hi;
    if (!clipRectToCanvas(pos, size, canvas.size_, lo, hi)) return;

    const int w = hi.x - lo.x;
    const int h = hi.y - lo.y;
    const int pixels = w * h;

    scratch.colors_.assign(pixels, color);
    scratch.distances_.assign(pixels, distance);

    canvas.textureTriangleColors_.second->subImage2D(
        lo.x, lo.y, w, h,
        IRRender::PixelDataFormat::RGBA,
        IRRender::PixelDataType::UNSIGNED_BYTE,
        scratch.colors_.data()
    );
    canvas.textureTriangleDistances_.second->subImage2D(
        lo.x, lo.y, w, h,
        IRRender::PixelDataFormat::RED_INTEGER,
        IRRender::PixelDataType::INT32,
        scratch.distances_.data()
    );
}

// Stroke a 1-trixel-wide hollow border on a canvas. Four thin
// fillRect calls; the corners are written twice but the cost is
// negligible compared to the per-rect subImage2D overhead.
inline void drawBorder(
    IRComponents::C_TriangleCanvasTextures &canvas,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color,
    int distance,
    int thickness,
    RectFillScratch &scratch
) {
    if (size.x <= 0 || size.y <= 0 || thickness <= 0) return;
    const int t = IRMath::min(thickness, IRMath::min(size.x, size.y));

    fillRect(canvas, pos, IRMath::ivec2(size.x, t), color, distance, scratch);
    fillRect(
        canvas,
        IRMath::ivec2(pos.x, pos.y + size.y - t),
        IRMath::ivec2(size.x, t),
        color,
        distance,
        scratch
    );
    fillRect(
        canvas,
        IRMath::ivec2(pos.x, pos.y + t),
        IRMath::ivec2(t, size.y - 2 * t),
        color,
        distance,
        scratch
    );
    fillRect(
        canvas,
        IRMath::ivec2(pos.x + size.x - t, pos.y + t),
        IRMath::ivec2(t, size.y - 2 * t),
        color,
        distance,
        scratch
    );
}

} // namespace IRRender

#endif /* TRIXEL_RECT_H */
