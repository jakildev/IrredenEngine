#ifndef IRREDEN_RENDER_ACTIVE_CANVAS_H
#define IRREDEN_RENDER_ACTIVE_CANVAS_H

#include <irreden/entity/ir_entity_types.hpp>

namespace IRRender {

// Returns the active canvas entity, or IREntity::kNullEntity when no
// RenderManager exists. Safe for headless and test contexts where the
// asserting getActiveCanvasEntity() would abort.
//
// Lives in a dedicated header (out of ir_render.hpp) so component
// constructors that need the snapshot — see "Constructor snapshots
// ambient state" exception in .claude/rules/cpp-ecs.md — do not pull
// in the full render surface.
IREntity::EntityId getActiveCanvasEntityOrNull();

// Names the canvas getActiveCanvasEntityOrNull() reports while no
// RenderManager exists, so a headless test can drive the systems that key
// on the active canvas (the fog reveal systems, the fog Lua service).
// Ignored whenever a RenderManager is live; kNullEntity restores the
// default.
void setHeadlessActiveCanvasEntity(IREntity::EntityId canvas);

} // namespace IRRender

#endif /* IRREDEN_RENDER_ACTIVE_CANVAS_H */
