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
    // World-placement opt-in (#1576 P4b-1). Default OFF keeps the screen-locked
    // overlay contract (#1582 Option B / PR #1583): the composite writes the
    // canvas at a fixed near-zero depth so it sits on top of world geometry,
    // unaffected by the entity's world iso depth. When ON, ENTITY_CANVAS_TO_
    // FRAMEBUFFER sets distanceOffset_ = the entity's world iso depth so the
    // canvas's pool-centered trixel distances land in the shared world depth
    // band and depth-sort against GRID solids, the floor, and each other.
    // Read directly by the composite (which already iterates C_EntityCanvas),
    // so no per-tick foreign getComponent. Receive (P4b-2) and cast (P4b-3)
    // gate further plumbing on this same flag.
    bool worldPlaced_ = false;

    C_EntityCanvas() = default;

    C_EntityCanvas(
        IREntity::EntityId canvasEntity,
        ivec2 canvasSize,
        bool visible = true,
        bool worldPlaced = false
    )
        : canvasEntity_{canvasEntity}
        , canvasSize_{canvasSize}
        , visible_{visible}
        , worldPlaced_{worldPlaced} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
