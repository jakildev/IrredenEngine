#ifndef EDITOR_WIDGET_GEOMETRY_H
#define EDITOR_WIDGET_GEOMETRY_H

#include <irreden/render/widgets.hpp>
#include <irreden/render/components/component_gui_position.hpp>

namespace IRVoxelEditor {
inline bool widgetContainsCursor(IREntity::EntityId id, IRMath::vec2 cursor) {
    const auto widget = IREntity::getComponentOptional<IRComponents::C_Widget>(id);
    const auto position = IREntity::getComponentOptional<IRComponents::C_GuiPosition>(id);
    if (!widget || !position || !(*widget)->visible_)
        return false;
    const auto start = (*position)->pos_;
    const auto size = (*widget)->size_;
    return cursor.x >= start.x && cursor.y >= start.y && cursor.x < start.x + size.x &&
           cursor.y < start.y + size.y;
}
} // namespace IRVoxelEditor

#endif
