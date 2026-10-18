#ifndef GUI_CANVAS_H
#define GUI_CANVAS_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_gui_canvas_display.hpp>

namespace IRPrefab::GuiCanvas {

inline IRComponents::C_GuiCanvasDisplay &display() {
    return IREntity::singleton<IRComponents::C_GuiCanvasDisplay>();
}

// Registration-time singleton touch: both composite systems call it from
// `create()`, so the lazy create runs on the main thread during pipeline
// wiring and never inside a first-frame `beginTick`.
inline void ensureDisplaySingleton() {
    display();
}

// Pixelated GUI (default off; the `gui_pixelated` world-config key): the GUI
// canvas composites into the game-resolution framebuffer and upscales with it,
// so its lattice triangles snap to the game pixel grid. Off,
// GUI_CANVAS_TO_SCREEN draws the canvas at screen resolution with anti-aliased
// triangle edges. At the minimum 2x1-pixel texel footprint the composite uses
// hard triangle ownership because no fractional edge sample can leave both
// neighbours solid. Takes effect on the next frame.
inline void setPixelated(bool pixelated) {
    display().pixelated_ = pixelated;
}

inline bool isPixelated() {
    return display().pixelated_;
}

// True when the GUI canvas is drawn by GUI_CANVAS_TO_SCREEN at screen
// resolution; false when it composites into the game-resolution framebuffer
// through the main trixel gather. The pixelated setting wins, and a creation
// that never registers the screen pass keeps the framebuffer path, so its GUI
// cannot silently disappear. Both composite systems latch this once per frame
// in `beginTick`: exactly one of them draws the canvas.
inline bool compositesOnScreen() {
    return !isPixelated() &&
           IRSystem::findSystem(IRSystem::GUI_CANVAS_TO_SCREEN) != IRSystem::kNullSystemId;
}

} // namespace IRPrefab::GuiCanvas

#endif /* GUI_CANVAS_H */
