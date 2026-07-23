#ifndef SYSTEM_WIDGET_RENDER_TEXT_INPUT_H
#define SYSTEM_WIDGET_RENDER_TEXT_INPUT_H

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
#include <irreden/render/trixel_text.hpp>

namespace IRSystem {

// Renders a single-line text input. Background + border use the
// standard stateful palette (border switches to borderFocused_ when
// state.focused_ is set). The text is drawn left-aligned, vertically
// centered. The cursor is a 2-px vertical rect blink-toggled by a
// frame counter on the system itself, painted only when the widget
// holds keyboard focus.
template <> struct System<WIDGET_RENDER_TEXT_INPUT> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;
    int frameCounter_ = 0;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        ++frameCounter_;
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetTextInput &textInput,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

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

        const int textX = guiPos.pos_.x + theme_.padding_ * 2;
        if (!textInput.text_.empty()) {
            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                textInput.text_,
                IRMath::ivec2(textX, guiPos.pos_.y),
                widget.size_.y,
                IRPrefab::Widget::detail::stateText(theme_, widget)
            );
        }

        if (!state.focused_ || widget.disabled_)
            return;

        // Blink: cursor visible during the first half of each blink
        // period, hidden during the second. textCursorBlinkPeriodFrames_
        // is the FULL period.
        const int period = IRMath::max(2, theme_.textCursorBlinkPeriodFrames_);
        const bool cursorVisible = (frameCounter_ % period) < (period / 2);
        if (!cursorVisible)
            return;

        const int cursorChars =
            IRMath::clamp(textInput.cursorPos_, 0, static_cast<int>(textInput.text_.size()));
        const int cursorX = IRMath::min(
            textX + cursorChars * IRRender::kGlyphStepX,
            guiPos.pos_.x + widget.size_.x - theme_.textInputCursorWidth_
        );
        const int cursorH = IRRender::kGlyphHeight;
        const int cursorY = guiPos.pos_.y + (widget.size_.y - cursorH) / 2;
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(cursorX, cursorY),
            IRMath::ivec2(theme_.textInputCursorWidth_, cursorH),
            theme_.textInputCursor_,
            IRRender::kWidgetBorderDistance,
            scratch_
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_TEXT_INPUT,
            IRComponents::C_Widget,
            IRComponents::C_WidgetTextInput,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderTextInput");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_TEXT_INPUT_H */
