#ifndef IR_PREFAB_DEBUG_OVERLAY_H
#define IR_PREFAB_DEBUG_OVERLAY_H

// Driver-side API for the LIGHTING_TO_TRIXEL pass's false-color debug
// visualization of lighting buffers. Singleton state — one overlay
// mode globally, stored in an `inline` variable so every translation
// unit that includes this header sees the same value.
//
// The enum integer values are mirrored by `debugOverlayMode_` in
// `c_lighting_to_trixel.glsl` and `c_lighting_to_trixel.metal`;
// changing them here requires editing both shader files in lockstep.

#include <cstdint>
#include <cstring>

namespace IRPrefab::DebugOverlay {

/// False-color visualization of lighting buffers, applied during the
/// LIGHTING_TO_TRIXEL pass. When set to anything other than `NONE` the
/// final composite color is replaced by the selected debug
/// visualization; upstream lighting passes (AO, sun shadow) still run
/// unchanged so the values being visualized are exactly what the
/// artistic path consumes.
/// - `NONE`        — no overlay; normal artistic lighting/composite.
/// - `AO`          — ambient-occlusion factor as red→green
///                   (red = fully occluded, green = fully unoccluded).
/// - `LIGHT_LEVEL` — combined AO × sun-shadow scalar painted as
///                   blue→white (blue = dark, white = bright).
/// - `SHADOW`      — directional sun-shadow occupancy (black = lit,
///                   magenta = shadowed).
enum class Mode : std::uint8_t {
    NONE = 0,
    AO = 1,
    LIGHT_LEVEL = 2,
    SHADOW = 3
};

namespace detail {
inline Mode g_mode = Mode::NONE;
} // namespace detail

inline void set(Mode mode) {
    detail::g_mode = mode;
}

inline Mode get() {
    return detail::g_mode;
}

/// Parse a string to `Mode`. Accepts "none", "ao", "light_level",
/// "shadow". Returns `NONE` for null or unrecognized input.
inline Mode modeFromString(const char *s) {
    if (s == nullptr)                       return Mode::NONE;
    if (std::strcmp(s, "ao") == 0)          return Mode::AO;
    if (std::strcmp(s, "light_level") == 0) return Mode::LIGHT_LEVEL;
    if (std::strcmp(s, "shadow") == 0)      return Mode::SHADOW;
    return Mode::NONE;
}

} // namespace IRPrefab::DebugOverlay

#endif /* IR_PREFAB_DEBUG_OVERLAY_H */
