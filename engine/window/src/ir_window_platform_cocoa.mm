// macOS half of ir_window_platform.hpp (Linux / Windows: ir_window_platform.cpp).
#include <irreden/window/ir_window_platform.hpp>

#include <irreden/ir_profile.hpp>

#import <AppKit/AppKit.h>

#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>

namespace IRWindow {

namespace {

struct PointerGestureMonitor {
    id token_ = nil;
};

PointerGesturePhase gesturePhase(NSEventPhase phase) {
    std::uint8_t result = 0;
    const auto add = [&](NSEventPhase native, PointerGesturePhase portable) {
        if ((phase & native) != 0)
            result |= static_cast<std::uint8_t>(portable);
    };
    add(NSEventPhaseBegan, PointerGesturePhase::BEGAN);
    add(NSEventPhaseChanged, PointerGesturePhase::CHANGED);
    add(NSEventPhaseEnded, PointerGesturePhase::ENDED);
    add(NSEventPhaseCancelled, PointerGesturePhase::CANCELLED);
    add(NSEventPhaseMayBegin, PointerGesturePhase::MAY_BEGIN);
    add(NSEventPhaseStationary, PointerGesturePhase::STATIONARY);
    return static_cast<PointerGesturePhase>(result);
}

} // namespace

void *platformInstallPointerGestures(IRGLFWWindow &window) {
    @autoreleasepool {
        NSWindow *nativeWindow = glfwGetCocoaWindow(window.getRawWindow());
        IRGLFWWindow *owner = &window;
        id token = [NSEvent
            addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel | NSEventMaskMagnify
                                         handler:^NSEvent *(NSEvent *event) {
                                           if ([event window] != nativeWindow)
                                               return event;
                                           NSView *view = [nativeWindow contentView];
                                           const NSPoint point =
                                               [view convertPoint:[event locationInWindow]
                                                         fromView:nil];
                                           const NSRect bounds = [view bounds];
                                           const NSEventPhase phase = [event phase];
                                           const NSEventPhase momentum =
                                               [event type] == NSEventTypeScrollWheel
                                                   ? [event momentumPhase]
                                                   : NSEventPhaseNone;
                                           // A gesture's ending event can lie outside the content
                                           // after its beginning was accepted; do not strand its
                                           // captured owner.
                                           if (!NSPointInRect(point, bounds) &&
                                               momentum == NSEventPhaseNone &&
                                               (phase == NSEventPhaseNone ||
                                                (phase &
                                                 (NSEventPhaseBegan | NSEventPhaseMayBegin))))
                                               return event;
                                           PointerGestureEvent input;
                                           input.windowPoint_ = {
                                               point.x - NSMinX(bounds),
                                               [view isFlipped] ? point.y - NSMinY(bounds)
                                                                : NSMaxY(bounds) - point.y
                                           };
                                           input.phase_ = gesturePhase(phase);
                                           input.momentumPhase_ = gesturePhase(momentum);
                                           const NSEventModifierFlags flags = [event modifierFlags];
                                           input.modifiers_ =
                                               ((flags & NSEventModifierFlagShift) ? GLFW_MOD_SHIFT
                                                                                   : 0) |
                                               ((flags & NSEventModifierFlagControl)
                                                    ? GLFW_MOD_CONTROL
                                                    : 0) |
                                               ((flags & NSEventModifierFlagOption) ? GLFW_MOD_ALT
                                                                                    : 0) |
                                               ((flags & NSEventModifierFlagCommand)
                                                    ? GLFW_MOD_SUPER
                                                    : 0);
                                           if ([event type] == NSEventTypeMagnify) {
                                               input.kind_ = PointerGestureKind::MAGNIFY;
                                               input.magnification_ = [event magnification];
                                           } else {
                                               const bool precise =
                                                   [event hasPreciseScrollingDeltas];
                                               input.source_ =
                                                   precise ? PointerGestureSource::PRECISE_SCROLL
                                                           : PointerGestureSource::UNKNOWN;
                                               const double scale = precise ? 0.1 : 1.0;
                                               input.scroll_ = {
                                                   [event scrollingDeltaX] * scale,
                                                   [event scrollingDeltaY] * scale
                                               };
                                               input.directionInverted_ =
                                                   [event isDirectionInvertedFromDevice];
                                           }
                                           owner->addPointerGestureToProcess(input);
                                           // The engine queue replaces GLFW's scroll callback for
                                           // this event.
                                           return nil;
                                         }];
        if (token == nil)
            return nullptr;
        return new PointerGestureMonitor{[token retain]};
    }
}

void platformRemovePointerGestures(void *monitor) {
    if (monitor == nullptr)
        return;
    auto *owned = static_cast<PointerGestureMonitor *>(monitor);
    [NSEvent removeMonitor:owned->token_];
    [owned->token_ release];
    delete owned;
}

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
        const bool isAccessory = [NSApp activationPolicy] == NSApplicationActivationPolicyAccessory;
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
