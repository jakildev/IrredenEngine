
#include <irreden/window/ir_glfw_window.hpp>

namespace IRWindow {

/// Global pointer to the active GLFW window; managed by the engine runtime.
/// Prefer @ref getWindow() for safe access.
extern IRGLFWWindow *g_irglfwWindow;
/// Returns a reference to the active window. Asserts if not initialised.
IRGLFWWindow &getWindow();

/// Signals the window to close on the next `World::gameLoop()` iteration.
void closeWindow();
/// True once a close has been requested (by @ref closeWindow or the window
/// manager) and the loop has not yet exited. The read side of @ref closeWindow:
/// a headless test that drives a QUIT control has no other way to observe that
/// the click reached the engine, since the run ends before the next capture.
bool isCloseRequested();
/// Writes the current window size in logical pixels to @p size.
/// Use @ref getFramebufferSize when you need the actual render target dimensions.
void getWindowSize(IRMath::ivec2 &size);
/// Writes the framebuffer dimensions in physical pixels to @p size.
/// Under HiDPI / scaling this is larger than the logical window size.
void getFramebufferSize(IRMath::ivec2 &size);
/// Writes the raw GLFW cursor position in window-space pixels to @p pos.
void getCursorPosition(IRMath::dvec2 &pos);

} // namespace IRWindow