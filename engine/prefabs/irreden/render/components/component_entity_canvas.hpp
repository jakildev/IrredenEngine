#ifndef COMPONENT_ENTITY_CANVAS_H
#define COMPONENT_ENTITY_CANVAS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

using namespace IRMath;

namespace IRComponents {

struct C_EntityCanvas {
    IREntity::EntityId canvasEntity_ = IREntity::kNullEntity;
    ivec2 canvasSize_{0};
    bool visible_ = true;

    C_EntityCanvas() = default;

    C_EntityCanvas(IREntity::EntityId canvasEntity, ivec2 canvasSize, bool visible = true)
        : canvasEntity_{canvasEntity}, canvasSize_{canvasSize}, visible_{visible} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
