#ifndef WIDGET_DRAW_H
#define WIDGET_DRAW_H

#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/trixel_text.hpp>
#include <irreden/render/trixel_font.hpp>
#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/ir_render_types.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>

#include <irreden/render/widget_theme.hpp>
#include <irreden/render/components/component_widget.hpp>

#include <string>
#include <vector>

namespace IRPrefab::Widget::detail {

// Widget text renders through the batched compute path (IRPrefab::GuiText)
// at this base font size. 1 == the native 7x11 glyph (no upscale), matching
// the legacy renderText path so the migration is visually identical. A
// higher-resolution GUI canvas can raise this for larger body text without
// touching call sites.
constexpr int kWidgetTextFontSize = 1;

// Picks the background color for a widget given its interactive state.
// Centralized so every widget render system follows the same palette.
inline IRMath::Color stateBackground(
    const WidgetTheme &theme,
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetState &state
) {
    if (widget.disabled_)
        return theme.backgroundDisabled_;
    if (state.pressed_)
        return theme.backgroundPressed_;
    if (state.hovered_)
        return theme.backgroundHover_;
    return theme.backgroundIdle_;
}

inline IRMath::Color stateBorder(
    const WidgetTheme &theme,
    const IRComponents::C_Widget &widget,
    const IRComponents::C_WidgetState &state
) {
    if (widget.disabled_)
        return theme.borderDisabled_;
    if (state.pressed_)
        return theme.borderPressed_;
    if (state.hovered_)
        return theme.borderHover_;
    if (state.focused_)
        return theme.borderFocused_;
    return theme.borderIdle_;
}

inline IRMath::Color stateText(const WidgetTheme &theme, const IRComponents::C_Widget &widget) {
    return widget.disabled_ ? theme.textDisabled_ : theme.textIdle_;
}

// Queue `text` centered horizontally and vertically inside `[pos, pos +
// size)` onto `cmds`. Appends glyph commands rather than rasterizing
// immediately; the owning render system dispatches `cmds` once in its
// endTick. `canvasSize` is the GUI canvas extent (for parity alignment).
inline void queueCenteredText(
    std::vector<IRRender::GlyphDrawCommand> &cmds,
    IRMath::ivec2 canvasSize,
    const std::string &text,
    IRMath::ivec2 pos,
    IRMath::ivec2 size,
    IRMath::Color color,
    int fontSize = kWidgetTextFontSize
) {
    IRPrefab::GuiText::queueGuiText(
        cmds,
        text,
        pos,
        canvasSize,
        color,
        fontSize,
        IRComponents::TextAlignH::CENTER,
        IRComponents::TextAlignV::CENTER,
        size.x,
        size.y
    );
}

// Queue `text` left-aligned, vertically centered within `height`, onto
// `cmds`. See queueCenteredText for the deferred-dispatch contract.
inline void queueLeftText(
    std::vector<IRRender::GlyphDrawCommand> &cmds,
    IRMath::ivec2 canvasSize,
    const std::string &text,
    IRMath::ivec2 pos,
    int height,
    IRMath::Color color,
    int fontSize = kWidgetTextFontSize
) {
    IRPrefab::GuiText::queueGuiText(
        cmds,
        text,
        pos,
        canvasSize,
        color,
        fontSize,
        IRComponents::TextAlignH::LEFT,
        IRComponents::TextAlignV::CENTER,
        0,
        height
    );
}

} // namespace IRPrefab::Widget::detail

#endif /* WIDGET_DRAW_H */
