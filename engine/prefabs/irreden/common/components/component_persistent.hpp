#ifndef COMPONENT_PERSISTENT_H
#define COMPONENT_PERSISTENT_H

namespace IRComponents {

// Tag marking a non-singleton entity to survive `IREntity::resetGameplay()`
// (the scene-transition teardown primitive, #1814). Singleton entities are
// preserved automatically — the EntityManager's singleton cache IS the
// preserve registry — so this tag is only needed for the rare engine- or
// creation-created entity that is NOT a singleton yet must outlive a scene
// reset (e.g. the RenderManager's camera + canvas entities). Empty tag: holds
// no data, only group membership.
struct C_Persistent {};

} // namespace IRComponents

#endif /* COMPONENT_PERSISTENT_H */
