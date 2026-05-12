#ifndef SYSTEM_LAYOUT_COMPUTE_H
#define SYSTEM_LAYOUT_COMPUTE_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Propagates computed layout-tree positions and sizes to leaf widget
// entities each frame. Must run before any WIDGET_RENDER_* system so
// widgets are positioned from the current frame's layout tree state
// (which may have changed due to splitter drag in the INPUT pipeline).
//
// Pipeline order requirement (RENDER pipeline):
//   LAYOUT_COMPUTE → WIDGET_RENDER_PANEL → ... → TRIXEL_TO_FRAMEBUFFER
template <> struct System<LAYOUT_COMPUTE> {
    void beginTick() {
        IRPrefab::Layout::compute();
    }

    void tick(
        const IRComponents::C_LayoutLeaf &leaf,
        IRComponents::C_GuiPosition &guiPos,
        IRComponents::C_Widget &widget
    ) {
        if (leaf.nodeIdx_ < 0) return;
        const auto &node = IRPrefab::Layout::getNode(leaf.nodeIdx_);
        guiPos.pos_ = node.pos_;
        widget.size_ = node.size_;
    }

    static SystemId create() {
        return registerSystem<
            LAYOUT_COMPUTE,
            IRComponents::C_LayoutLeaf,
            IRComponents::C_GuiPosition,
            IRComponents::C_Widget
        >("LayoutCompute");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_LAYOUT_COMPUTE_H */
