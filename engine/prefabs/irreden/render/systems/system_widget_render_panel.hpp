#ifndef SYSTEM_WIDGET_RENDER_PANEL_H
#define SYSTEM_WIDGET_RENDER_PANEL_H

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

// Renders panel widgets onto the GUI canvas: a single background rect,
// optional title-bar rect, optional border. Runs after TEXT_TO_TRIXEL
// (so the canvas clear has already happened and any external GUI text
// — perf overlay, etc. — has been drawn). Widgets overpaint where they
// overlap; creations are expected to lay them out away from existing
// overlay text. Lifting that limitation requires extracting the GUI
// canvas clear into its own system; tracked as a follow-up.
template <> struct System<WIDGET_RENDER_PANEL> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    }

    void tick(
        const IRComponents::C_Widget &widget,
        const IRComponents::C_WidgetPanel &panel,
        const IRComponents::C_WidgetState &state,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_) return;

        const auto &theme = IRPrefab::Widget::defaultTheme();
        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            theme.panelBackground_,
            IRRender::kWidgetBackgroundDistance,
            scratch_
        );

        if (!panel.title_.empty()) {
            const int titleBarH = IRRender::kGlyphStepY + theme.padding_ * 2;
            IRRender::fillRect(
                *canvas_,
                guiPos.pos_,
                IRMath::ivec2(widget.size_.x, titleBarH),
                theme.panelTitleBackground_,
                IRRender::kWidgetBorderDistance,
                scratch_
            );
            IRPrefab::Widget::detail::drawCenteredText(
                *canvas_,
                panel.title_,
                guiPos.pos_,
                IRMath::ivec2(widget.size_.x, titleBarH),
                IRPrefab::Widget::detail::stateText(theme, widget)
            );
        }

        if (panel.drawBorder_) {
            IRRender::drawBorder(
                *canvas_,
                guiPos.pos_,
                widget.size_,
                IRPrefab::Widget::detail::stateBorder(theme, widget, state),
                IRRender::kWidgetBorderDistance,
                theme.borderThickness_,
                scratch_
            );
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_PANEL,
            IRComponents::C_Widget,
            IRComponents::C_WidgetPanel,
            IRComponents::C_WidgetState,
            IRComponents::C_GuiPosition
        >("WidgetRenderPanel");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_PANEL_H */
