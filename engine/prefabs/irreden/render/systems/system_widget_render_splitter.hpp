#ifndef SYSTEM_WIDGET_RENDER_SPLITTER_H
#define SYSTEM_WIDGET_RENDER_SPLITTER_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_splitter.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Renders the visual splitter bar between adjacent layout zones. The
// bar gets a highlight color when actively dragged so the user knows
// which splitter is active.
//
// Pipeline order requirement (RENDER pipeline):
//   LAYOUT_COMPUTE → ... → WIDGET_RENDER_SPLITTER → TRIXEL_TO_FRAMEBUFFER
template <> struct System<WIDGET_RENDER_SPLITTER> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRPrefab::Widget::WidgetTheme theme_;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        const IRComponents::C_Splitter &splitter,
        const IRComponents::C_Widget &widget,
        const IRComponents::C_GuiPosition &guiPos
    ) {
        if (!canvas_)
            return;

        const auto &ls = IRPrefab::Layout::getLayout();
        const bool active = IRPrefab::Layout::isDraggingSplitter() &&
                            ls.dragSplitterParent_ == splitter.parentNodeIdx_ &&
                            ls.dragSplitterChildIdx_ == splitter.childIdx_;

        IRMath::Color color = active ? theme_.borderHover_ : theme_.borderIdle_;
        IRRender::fillRect(
            *canvas_,
            guiPos.pos_,
            widget.size_,
            color,
            IRRender::kWidgetBorderDistance,
            scratch_
        );
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_RENDER_SPLITTER,
            IRComponents::C_Splitter,
            IRComponents::C_Widget,
            IRComponents::C_GuiPosition>("WidgetRenderSplitter");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_SPLITTER_H */
