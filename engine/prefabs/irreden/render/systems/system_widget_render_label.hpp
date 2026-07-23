#ifndef SYSTEM_WIDGET_RENDER_LABEL_H
#define SYSTEM_WIDGET_RENDER_LABEL_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/widget_draw.hpp>
#include <irreden/render/trixel_text.hpp>

namespace IRSystem {

template <> struct System<WIDGET_RENDER_LABEL> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    std::vector<IRRender::GlyphDrawCommand> textCmds_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetLabel &label,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;
        if (label.text_.empty())
            return;
        const IRMath::Color color = (label.colorOverride_.alpha_ == 0)
                                        ? IRPrefab::Widget::detail::stateText(theme_, widget)
                                        : label.colorOverride_;
        IRPrefab::GuiText::queueGuiText(
            textCmds_,
            label.text_,
            guiPos.pos_,
            canvas_->size_,
            color,
            IRPrefab::Widget::detail::kWidgetTextFontSize
        );
    }

    void endTick() {
        IRPrefab::GuiText::dispatchGuiText(textCmds_);
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_LABEL,
            IRComponents::C_Widget,
            IRComponents::C_WidgetLabel,
            IRComponents::C_GuiPosition>("WidgetRenderLabel");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_LABEL_H */
