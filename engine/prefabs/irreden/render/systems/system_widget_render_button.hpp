#ifndef SYSTEM_WIDGET_RENDER_BUTTON_H
#define SYSTEM_WIDGET_RENDER_BUTTON_H

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

template <> struct System<WIDGET_RENDER_BUTTON> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetButton &button,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (widget.kind_ != IRComponents::WidgetKind::BUTTON) return;
        if (!canvas_) return;

        const auto &theme = IRPrefab::Widget::defaultTheme();
        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBackground(theme, widget, state),
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );
        IRRender::drawBorder(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateBorder(theme, widget, state),
            IRRender::kWidgetBorderDistance,
            theme.borderThickness_,
            scratch_
        );
        IRPrefab::Widget::detail::drawCenteredText(
            *canvas_,
            button.label_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateText(theme, widget)
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_BUTTON,
            IRComponents::C_Widget,
            IRComponents::C_WidgetButton,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition
        >("WidgetRenderButton");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_BUTTON_H */
