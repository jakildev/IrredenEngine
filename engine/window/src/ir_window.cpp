
#include <irreden/ir_window.hpp>

#include <irreden/ir_profile.hpp>
#include <irreden/ir_math.hpp>

namespace IRWindow {

IRGLFWWindow *g_irglfwWindow = nullptr;
IRGLFWWindow &getWindow() {
    IR_ASSERT(g_irglfwWindow != nullptr, "IRGLFWWindow not initialized");
    return *g_irglfwWindow;
}

void closeWindow() {
    getWindow().setShouldClose();
}

void getWindowSize(IRMath::ivec2 &size) {
    getWindow().getWindowSize(size.x, size.y);
}

void getCursorPosition(IRMath::dvec2 &pos) {
    getWindow().getCursorPosition(pos.x, pos.y);
}

} // namespace IRWindow