#ifndef COMPONENT_FOG_REVEALED_H
#define COMPONENT_FOG_REVEALED_H

#include <cstdint>

namespace IRComponents {

constexpr std::uint32_t kFogChannelDefault = 1u;

enum class FogOverride : std::uint8_t { NONE = 0, FORCE_HIDDEN = 1, FORCE_REVEALED = 2 };

enum class FogHiddenPolicy : std::uint8_t { HIDE = 0, GHOST = 1 };

struct C_FogRevealed {
    float revealFactor_ = 0.0f;
    bool shown_ = false;
    FogOverride override_ = FogOverride::NONE;
    bool ghostHeld_ = false;
    std::uint32_t channels_ = kFogChannelDefault;
};

static_assert(sizeof(C_FogRevealed) == 12);

} // namespace IRComponents

#endif /* COMPONENT_FOG_REVEALED_H */
