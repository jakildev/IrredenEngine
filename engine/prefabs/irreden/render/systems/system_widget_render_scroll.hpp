#ifndef SYSTEM_WIDGET_RENDER_SCROLL_H
#define SYSTEM_WIDGET_RENDER_SCROLL_H

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

// Renders a scroll bar widget: a thin track along one edge of the
// widget rect with a proportional thumb. Vertical axis paints the
// track on the right (full-height); horizontal axis paints it across
// the bottom (full-width). The widget body itself is otherwise
// transparent — creations are expected to place their own visible
// content within the widget's bounds and read scrollPos_ to offset
// that content.
template <> struct System<WIDGET_RENDER_SCROLL> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetScroll &scroll,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;
        if (widget.size_.x <= 0 || widget.size_.y <= 0)
            return;

        const bool vertical = (scroll.axis_ == IRComponents::C_WidgetScroll::Axis::VERTICAL);
        const int bar = IRMath::max(1, theme_.scrollBarThickness_);

        IRMath::ivec2 trackPos = guiPos.pos_;
        IRMath::ivec2 trackSize = widget.size_;
        if (vertical) {
            trackPos.x = guiPos.pos_.x + widget.size_.x - bar;
            trackSize.x = bar;
        } else {
            trackPos.y = guiPos.pos_.y + widget.size_.y - bar;
            trackSize.y = bar;
        }

        IRRender::fillRect(
            *canvas_,
            trackPos,
            trackSize,
            theme_.scrollTrack_,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );

        const int viewExtent = vertical ? widget.size_.y : widget.size_.x;
        const int contentExtent = IRMath::max(viewExtent, scroll.contentSize_);
        const int maxScroll = IRMath::max(0, contentExtent - viewExtent);
        const int clampedScroll = IRMath::clamp(scroll.scrollPos_, 0, maxScroll);

        // Thumb size proportional to viewExtent / contentExtent, clamped to a
        // minimum so it remains grabbable at extreme content sizes.
        const int thumbExtentRaw =
            (contentExtent > 0) ? (viewExtent * viewExtent) / contentExtent : viewExtent;
        const int thumbExtent =
            IRMath::clamp(thumbExtentRaw, theme_.scrollThumbMinExtent_, viewExtent);

        // Thumb offset proportional to scrollPos_ / maxScroll.
        const int thumbOffset =
            (maxScroll > 0) ? (clampedScroll * (viewExtent - thumbExtent)) / maxScroll : 0;

        IRMath::ivec2 thumbPos = trackPos;
        IRMath::ivec2 thumbSize = trackSize;
        if (vertical) {
            thumbPos.y = guiPos.pos_.y + thumbOffset;
            thumbSize.y = thumbExtent;
        } else {
            thumbPos.x = guiPos.pos_.x + thumbOffset;
            thumbSize.x = thumbExtent;
        }

        const IRMath::Color thumbColor = widget.disabled_ ? theme_.backgroundDisabled_
                                         : state.pressed_ ? theme_.scrollThumbHover_
                                         : state.hovered_ ? theme_.scrollThumbHover_
                                                          : theme_.scrollThumb_;
        IRRender::fillRect(
            *canvas_,
            thumbPos,
            thumbSize,
            thumbColor,
            IRRender::kWidgetBorderDistance,
            scratch_
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_SCROLL,
            IRComponents::C_Widget,
            IRComponents::C_WidgetScroll,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderScroll");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_SCROLL_H */
