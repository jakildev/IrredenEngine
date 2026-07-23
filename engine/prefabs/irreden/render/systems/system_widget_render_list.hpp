#ifndef SYSTEM_WIDGET_RENDER_LIST_H
#define SYSTEM_WIDGET_RENDER_LIST_H

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

// Renders a list widget: bordered background + one row per visible item
// from scrollOffset_ downward. Hover-row highlight comes from comparing
// the GUI-trixel mouse Y against the row's y span; the selected row gets
// the persistent listRowSelected_ tint. Rows clip to the widget's height
// by simply skipping rows that exceed it.
template <> struct System<WIDGET_RENDER_LIST> {
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
        const IRComponents::C_WidgetList &list,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;
        if (widget.size_.x <= 0 || widget.size_.y <= 0)
            return;

        const int itemH = IRMath::max(1, list.itemHeight_);
        const int rows = widget.size_.y / itemH;
        const int n = static_cast<int>(list.items_.size());

        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBackground(
                theme_,
                widget,
                IRComponents::C_WidgetState{}
            ),
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );

        for (int row = 0; row < rows; ++row) {
            const int idx = list.scrollOffset_ + row;
            if (idx < 0 || idx >= n)
                break;
            const IRMath::ivec2 rowPos(guiPos.pos_.x, guiPos.pos_.y + row * itemH);
            const IRMath::ivec2 rowSize(widget.size_.x, itemH);

            const bool isSelected = (idx == list.selectedIndex_);
            const bool isHoverRow =
                state.hovered_ && (state.dragValue_ >= static_cast<float>(row) &&
                                   state.dragValue_ < static_cast<float>(row + 1));

            if (isSelected) {
                IRRender::fillRect(
                    *canvas_,
                    rowPos,
                    rowSize,
                    theme_.listRowSelected_,
                    IRRender::kWidgetBorderDistance,
                    scratch_
                );
            } else if (isHoverRow) {
                IRRender::fillRect(
                    *canvas_,
                    rowPos,
                    rowSize,
                    theme_.listRowHover_,
                    IRRender::kWidgetBorderDistance,
                    scratch_
                );
            }

            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                list.items_[static_cast<std::size_t>(idx)],
                IRMath::ivec2(rowPos.x + theme_.padding_ * 2, rowPos.y),
                itemH,
                IRPrefab::Widget::detail::stateText(theme_, widget)
            );
        }

        IRRender::drawBorder(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBorder(theme_, widget, state),
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            scratch_
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_LIST,
            IRComponents::C_Widget,
            IRComponents::C_WidgetList,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderList");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_LIST_H */
