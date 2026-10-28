#ifndef COMPONENT_CANVAS_DRAW_LAYER_H
#define COMPONENT_CANVAS_DRAW_LAYER_H

namespace IRComponents {

// Tags a trixel canvas that `IRPrefab::CanvasDraw::createLayer` made: it has
// no producer of its own, so `System<CANVAS_DRAW>` clears it every frame
// before flushing the draws queued onto it.
struct C_CanvasDrawLayer {};

} // namespace IRComponents

#endif /* COMPONENT_CANVAS_DRAW_LAYER_H */
