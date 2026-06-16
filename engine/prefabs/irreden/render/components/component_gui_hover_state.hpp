#ifndef COMPONENT_GUI_HOVER_STATE_H
#define COMPONENT_GUI_HOVER_STATE_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Singleton: the z-ordered topmost hovered widget, published once per frame
// by System<WIDGET_INPUT>::endTick from its private topHoveredId_ routing
// result. Lets a consumer — chiefly the headless GUI-test harness (P3,
// #1796) — read "which widget is hovered" without re-scanning every hitbox.
//
// Create exactly one per world via IRPrefab::Widget::makeGuiHoverState() and
// read it back with IRPrefab::Widget::hoveredWidget(). When no instance
// exists, WIDGET_INPUT::endTick is a no-op and the accessor returns
// kNullEntity, so non-test creations pay nothing.
struct C_GuiHoverState {
    IREntity::EntityId hoveredWidget_ = IREntity::kNullEntity;
};

} // namespace IRComponents

#endif /* COMPONENT_GUI_HOVER_STATE_H */
