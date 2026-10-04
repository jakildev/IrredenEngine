// Linux and Windows halves of ir_window_platform.hpp; macOS builds compile
// ir_window_platform_cocoa.mm instead (engine/window/CMakeLists.txt).
#include <irreden/window/ir_window_platform.hpp>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#ifndef NOMINMAX
#define NOMINMAX 1
#endif
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace IRWindow {

void platformPrepareWindowMode(WindowMode) {
    // X11 and Win32 need nothing before creation: the GLFW_FOCUSED /
    // GLFW_FOCUS_ON_SHOW / GLFW_VISIBLE hints carry the whole mode.
}

void platformApplyWindowMode(GLFWwindow *window, WindowMode mode) {
#if defined(_WIN32)
    if (mode != WindowMode::BACKGROUND) {
        return;
    }
    // GLFW shows the window with SW_SHOWNA (no activation), but CreateWindowEx
    // still inserts a new top-level window at the top of the z-order, so it
    // would cover the human's windows while never being focused. Send it to
    // the bottom without activating it or disturbing its owner chain.
    HWND handle = glfwGetWin32Window(window);
    if (handle == nullptr) {
        return;
    }
    SetWindowPos(
        handle,
        HWND_BOTTOM,
        0,
        0,
        0,
        0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOOWNERZORDER
    );
#else
    (void)window;
    (void)mode;
#endif
}

} // namespace IRWindow
