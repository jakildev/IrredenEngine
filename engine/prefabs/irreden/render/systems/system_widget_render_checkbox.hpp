#ifndef SYSTEM_WIDGET_RENDER_CHECKBOX_H
#define SYSTEM_WIDGET_RENDER_CHECKBOX_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_draw.hpp>
#include <irreden/render/trixel_rect.hpp>

namespace IRSystem {

template <> struct System<WIDGET_RENDER_CHECKBOX> {
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
        const IRComponents::C_WidgetCheckbox &checkbox,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

        const int boxSize = theme_.checkboxBoxSize_;
        const int boxY = guiPos.pos_.y + (widget.size_.y - boxSize) / 2;

        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(guiPos.pos_.x, boxY),
            IRMath::ivec2(boxSize, boxSize),
            IRPrefab::Widget::detail::stateBackground(theme_, widget, state),
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
        IRRender::drawBorder(
            *canvas_,
            IRMath::ivec2(guiPos.pos_.x, boxY),
            IRMath::ivec2(boxSize, boxSize),
            IRPrefab::Widget::detail::stateBorder(theme_, widget, state),
            IRRender::kWidgetBorderDistance,
            theme_.borderThickness_,
            scratch_
        );

        if (checkbox.checked_) {
            const int inset = theme_.borderThickness_ + 2;
            IRRender::fillRect(
                *canvas_,
                IRMath::ivec2(guiPos.pos_.x + inset, boxY + inset),
                IRMath::ivec2(boxSize - 2 * inset, boxSize - 2 * inset),
                theme_.checkboxFill_,
                IRRender::kWidgetLabelDistance,
                scratch_
            );
        }

        if (!checkbox.label_.empty()) {
            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                checkbox.label_,
                IRMath::ivec2(guiPos.pos_.x + boxSize + theme_.padding_ * 2, guiPos.pos_.y),
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
            WIDGET_RENDER_CHECKBOX,
            IRComponents::C_Widget,
            IRComponents::C_WidgetCheckbox,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderCheckbox");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_CHECKBOX_H */
