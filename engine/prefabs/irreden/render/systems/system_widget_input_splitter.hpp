#ifndef SYSTEM_WIDGET_INPUT_SPLITTER_H
#define SYSTEM_WIDGET_INPUT_SPLITTER_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/render/components/component_widget.hpp>
#include <irreden/render/components/component_gui_position.hpp>
#include <irreden/render/components/component_splitter.hpp>
#include <irreden/input/components/component_hitbox_2d_gui.hpp>
#include <irreden/render/layout.hpp>

namespace IRSystem {

// Drives IRPrefab::Layout splitter drag state from mouse input. Runs in
// the INPUT pipeline after WIDGET_INPUT so C_WidgetState::pressed_ is
// current. A single splitter drag is active at a time (tracked by
// C_LayoutState::dragSplitterParent_ via IRPrefab::Layout::getLayout()).
//
// Pipeline order requirement (INPUT pipeline):
//   HITBOX_MOUSE_TEST_GUI → WIDGET_INPUT → WIDGET_INPUT_SPLITTER
template <> struct System<WIDGET_INPUT_SPLITTER> {
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);

    void beginTick() {
        mouseGuiTrixel_ = IRPrefab::Layout::mousePositionInGuiTrixels();
    }

    void tick(
        const IRComponents::C_Splitter &splitter,
        const IRComponents::C_WidgetState &state
    ) {
        const bool isDragging = IRPrefab::Layout::isDraggingSplitter();
        const bool isThisSplitter = isDragging &&
            (IRPrefab::Layout::getLayout().dragSplitterParent_ == splitter.parentNodeIdx_) &&
            (IRPrefab::Layout::getLayout().dragSplitterChildIdx_ == splitter.childIdx_);

        const IRMath::ivec2 mouseI = IRMath::ivec2(
            static_cast<int>(mouseGuiTrixel_.x),
            static_cast<int>(mouseGuiTrixel_.y)
        );
        if (!isDragging && state.pressed_) {
            IRPrefab::Layout::beginSplitterDrag(
                splitter.parentNodeIdx_, splitter.childIdx_, mouseI
            );
        } else if (isThisSplitter && !state.pressed_) {
            IRPrefab::Layout::endSplitterDrag();
        }
    }

    void endTick() {
        if (IRPrefab::Layout::isDraggingSplitter()) {
            const IRMath::ivec2 mouseI = IRMath::ivec2(
                static_cast<int>(mouseGuiTrixel_.x),
                static_cast<int>(mouseGuiTrixel_.y)
            );
            IRPrefab::Layout::updateSplitterDrag(mouseI);
        }
    }

    static SystemId create() {
        return registerSystem<
            WIDGET_INPUT_SPLITTER,
            IRComponents::C_Splitter,
            IRComponents::C_WidgetState
        >("WidgetInputSplitter");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_WIDGET_INPUT_SPLITTER_H */
