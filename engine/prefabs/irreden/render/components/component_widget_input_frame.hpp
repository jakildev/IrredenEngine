#ifndef COMPONENT_WIDGET_INPUT_FRAME_H
#define COMPONENT_WIDGET_INPUT_FRAME_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <string>

namespace IRComponents {

struct C_WidgetInputFrame {
    IREntity::EntityId focusedWidget_ = IREntity::kNullEntity;
    bool focusVisible_ = false;
    bool hintsVisible_ = false;
    IRMath::vec2 mouseGuiTrixel_ = IRMath::vec2(0.0f);
    // The GUI canvas this frame's widget geometry is laid out against; zero
    // when there is none.
    IRMath::ivec2 guiCanvasSize_ = IRMath::ivec2(0);
    std::string typedText_;
};

} // namespace IRComponents

#endif /* COMPONENT_WIDGET_INPUT_FRAME_H */
