#ifndef SYSTEM_WIDGET_RENDER_DOCK_PREVIEW_H
#define SYSTEM_WIDGET_RENDER_DOCK_PREVIEW_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/trixel_rect.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Renders semi-transparent drop-zone overlays when a panel drag is
// active. One overlay appears on every leaf zone that can accept the
// dragged panel; the nearest valid drop target gets a brighter
// highlight. Runs in the RENDER pipeline just before
// TRIXEL_TO_FRAMEBUFFER so overlays appear on top of all widget content.
//
// Per-entity tick (C_Widget) is a no-op; all work happens in endTick so
// we iterate the layout tree directly instead of querying per-entity.
//
// Pipeline order requirement (RENDER pipeline):
//   WIDGET_RENDER_SPLITTER → WIDGET_RENDER_DOCK_PREVIEW
//   → TRIXEL_TO_FRAMEBUFFER
template <> struct System<WIDGET_RENDER_DOCK_PREVIEW> {
    IRComponents::C_TriangleCanvasTextures *canvas_ = nullptr;
    IRRender::RectFillScratch scratch_;

    void beginTick() {
        canvas_ = nullptr;
        if (!IRPrefab::Layout::isDraggingPanel())
            return;
        IREntity::EntityId guiCanvas = IRRender::getCanvas("gui");
        canvas_ = &IREntity::getComponent<IRComponents::C_TriangleCanvasTextures>(guiCanvas);
    }

    void tick(const IRComponents::C_LayoutLeaf &) {
        // work is done in endTick
    }

    void endTick() {
        if (!canvas_ || !IRPrefab::Layout::isDraggingPanel())
            return;

        const auto &ls = IRPrefab::Layout::getLayout();
        const IRMath::ivec2 dragPos = IRPrefab::Layout::getDragCurrentPos();
        const int nearestIdx = IRPrefab::Layout::checkDropTarget(dragPos);

        const IRMath::Color overlayColor{60, 120, 200, 120};
        const IRMath::Color highlightColor{120, 200, 255, 180};

        const auto &nodes = ls.nodes_;
        for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
            const auto &node = nodes[i];
            if (node.type_ != IRPrefab::Layout::LayoutNode::Type::LEAF)
                continue;
            if (i == ls.draggedPanelNodeIdx_)
                continue;

            const IRMath::Color color = (i == nearestIdx) ? highlightColor : overlayColor;
            IRRender::fillRect(
                *canvas_,
                node.pos_,
                node.size_,
                color,
                IRRender::kWidgetBackgroundDistance,
                scratch_
            );
        }
    }

    static SystemId create() {
        return registerSystem<WIDGET_RENDER_DOCK_PREVIEW, IRComponents::C_LayoutLeaf>(
            "WidgetRenderDockPreview"
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_RENDER_DOCK_PREVIEW_H */
