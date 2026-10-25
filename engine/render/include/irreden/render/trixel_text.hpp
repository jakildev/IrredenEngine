#ifndef TRIXEL_TEXT_H
#define TRIXEL_TEXT_H

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <string>
#include <string_view>

using namespace IRMath;
using namespace IRComponents;

namespace IRRender {

// Distance value used for GUI text (very near, renders on top of everything)
constexpr int kGuiTextDistance = IRConstants::kTrixelDistanceMinDistance + 1;

// Computes the parity-correct starting position for text on a canvas.
// Adjusts the requested position so (px + py + originModifier) % 2 == 0,
// matching the glyph design convention (position (0,0) = parity 0). The GUI
// canvas displays each texel as a lattice triangle whose orientation follows
// this parity, so an unaligned origin would mirror every glyph diagonal.
inline ivec2 parityAlignedPosition(ivec2 position, ivec2 canvasSize) {
    int originModifier = ((canvasSize.x / 2 - 1) + (canvasSize.y / 2 - 1)) & 1;
    if ((position.x + position.y + originModifier) & 1) {
        position.x += 1;
    }
    return position;
}

// Draws one glyph's bitmap `rows` (glyphRowPixel at `width`) onto a canvas at
// the given trixel position.
inline void renderGlyphRows(
    C_TriangleCanvasTextures &canvas,
    std::span<const uint8_t> rows,
    int width,
    ivec2 position,
    Color color
) {
    for (int row = 0; row < static_cast<int>(rows.size()); ++row) {
        for (int col = 0; col < width; ++col) {
            if (!glyphRowPixel(rows[row], width, col)) {
                continue;
            }
            ivec2 trixelPos = position + ivec2(col, row);
            if (trixelPos.x >= 0 && trixelPos.y >= 0 && trixelPos.x < canvas.size_.x &&
                trixelPos.y < canvas.size_.y) {
                canvas.setTrixel(trixelPos, color, kGuiTextDistance);
            }
        }
    }
}

// Renders a single large-set glyph onto a canvas at the given trixel position.
inline void
renderGlyph(C_TriangleCanvasTextures &canvas, const Glyph &glyph, ivec2 position, Color color) {
    renderGlyphRows(canvas, glyph, kGlyphWidth, position, color);
}

// Measures the width (in trixels) of the next word starting at position i.
inline int nextWordWidth(std::string_view text, size_t i, TextSize size = TextSize::LARGE) {
    const int stepX = glyphMetrics(size).stepX_;
    int width = 0;
    while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
        width += stepX;
        i++;
    }
    return width;
}

// Renders a text string onto a canvas. Supports newlines and word wrapping.
// wrapWidth: max trixels per line (0 = no wrapping, wraps to canvas edge if negative).
inline void renderText(
    C_TriangleCanvasTextures &canvas,
    const std::string &text,
    ivec2 position,
    Color color,
    int wrapWidth = 0,
    TextSize size = TextSize::LARGE
) {
    const GlyphMetrics metrics = glyphMetrics(size);
    ivec2 aligned = parityAlignedPosition(position, canvas.size_);
    ivec2 cursor = aligned;
    const int effectiveWrap = (wrapWidth > 0)
        ? aligned.x + wrapWidth
        : (wrapWidth < 0 ? canvas.size_.x : 0);

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            cursor.x = aligned.x;
            cursor.y += metrics.stepY_;
            continue;
        }

        if (effectiveWrap > 0 && c == ' ') {
            int upcoming = nextWordWidth(text, i + 1, size);
            if (cursor.x + metrics.stepX_ + upcoming > effectiveWrap) {
                cursor.x = aligned.x;
                cursor.y += metrics.stepY_;
                continue;
            }
        }

        if (effectiveWrap > 0 && cursor.x + metrics.width_ > effectiveWrap && c != ' ') {
            cursor.x = aligned.x;
            cursor.y += metrics.stepY_;
        }

        renderGlyphRows(canvas, glyphRows(size, c), metrics.width_, cursor, color);
        cursor.x += metrics.stepX_;
    }
}

// Height in trixels of a block of `lineCount` lines at `fontSize`: the
// inter-line steps plus one glyph row, so the last row carries no line gap.
inline int textBlockHeight(int lineCount, int fontSize = 1, TextSize size = TextSize::LARGE) {
    const GlyphMetrics metrics = glyphMetrics(size);
    return ((lineCount - 1) * metrics.stepY_ + metrics.height_) * fontSize;
}

// Measures the size in trixels that a text string would occupy.
// wrapWidth: max trixels per line (0 = no wrapping).
inline ivec2
measureText(const std::string &text, int wrapWidth = 0, TextSize size = TextSize::LARGE) {
    const GlyphMetrics metrics = glyphMetrics(size);
    int maxWidth = 0;
    int currentWidth = 0;
    int lines = 1;

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            maxWidth = IRMath::max(maxWidth, currentWidth);
            currentWidth = 0;
            lines++;
        } else {
            if (wrapWidth > 0 && c == ' ') {
                int upcoming = nextWordWidth(text, i + 1, size);
                if (currentWidth + metrics.stepX_ + upcoming > wrapWidth) {
                    maxWidth = IRMath::max(maxWidth, currentWidth);
                    currentWidth = 0;
                    lines++;
                    continue;
                }
            }
            if (wrapWidth > 0 && currentWidth + metrics.width_ > wrapWidth && c != ' ') {
                maxWidth = IRMath::max(maxWidth, currentWidth);
                currentWidth = 0;
                lines++;
            }
            currentWidth += metrics.stepX_;
        }
    }
    maxWidth = IRMath::max(maxWidth, currentWidth);

    if (maxWidth > 0) {
        maxWidth -= metrics.spacingX_;
    }

    return ivec2(maxWidth, lines * metrics.stepY_ - metrics.spacingY_);
}

} // namespace IRRender

#endif /* TRIXEL_TEXT_H */
