#ifndef POINTER_GESTURE_H
#define POINTER_GESTURE_H

#include <irreden/ir_math.hpp>

#include <cstdint>

namespace IRWindow {

enum class PointerGestureKind { SCROLL, MAGNIFY };

// Precision describes the event, not the physical device: some mice also
// produce precise scrolling. UNKNOWN must not be guessed from delta size.
enum class PointerGestureSource { UNKNOWN, PRECISE_SCROLL, WHEEL };

enum class PointerGesturePhase : std::uint8_t {
    NONE = 0,
    BEGAN = 1 << 0,
    CHANGED = 1 << 1,
    ENDED = 1 << 2,
    CANCELLED = 1 << 3,
    MAY_BEGIN = 1 << 4,
    STATIONARY = 1 << 5,
};

constexpr bool hasGesturePhase(PointerGesturePhase value, PointerGesturePhase phase) {
    return (static_cast<std::uint8_t>(value) & static_cast<std::uint8_t>(phase)) != 0;
}

struct PointerGestureEvent {
    PointerGestureKind kind_ = PointerGestureKind::SCROLL;
    PointerGestureSource source_ = PointerGestureSource::UNKNOWN;
    PointerGesturePhase phase_ = PointerGesturePhase::NONE;
    PointerGesturePhase momentumPhase_ = PointerGesturePhase::NONE;
    // GLFW scroll units: Cocoa precise point deltas are multiplied by 0.1.
    IRMath::dvec2 scroll_{0.0, 0.0};
    // Native magnification change, independent of scroll units.
    double magnification_ = 0.0;
    // Logical window points, top-left origin; never framebuffer pixels.
    IRMath::dvec2 windowPoint_{0.0, 0.0};
    // Shift/Control/Alt/Super occupy bits 0..3, as in GLFW and IRInput.
    std::uint8_t modifiers_ = 0;
    bool directionInverted_ = false;
};

} // namespace IRWindow

#endif
