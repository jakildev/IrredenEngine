// macOS half of ir_window_platform.hpp (Linux / Windows: ir_window_platform.cpp).
#include <irreden/window/ir_window_platform.hpp>

#include <irreden/ir_profile.hpp>

#import <AppKit/AppKit.h>

namespace IRWindow {

void platformPrepareWindowMode(WindowMode mode) {
    if (mode == WindowMode::NORMAL) {
        return;
    }
    // glfwInit made the process a regular Dock application
    // (NSApplicationActivationPolicyRegular). Accessory keeps every window
    // capability but drops the Dock icon and the Cmd-Tab entry. The process
    // is never activated in these modes (GLFW_FOCUSED off skips
    // activateIgnoringOtherApps), and AppKit will not order an inactive
    // application's window in front of the active one's key window — which is
    // exactly where a BACKGROUND window belongs.
    @autoreleasepool {
        const BOOL applied = [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];
        const bool isAccessory =
            [NSApp activationPolicy] == NSApplicationActivationPolicyAccessory;
        if (applied && isAccessory) {
            IRE_LOG_INFO(
                "Window mode '{}': activation policy set to accessory (no Dock icon).",
                windowModeName(mode)
            );
        } else {
            IRE_LOG_WARN(
                "Window mode '{}': accessory activation policy not applied (applied={}, "
                "accessory={}); the window still never takes focus.",
                windowModeName(mode),
                applied ? 1 : 0,
                isAccessory ? 1 : 0
            );
        }
    }
}

void platformApplyWindowMode(GLFWwindow *, WindowMode) {
    // Nothing after creation: the hints and the activation policy cover it.
}

} // namespace IRWindow
