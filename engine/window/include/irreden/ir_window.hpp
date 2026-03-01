
#include <irreden/window/ir_glfw_window.hpp>

namespace IRWindow {

extern IRGLFWWindow *g_irglfwWindow;
IRGLFWWindow &getWindow();

void closeWindow();
void getWindowSize(IRMath::ivec2 &size);
void getFramebufferSize(IRMath::ivec2 &size);
void getCursorPosition(IRMath::dvec2 &pos);

} // namespace IRWindow