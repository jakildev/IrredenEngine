#ifndef TRIXEL_TEXT_H
#define TRIXEL_TEXT_H

#include <irreden/render/trixel_font.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <string>

using namespace IRMath;
using namespace IRComponents;

namespace IRRender {

// Distance value used for GUI text (very near, renders on top of everything)
constexpr int kGuiTextDistance = IRConstants::kTrixelDistanceMinDistance + 1;

// Computes the parity-correct starting position for text on a canvas.
// Adjusts the requested position so (px + py + originModifier) % 2 == 0,
// matching the glyph design convention (position (0,0) = parity 0).
inline ivec2 parityAlignedPosition(ivec2 position, ivec2 canvasSize) {
    int originModifier = ((canvasSize.x / 2 - 1) + (canvasSize.y / 2 - 1)) & 1;
    if ((position.x + position.y + originModifier) & 1) {
        position.x += 1;
    }
    return position;
}

// Renders a single glyph onto a canvas at the given trixel position.
inline void renderGlyph(
    C_TriangleCanvasTextures &canvas,
    const Glyph &glyph,
    ivec2 position,
    Color color
) {
    for (int row = 0; row < kGlyphHeight; ++row) {
        for (int col = 0; col < kGlyphWidth; ++col) {
            if (glyphPixel(glyph, col, row)) {
                ivec2 trixelPos = position + ivec2(col, row);
                if (trixelPos.x >= 0 && trixelPos.y >= 0 &&
                    trixelPos.x < canvas.size_.x && trixelPos.y < canvas.size_.y) {
                    canvas.setTrixel(trixelPos, color, kGuiTextDistance);
                }
            }
        }
    }
}

// Measures the width (in trixels) of the next word starting at position i.
inline int nextWordWidth(const std::string &text, size_t i) {
    int width = 0;
    while (i < text.size() && text[i] != ' ' && text[i] != '\n') {
        width += kGlyphStepX;
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
    int wrapWidth = 0
) {
    ivec2 aligned = parityAlignedPosition(position, canvas.size_);
    ivec2 cursor = aligned;
    const int effectiveWrap = (wrapWidth > 0)
        ? aligned.x + wrapWidth
        : (wrapWidth < 0 ? canvas.size_.x : 0);

    for (size_t i = 0; i < text.size(); ++i) {
        char c = text[i];
        if (c == '\n') {
            cursor.x = aligned.x;
            cursor.y += kGlyphStepY;
            continue;
        }

        if (effectiveWrap > 0 && c == ' ') {
            int upcoming = nextWordWidth(text, i + 1);
            if (cursor.x + kGlyphStepX + upcoming > effectiveWrap) {
                cursor.x = aligned.x;
                cursor.y += kGlyphStepY;
                continue;
            }
        }

        if (effectiveWrap > 0 && cursor.x + kGlyphWidth > effectiveWrap && c != ' ') {
            cursor.x = aligned.x;
            cursor.y += kGlyphStepY;
        }

        const Glyph *glyph = getGlyph(c);
        if (glyph) {
            renderGlyph(canvas, *glyph, cursor, color);
        }
        cursor.x += kGlyphStepX;
    }
}

// Measures the size in trixels that a text string would occupy.
// wrapWidth: max trixels per line (0 = no wrapping).
inline ivec2 measureText(const std::string &text, int wrapWidth = 0) {
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
                int upcoming = nextWordWidth(text, i + 1);
                if (currentWidth + kGlyphStepX + upcoming > wrapWidth) {
                    maxWidth = IRMath::max(maxWidth, currentWidth);
                    currentWidth = 0;
                    lines++;
                    continue;
                }
            }
            if (wrapWidth > 0 && currentWidth + kGlyphWidth > wrapWidth && c != ' ') {
                maxWidth = IRMath::max(maxWidth, currentWidth);
                currentWidth = 0;
                lines++;
            }
            currentWidth += kGlyphStepX;
        }
    }
    maxWidth = IRMath::max(maxWidth, currentWidth);

    if (maxWidth > 0) {
        maxWidth -= kGlyphSpacingX;
    }

    return ivec2(maxWidth, lines * kGlyphStepY - kGlyphSpacingY);
}

} // namespace IRRender

#endif /* TRIXEL_TEXT_H */
