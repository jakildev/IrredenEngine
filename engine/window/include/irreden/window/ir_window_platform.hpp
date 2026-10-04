#ifndef IR_WINDOW_PLATFORM_H
#define IR_WINDOW_PLATFORM_H

#include <irreden/window/ir_glfw_window.hpp>

namespace IRWindow {

/// Per-OS adjustments GLFW's hints cannot express, applied around window
/// creation for the non-NORMAL modes. Both are no-ops for NORMAL and on
/// platforms with nothing to do:
///   - macOS (`ir_window_platform_cocoa.mm`): @ref platformPrepareWindowMode
///     switches the process to the accessory activation policy after
///     `glfwInit` (which made it a regular Dock app), so a BACKGROUND or
///     HIDDEN launch never shows a Dock icon or takes a Cmd-Tab slot.
///   - Windows (`ir_window_platform.cpp`): @ref platformApplyWindowMode pushes
///     a BACKGROUND window to the bottom of the z-order without activating
///     it; GLFW's non-activating show still inserts a new top-level window on
///     top of everything.
void platformPrepareWindowMode(WindowMode mode);
void platformApplyWindowMode(GLFWwindow *window, WindowMode mode);

} // namespace IRWindow

#endif /* IR_WINDOW_PLATFORM_H */
