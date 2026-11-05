#ifndef WIDGET_KEYCAP_H
#define WIDGET_KEYCAP_H

#include <irreden/render/gui_text_batch.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/gui_rect.hpp>

namespace IRPrefab::Widget {
inline IRMath::ivec2 keycapSize(std::string_view shortcut) {
    return {
        GuiText::textRunWidth(shortcut, 1, IRRender::TextSize::SMALL) + 4,
        GuiText::glyphHeight(1, IRRender::TextSize::SMALL) + 4
    };
}
inline void drawKeycap(
    IRComponents::C_TriangleCanvasTextures &canvas,
    IRRender::RectFillScratch &scratch,
    std::vector<IRRender::GlyphDrawCommand> &commands,
    IRMath::ivec2 pos,
    std::string_view shortcut,
    IRMath::Color background,
    IRMath::Color foreground
) {
    IRRender::fillRect(
        canvas,
        pos,
        keycapSize(shortcut),
        background,
        IRRender::kWidgetBorderDistance,
        scratch
    );
    IRRender::drawBorder(
        canvas,
        pos,
        keycapSize(shortcut),
        foreground,
        IRRender::kWidgetBorderDistance,
        1,
        scratch
    );
    GuiText::queueGuiText(
        commands,
        shortcut,
        pos + IRMath::ivec2(2, 2),
        canvas.size_,
        foreground,
        1,
        IRComponents::TextAlignH::LEFT,
        IRComponents::TextAlignV::TOP,
        0,
        0,
        0,
        IRRender::TextSize::SMALL
    );
}
} // namespace IRPrefab::Widget
#endif
