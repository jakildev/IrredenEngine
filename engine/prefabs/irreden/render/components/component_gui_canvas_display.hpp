#ifndef COMPONENT_GUI_CANVAS_DISPLAY_H
#define COMPONENT_GUI_CANVAS_DISPLAY_H

namespace IRComponents {

// Singleton row holding how the engine GUI canvas reaches the screen, read
// and written via `IRPrefab::GuiCanvas::` (`gui_canvas.hpp`).
struct C_GuiCanvasDisplay {
    // True: the main gather composites the GUI canvas into the
    // game-resolution framebuffer, so it upscales with the world. False:
    // GUI_CANVAS_TO_SCREEN draws it at screen resolution.
    bool pixelated_ = false;
};

} // namespace IRComponents

#endif /* COMPONENT_GUI_CANVAS_DISPLAY_H */
