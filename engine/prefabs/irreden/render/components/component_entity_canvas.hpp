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
    // Foreground depth priority (#1958 two-tier partition; the #1884 Bug-A fix).
    // 0 (default) = world content: the canvas depth-sorts against world geometry
    // on the shared iso-depth convention (the #1624 world-placed default). != 0 =
    // FOREGROUND priority: ENTITY_CANVAS_TO_FRAMEBUFFER pins the canvas's
    // model-frame local iso-depth into a reserved near depth band
    // (kDepthForegroundCeil) so the solid renders unconditionally in front of the
    // floor / any world geometry below it, at all zooms and yaws, INDEPENDENT of
    // world extent — for floating showcases that must not clip behind the floor.
    // Only meaningful when !screenLocked_ (a screen-locked overlay already sits at
    // a fixed near depth). Two-tier for now: any non-zero value selects the single
    // foreground tier; per-trixel priority tiers are #1960. Read directly by the
    // composite (which already iterates C_EntityCanvas), so no per-tick getComponent.
    int depthPriority_ = 0;

    C_EntityCanvas() = default;

    C_EntityCanvas(
        IREntity::EntityId canvasEntity,
        ivec2 canvasSize,
        bool visible = true,
        bool screenLocked = false,
        int depthPriority = 0
    )
        : canvasEntity_{canvasEntity}
        , canvasSize_{canvasSize}
        , visible_{visible}
        , screenLocked_{screenLocked}
        , depthPriority_{depthPriority} {}
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_H */
