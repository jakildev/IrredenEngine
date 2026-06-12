#ifndef COMPONENT_DETACHED_CANVAS_H
#define COMPONENT_DETACHED_CANVAS_H

namespace IRComponents {

// Tag marking a canvas entity as a detached, per-entity canvas — one
// owned by a parent entity's `C_EntityCanvas` and composited by
// `ENTITY_CANVAS_TO_FRAMEBUFFER` at the parent's iso position. The
// full-screen `TRIXEL_TO_FRAMEBUFFER` pass iterates every
// `C_TriangleCanvasTextures` canvas, so without this tag a detached
// canvas would *also* be blitted across the whole framebuffer (a second,
// camera-scaled copy on top of the per-entity composite). `TRIXEL_TO_FRAMEBUFFER`
// excludes tagged canvases. Attached by `IRPrefab::EntityCanvas::create`
// and `createWithVoxelPool`; the "main" / "background" / "gui" screen
// canvases never carry it.
struct C_DetachedCanvas {};

} // namespace IRComponents

#endif /* COMPONENT_DETACHED_CANVAS_H */
