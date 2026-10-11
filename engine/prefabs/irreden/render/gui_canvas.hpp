#ifndef GUI_CANVAS_H
#define GUI_CANVAS_H

#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>

namespace IRPrefab::GuiCanvas {

// True when the GUI canvas is drawn by GUI_CANVAS_TO_SCREEN at screen
// resolution; false when it composites into the game-resolution framebuffer
// through the main trixel gather. The pixelated setting wins, and a creation
// that never registers the screen pass keeps the framebuffer path, so its GUI
// cannot silently disappear.
inline bool compositesOnScreen() {
    return !IRRender::isGuiPixelated() &&
           IRSystem::findSystem(IRSystem::GUI_CANVAS_TO_SCREEN) != IRSystem::kNullSystemId;
}

} // namespace IRPrefab::GuiCanvas

#endif /* GUI_CANVAS_H */
