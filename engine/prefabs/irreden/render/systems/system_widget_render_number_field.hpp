#ifndef SYSTEM_WIDGET_RENDER_NUMBER_FIELD_H
#define SYSTEM_WIDGET_RENDER_NUMBER_FIELD_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/widget_draw.hpp>
#include <irreden/render/widget_text_layout.hpp>
#include <irreden/render/widget_theme.hpp>

namespace IRSystem {

template <> struct System<WIDGET_RENDER_NUMBER_FIELD> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;
    std::string formattedText_;

    void beginTick() {
        const IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
        formattedText_.reserve(48);
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetNumberField &field,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_) {
            return;
        }
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

        if (!field.editing_) {
            field.formatValue(formattedText_);
        }
        IRPrefab::WidgetText::queue(
            textCmds_,
            IRPrefab::WidgetText::numberFieldText(
                widget,
                field,
                guiPos.pos_,
                theme_,
                IRPrefab::WidgetText::numberFieldShownText(field, formattedText_)
            ),
            canvas_->size_,
            IRPrefab::Widget::detail::stateText(theme_, widget)
        );

        if (!field.editing_ || widget.disabled_) {
            return;
        }
        const int cursorWidth = IRPrefab::WidgetText::numberFieldCursorWidth(field, theme_);
        const int padding = theme_.padding_ * 2;
        const IRRender::GlyphMetrics metrics = IRRender::glyphMetrics(field.textSize_);
        const int cursorX = guiPos.pos_.x + widget.size_.x - padding - cursorWidth;
        const int cursorY = guiPos.pos_.y + (widget.size_.y - metrics.height_) / 2;
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(cursorX, cursorY),
            IRMath::ivec2(cursorWidth, metrics.height_),
            theme_.textInputCursor_,
            IRRender::kWidgetBorderDistance,
            scratch_
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        IRPrefab::Widget::ensureThemeSingleton();
        return registerSystem<
            WIDGET_RENDER_NUMBER_FIELD,
            IRComponents::C_Widget,
            IRComponents::C_WidgetNumberField,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition,
            AlsoWrites<IRComponents::C_TriangleCanvasTextures>>("WidgetRenderNumberField");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_NUMBER_FIELD_H */
