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
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/trixel_rect.hpp>

namespace IRSystem {

// Renders the dropdown collapsed header (one row, like a button), plus
// the expanded item panel below it when isOpen_. The header always
// occupies widget.size_. The expanded panels are painted in endTick, after
// every header's text has been dispatched: a panel's fill must land over the
// headers of the dropdowns below it, and glyphs are drawn by a dispatch that
// runs after the per-entity fills, so a panel painted in the tick would have
// those headers' text written over it. Hit-region growth to cover the open
// panel is handled by WIDGET_APPLY_DROPDOWN (hitbox size mutation).
template <> struct System<WIDGET_RENDER_DROPDOWN> {
    // An open dropdown, held from its tick to endTick of the same frame (no
    // structural change runs in between).
    struct OpenPanel {
        const IRComponents::C_Widget *widget_;
        const IRComponents::C_WidgetDropdown *dd_;
        const IRComponents::C_WidgetState *state_;
        IRMath::ivec2 pos_;
    };

    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;
    std::vector<OpenPanel> openPanels_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
        openPanels_.clear();
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

        IRPrefab::WidgetText::queue(
            textCmds_,
            IRPrefab::WidgetText::dropdownHeader(widget, dd, guiPos.pos_, theme_),
            canvas_->size_,
            IRPrefab::Widget::detail::stateText(theme_, widget)
        );

        // Dropdown chevron at the right edge: a "v" of lattice diamonds, two
        // arms stepping one column per row into a shared bottom diamond. The
        // origin is parity-aligned like text so every pair is a solid diamond.
        const int chevronW = IRPrefab::Widget::detail::kDropdownChevronWidth;
        const int chevronH = IRPrefab::Widget::detail::kDropdownChevronHeight;
        const IRMath::ivec2 chevron = IRRender::parityAlignedPosition(
            IRMath::ivec2(
                guiPos.pos_.x + widget.size_.x - chevronW -
                    IRPrefab::Widget::detail::dropdownHorizontalInset(theme_.padding_),
                guiPos.pos_.y + (widget.size_.y - chevronH) / 2 + 1
            ),
            canvas_->size_
        );
        constexpr IRMath::ivec2 kChevronDiamonds[] = {{0, 0}, {4, 0}, {1, 1}, {3, 1}, {2, 2}};
        for (const IRMath::ivec2 &diamond : kChevronDiamonds) {
            IRRender::fillRect(
                *canvas_,
                chevron + diamond,
                IRMath::ivec2(2, 1),
                IRPrefab::Widget::detail::stateText(theme_, widget),
                IRRender::kWidgetBorderDistance,
                scratch_
            );
        }

        if (dd.isOpen_ && n > 0) {
            openPanels_.push_back(OpenPanel{&widget, &dd, &state, guiPos.pos_});
        }
    }

    // Expanded item panel. Painted over anything below it, the other
    // dropdowns' headers included (their text was dispatched first).
    void paintPanel(const OpenPanel &panel) {
        const IRComponents::C_Widget &widget = *panel.widget_;
        const IRComponents::C_WidgetDropdown &dd = *panel.dd_;
        const IRComponents::C_WidgetState &state = *panel.state_;
        const int itemH = dd.rowHeight();
        const int n = static_cast<int>(dd.items_.size());
        const IRPrefab::GuiRect strip =
            IRPrefab::WidgetText::dropdownStripRect(widget, dd, panel.pos_);
        const IRMath::ivec2 panelPos = strip.pos_;
        const IRMath::ivec2 panelSize = strip.size_;
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
            const IRMath::ivec2 rowPos(
                panelPos.x,
                panel.pos_.y + dd.itemTopOffsetY(widget.size_.y, i)
            );
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
            IRPrefab::WidgetText::queue(
                textCmds_,
                IRPrefab::WidgetText::dropdownStripRow(widget, dd, panel.pos_, theme_, i),
                canvas_->size_,
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
        // The headers' text first, then the open panels over it, then theirs.
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
        if (openPanels_.empty()) {
            return;
        }
        for (const OpenPanel &panel : openPanels_) {
            paintPanel(panel);
        }
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        IRPrefab::Widget::ensureThemeSingleton();
        return registerSystem<
            WIDGET_RENDER_DROPDOWN,
            IRComponents::C_Widget,
            IRComponents::C_WidgetDropdown,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("WidgetRenderDropdown");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_DROPDOWN_H */
