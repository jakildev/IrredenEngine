#ifndef SYSTEM_WIDGET_RENDER_COLOR_SWATCH_H
#define SYSTEM_WIDGET_RENDER_COLOR_SWATCH_H

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

// Renders color-swatch widgets onto the GUI canvas. The body fill is
// taken straight from `C_WidgetColorSwatch::color_` (each swatch is its
// own color). The border thickens and switches to the theme's
// active-border color when `selected_` is set, so the swatch the user
// most recently picked stays visually distinct from its siblings.
// Hover and pressed states still tint the border via the theme so
// click feedback survives selection.
template <> struct System<WIDGET_RENDER_COLOR_SWATCH> {
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
        const IRComponents::C_WidgetColorSwatch &swatch,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            swatch.color_,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );

        // Selected swatches get a thicker, theme-focused-color border so
        // the active palette pick reads at a glance. The hover/pressed
        // state still wins over the base unselected border (state-aware
        // theme call), which keeps click feedback consistent with the
        // other interactive widgets.
        const IRMath::Color borderColor =
            swatch.selected_ ? theme_.borderFocused_
                             : IRPrefab::Widget::detail::stateBorder(theme_, widget, state);
        const int thickness =
            swatch.selected_ ? theme_.borderThickness_ + 1 : theme_.borderThickness_;
        IRRender::drawBorder(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            borderColor,
            IRRender::kWidgetBorderDistance,
            thickness,
            scratch_
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_COLOR_SWATCH,
            IRComponents::C_Widget,
            IRComponents::C_WidgetColorSwatch,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderColorSwatch");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_COLOR_SWATCH_H */
