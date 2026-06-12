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
    // Screen-locked overlay opt-OUT (#1624; supersedes the #1582 Option B
    // default — see docs/design/detached-canvas-depth-default.md). Default OFF
    // means a detached canvas is WORLD-PLACED: ENTITY_CANVAS_TO_FRAMEBUFFER
    // sets distanceOffset_ = the entity's world iso depth so the canvas's
    // pool-centered trixel distances land in the shared world depth band and
    // depth-sort against GRID solids, the floor, and each other; re-voxelize
    // solids also receive (P4b-2) and cast (P4b-3) world sun-shadow there.
    // Set TRUE for genuine overlay cases (HUD props, billboards, floating
    // showcases): the composite writes the canvas at a fixed near-zero depth,
    // on top of world geometry and unaffected by the entity's world iso depth
    // — byte-identical to the pre-#1624 default. Read directly by the
    // composite (which already iterates C_EntityCanvas), so no per-tick
    // foreign getComponent.
    bool screenLocked_ = false;

    C_EntityCanvas() = default;

    C_EntityCanvas(
        IREntity::EntityId canvasEntity,
        ivec2 canvasSize,
        bool visible = true,
        bool screenLocked = false
    )
        : canvasEntity_{canvasEntity}
        , canvasSize_{canvasSize}
        , visible_{visible}
        , screenLocked_{screenLocked} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
