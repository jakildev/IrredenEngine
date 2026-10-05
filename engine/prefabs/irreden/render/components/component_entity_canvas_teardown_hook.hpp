#ifndef COMPONENT_ENTITY_CANVAS_TEARDOWN_HOOK_H
#define COMPONENT_ENTITY_CANVAS_TEARDOWN_HOOK_H

#include <irreden/ir_entity.hpp>

namespace IRComponents {

// Per-world bookkeeping singleton: the id of the owner-teardown hook carried
// by this world's EntityManager. Hooks die with the manager, so the id is
// process-local, deliberately not serialized, and must not live in a static
// that could suppress registration in a later world. Written only by
// `IRPrefab::EntityCanvas::ensureOwnerTeardownHook`, which makes registration
// idempotent across all entity canvases in the world.
struct C_EntityCanvasTeardownHook {
    IREntity::PreDestroyHookId hookId_ = IREntity::kInvalidPreDestroyHookId;
};

} // namespace IRComponents

#endif /* COMPONENT_ENTITY_CANVAS_TEARDOWN_HOOK_H */
