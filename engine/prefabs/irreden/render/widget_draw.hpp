#ifndef WIDGET_DRAW_H
#define WIDGET_DRAW_H

#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <irreden/render/widget_theme.hpp>
#include <irreden/render/components/component_widget.hpp>

#include <string>

namespace IRPrefab::Widget::detail {

// Picks the background color for a widget given its interactive state.
// Centralized so every widget render system follows the same palette.
inline IRMath::Color stateBackground(
    const WidgetTheme &theme,
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetState &state
) {
    if (widget.disabled_) return theme.backgroundDisabled_;
    if (state.pressed_) return theme.backgroundPressed_;
    if (state.hovered_) return theme.backgroundHover_;
    return theme.backgroundIdle_;
}

inline IRMath::Color stateBorder(
    const WidgetTheme &theme,
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetState &state
) {
    if (widget.disabled_) return theme.borderDisabled_;
    if (state.pressed_) return theme.borderPressed_;
    if (state.hovered_) return theme.borderHover_;
    if (state.focused_) return theme.borderFocused_;
    return theme.borderIdle_;
}

inline IRMath::Color stateText(const WidgetTheme &theme, const IRComponents::C_Widget &widget) {
    return widget.disabled_ ? theme.textDisabled_ : theme.textIdle_;
}

// Approximate text bounds for the trixel font. The font is fixed-width
// 7x11 with 1-trixel spacing — measurement is `glyphCount * stepX -
// spacing` horizontally, one line vertically (no \n support here; the
// renderer in `renderText` handles wrapping itself).
inline IRMath::ivec2 measureSingleLine(const std::string &text) {
    if (text.empty()) return IRMath::ivec2(0, IRRender::kGlyphHeight);
    int w = static_cast<int>(text.size()) * IRRender::kGlyphStepX - IRRender::kGlyphSpacingX;
    return IRMath::ivec2(w, IRRender::kGlyphHeight);
}

// Render `text` centered horizontally inside `[pos, pos + size)` and
// vertically aligned to the visual middle of the widget.
inline void drawCenteredText(
    IRComponents::C_TriangleCanvasTextures &canvas,
    const std::string &text,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color
) {
    if (text.empty()) return;
    const IRMath::ivec2 textSize = measureSingleLine(text);
    IRMath::ivec2 textPos = pos + (size - textSize) / 2;
    // Round down to even so parityAlignedPosition's adjustment never
    // shifts visible glyphs left of the widget's interior padding.
    textPos.x = (textPos.x / 2) * 2;
    textPos.y = (textPos.y / 2) * 2;
    IRRender::renderText(canvas, text, textPos, color);
}

// Render `text` left-aligned, vertically centered.
inline void drawLeftText(
    IRComponents::C_TriangleCanvasTextures &canvas,
    const std::string &text,
    IRMath::ivec2 pos,
    int height,
    IRMath::Color color
) {
    if (text.empty()) return;
    const IRMath::ivec2 textSize = measureSingleLine(text);
    IRMath::ivec2 textPos = IRMath::ivec2(pos.x, pos.y + (height - textSize.y) / 2);
    textPos.x = (textPos.x / 2) * 2;
    textPos.y = (textPos.y / 2) * 2;
    IRRender::renderText(canvas, text, textPos, color);
}

} // namespace IRPrefab::Widget::detail

#endif /* WIDGET_DRAW_H */
