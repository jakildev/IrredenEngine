#ifndef SYSTEM_WIDGET_RENDER_DROPDOWN_H
#define SYSTEM_WIDGET_RENDER_DROPDOWN_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_draw.hpp>
#include <irreden/render/trixel_rect.hpp>

namespace IRSystem {

// Renders the dropdown collapsed header (one row, like a button), plus
// the expanded item panel below it when isOpen_. The header always
// occupies widget.size_; the expanded panel is drawn below the header
// in painter order so it overpaints any widget that happens to share
// the same z-stripe. Hit-region growth to cover the open panel is
// handled by WIDGET_APPLY_DROPDOWN (hitbox size mutation).
template <> struct System<WIDGET_RENDER_DROPDOWN> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetDropdown &dd,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;
        if (widget.size_.x <= 0 || widget.size_.y <= 0)
            return;

        const int itemH = IRMath::max(1, dd.itemHeight_);
        const int n = static_cast<int>(dd.items_.size());

        // Collapsed header.
        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBackground(theme_, widget, state),
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
        IRRender::drawBorder(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBorder(theme_, widget, state),
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            scratch_
        );

        // Selected label (or placeholder dash when empty).
        const std::string &headerText = (dd.selectedIndex_ >= 0 && dd.selectedIndex_ < n)
                                            ? dd.items_[static_cast<std::size_t>(dd.selectedIndex_)]
                                            : std::string("---");
        IRPrefab::Widget::detail::queueLeftText(
            textCmds_,
            canvas_->size_,
            headerText,
            IRMath::ivec2(guiPos.pos_.x + theme_.padding_ * 2, guiPos.pos_.y),
            widget.size_.y,
            IRPrefab::Widget::detail::stateText(theme_, widget)
        );

        // Dropdown chevron — solid triangle approximated as a small stack of rects
        // at the right edge. Two rects suffice for visual recognizability at the
        // GUI canvas resolution this framework targets.
        const int chevronW = 8;
        const int chevronH = 4;
        const int chevX = guiPos.pos_.x + widget.size_.x - chevronW - theme_.padding_ * 2;
        const int chevY = guiPos.pos_.y + (widget.size_.y - chevronH) / 2;
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(chevX, chevY),
            IRMath::ivec2(chevronW, 2),
            IRPrefab::Widget::detail::stateText(theme_, widget),
            IRRender::kWidgetBorderDistance,
            scratch_
        );
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(chevX + 2, chevY + 2),
            IRMath::ivec2(chevronW - 4, 2),
            IRPrefab::Widget::detail::stateText(theme_, widget),
            IRRender::kWidgetBorderDistance,
            scratch_
        );

        if (!dd.isOpen_ || n == 0)
            return;

        // Expanded item panel. Painted in painter order over anything below it.
        const IRMath::ivec2 panelPos(guiPos.pos_.x, guiPos.pos_.y + widget.size_.y);
        const IRMath::ivec2 panelSize(widget.size_.x, itemH * n);
        IRRender::fillRect(
            *canvas_,
            panelPos,
            panelSize,
            theme_.backgroundIdle_,
            IRRender::kWidgetBorderDistance,
            scratch_
        );

        // Hover-row derived from the cached state.dragValue_ (set by
        // WIDGET_APPLY_DROPDOWN to the cursor-row index, or -1 if outside).
        const int hoverRow = static_cast<int>(state.dragValue_);

        for (int i = 0; i < n; ++i) {
            const IRMath::ivec2 rowPos(panelPos.x, panelPos.y + i * itemH);
            const IRMath::ivec2 rowSize(panelSize.x, itemH);
            if (i == dd.selectedIndex_) {
                IRRender::fillRect(
                    *canvas_,
                    rowPos,
                    rowSize,
                    theme_.listRowSelected_,
                    IRRender::kWidgetLabelDistance,
                    scratch_
                );
            } else if (i == hoverRow) {
                IRRender::fillRect(
                    *canvas_,
                    rowPos,
                    rowSize,
                    theme_.listRowHover_,
                    IRRender::kWidgetLabelDistance,
                    scratch_
                );
            }
            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                dd.items_[static_cast<std::size_t>(i)],
                IRMath::ivec2(rowPos.x + theme_.padding_ * 2, rowPos.y),
                itemH,
                IRPrefab::Widget::detail::stateText(theme_, widget)
            );
        }

        IRRender::drawBorder(
            *canvas_,
            panelPos,
            panelSize,
            IRPrefab::Widget::detail::stateBorder(theme_, widget, state),
            IRRender::kWidgetLabelDistance,
            theme_.borderThickness_,
            scratch_
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_DROPDOWN,
            IRComponents::C_Widget,
            IRComponents::C_WidgetDropdown,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderDropdown");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_DROPDOWN_H */
