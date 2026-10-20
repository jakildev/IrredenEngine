#ifndef COMPONENT_WIDGET_FOCUS_REQUEST_H
#define COMPONENT_WIDGET_FOCUS_REQUEST_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Singleton: a programmatic keyboard-focus request, written by
// IRPrefab::Widget::focus() and consumed by the next
// System<WIDGET_INPUT>::beginTick, which owns focusedWidgetId_. Lazily
// created on the first request, so creations that never ask pay nothing.
struct C_WidgetFocusRequest {
    IREntity::EntityId widget_ = IREntity::kNullEntity;
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_FOCUS_REQUEST_H */
