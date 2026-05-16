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
// in the full render surface. See #753 (T-205).
IREntity::EntityId getActiveCanvasEntityOrNull();

} // namespace IRRender

#endif /* IRREDEN_RENDER_ACTIVE_CANVAS_H */
