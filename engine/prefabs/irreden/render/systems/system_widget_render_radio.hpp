#ifndef SYSTEM_WIDGET_RENDER_RADIO_H
#define SYSTEM_WIDGET_RENDER_RADIO_H

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

// Renders a single radio button: a box-outline indicator on the left,
// optional fill when selected, and a label to its right. Visually the
// radio button is drawn as a recessed square box rather than a circle
// because the trixel rasterizer paints axis-aligned rects natively;
// circle drawing would require either an SDF path or per-pixel
// stamping. The framework lives in widget-space so a square indicator
// reads identically once paired with the rest of the widget set.
template <> struct System<WIDGET_RENDER_RADIO> {
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
        const IRComponents::C_WidgetRadio &radio,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

        const int box = theme_.radioBoxSize_;
        const int boxY = guiPos.pos_.y + (widget.size_.y - box) / 2;

        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(guiPos.pos_.x, boxY),
            IRMath::ivec2(box, box),
            IRPrefab::Widget::detail::stateBackground(theme_, widget, state),
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
        IRRender::drawBorder(
            *canvas_,
            IRMath::ivec2(guiPos.pos_.x, boxY),
            IRMath::ivec2(box, box),
            IRPrefab::Widget::detail::stateBorder(theme_, widget, state),
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            scratch_
        );

        if (radio.selected_) {
            const int inset = theme_.borderThickness_ + 2;
            IRRender::fillRect(
                *canvas_,
                IRMath::ivec2(guiPos.pos_.x + inset, boxY + inset),
                IRMath::ivec2(box - 2 * inset, box - 2 * inset),
                theme_.radioFill_,
                IRRender::kWidgetLabelDistance,
                scratch_
            );
        }

        if (!radio.label_.empty()) {
            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                radio.label_,
                IRMath::ivec2(guiPos.pos_.x + box + theme_.padding_ * 2, guiPos.pos_.y),
                widget.size_.y,
                IRPrefab::Widget::detail::stateText(theme_, widget)
            );
        }
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_RADIO,
            IRComponents::C_Widget,
            IRComponents::C_WidgetRadio,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderRadio");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_RADIO_H */
