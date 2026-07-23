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
        const IRComponents::C_WidgetButton &button,
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
        IRPrefab::Widget::detail::queueCenteredText(
            textCmds_,
            canvas_->size_,
            button.label_,
            guiPos.pos_,
            widget.size_,
            IRPrefab::Widget::detail::stateText(theme_, widget)
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_BUTTON,
            IRComponents::C_Widget,
            IRComponents::C_WidgetButton,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition>("WidgetRenderButton");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_BUTTON_H */
