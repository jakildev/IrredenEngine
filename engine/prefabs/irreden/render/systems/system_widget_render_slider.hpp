#ifndef SYSTEM_WIDGET_RENDER_SLIDER_H
#define SYSTEM_WIDGET_RENDER_SLIDER_H

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

#include <cstdio>
#include <string>

namespace IRSystem {

template <> struct System<WIDGET_RENDER_SLIDER> {
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
        const IRComponents::C_WidgetSlider &slider,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

        const int W = widget.size_.x;
        const int H = widget.size_.y;
        if (W <= 0 || H <= 0)
            return;

        // Top half is the optional label, bottom half is track + thumb.
        const int labelH = slider.label_.empty() ? 0 : IRRender::kGlyphStepY;
        const int trackY = guiPos.pos_.y + labelH + (H - labelH - theme_.sliderTrackThickness_) / 2;
        const int trackX = guiPos.pos_.x;

        if (!slider.label_.empty()) {
            IRPrefab::Widget::detail::queueLeftText(
                textCmds_,
                canvas_->size_,
                slider.label_,
                guiPos.pos_,
                labelH,
                IRPrefab::Widget::detail::stateText(theme_, widget)
            );
        }

        // Track
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(trackX, trackY),
            IRMath::ivec2(W, theme_.sliderTrackThickness_),
            theme_.sliderTrack_,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );

        // Thumb
        const float range = (slider.maxValue_ - slider.minValue_) != 0.0f
                                ? (slider.maxValue_ - slider.minValue_)
                                : 1.0f;
        const float t =
            IRMath::clamp((slider.currentValue_ - slider.minValue_) / range, 0.0f, 1.0f);
        const int thumbW = theme_.sliderThumbWidth_;
        const int thumbH = H - labelH;
        const int thumbX = guiPos.pos_.x + static_cast<int>(t * static_cast<float>(W - thumbW));
        const int thumbY = guiPos.pos_.y + labelH;
        const IRMath::Color thumbColor = widget.disabled_ ? theme_.backgroundDisabled_
                                         : state.hovered_ ? theme_.sliderThumbHover_
                                                          : theme_.sliderThumbIdle_;
        IRRender::fillRect(
            *canvas_,
            IRMath::ivec2(thumbX, thumbY),
            IRMath::ivec2(thumbW, thumbH),
            thumbColor,
            IRRender::kWidgetBorderDistance,
            scratch_
        );

        // Value text right-aligned within the label row [pos.x, pos.x + W).
        if (!slider.label_.empty()) {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "%.2f", slider.currentValue_);
            IRPrefab::GuiText::queueGuiText(
                textCmds_,
                buf,
                guiPos.pos_,
                canvas_->size_,
                IRPrefab::Widget::detail::stateText(theme_, widget),
                IRPrefab::Widget::detail::kWidgetTextFontSize,
                IRComponents::TextAlignH::RIGHT,
                IRComponents::TextAlignV::CENTER,
                W,
                labelH
            );
        }
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_SLIDER,
            IRComponents::C_Widget,
            IRComponents::C_WidgetSlider,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderSlider");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_SLIDER_H */
