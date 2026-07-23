#ifndef SYSTEM_WIDGET_INPUT_PANEL_DRAG_H
#define SYSTEM_WIDGET_INPUT_PANEL_DRAG_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_layout_leaf.hpp>
#include <irreden/render/layout.hpp>
#include <irreden/render/widget_theme.hpp>
#include <irreden/render/trixel_font.hpp>

namespace IRSystem {

// Detects title-bar press on layout-leaf panel entities and drives the
// panel drag-to-dock flow in IRPrefab::Layout. Panels that are NOT
// layout leaves are ignored (not part of the managed dockspace).
//
// Panel title-bar height is kGlyphStepY + 2*theme_.padding_ (same value
// used by WIDGET_RENDER_PANEL to draw the title bar).
//
// Pipeline order requirement (INPUT pipeline):
//   HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT → WIDGET_INPUT_SPLITTER
//   → WIDGET_INPUT_PANEL_DRAG
template <> struct System<WIDGET_INPUT_PANEL_DRAG> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    bool mouseLeftPressedThisFrame_ = false;
    bool mouseLeftDown_ = false;
    IRPrefab::Widget::WidgetTheme theme_;

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();

        mouseLeftPressedThisFrame_ = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::PRESSED
        );
        const bool heldThisFrame = IRInput::checkKeyMouseButton(
            IRInput::KeyMouseButtons::kMouseButtonLeft,
            IRInput::ButtonStatuses::HELD
        );
        mouseLeftDown_ = heldThisFrame || mouseLeftPressedThisFrame_;
        theme_ = IRPrefab::Widget::defaultTheme();
    }

    void tick(
        IREntity::EntityId entityId,
        const IRComponents::C_Widget &widget,
        const IRComponents::C_GuiPosition &guiPos,
        const IRComponents::C_LayoutLeaf &leaf
    ) {
        const int titleBarH = IRRender::kGlyphStepY + theme_.padding_ * 2;

        const IRMath::ivec2 mouseI =
            IRMath::ivec2(static_cast<int>(mouseGuiTrixel_.x), static_cast<int>(mouseGuiTrixel_.y));

        const bool isDraggingThis = IRPrefab::Layout::isDraggingPanel() &&
                                    IRPrefab::Layout::getDraggedPanelNodeIdx() == leaf.nodeIdx_;

        if (isDraggingThis) {
            if (!mouseLeftDown_) {
                IRPrefab::Layout::endPanelDrag(mouseI);
            } else {
                IRPrefab::Layout::updatePanelDrag(mouseI);
            }
            return;
        }

        if (IRPrefab::Layout::isDraggingPanel())
            return;

        if (mouseLeftPressedThisFrame_) {
            const bool inTitleBar =
                mouseI.x >= guiPos.pos_.x && mouseI.x < guiPos.pos_.x + widget.size_.x &&
                mouseI.y >= guiPos.pos_.y && mouseI.y < guiPos.pos_.y + titleBarH;

            if (inTitleBar) {
                IRPrefab::Layout::beginPanelDrag(leaf.nodeIdx_, entityId, mouseI, guiPos.pos_);
            }
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_INPUT_PANEL_DRAG,
            IRComponents::C_Widget,
            IRComponents::C_GuiPosition,
            IRComponents::C_LayoutLeaf>("WidgetInputPanelDrag");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_INPUT_PANEL_DRAG_H */
