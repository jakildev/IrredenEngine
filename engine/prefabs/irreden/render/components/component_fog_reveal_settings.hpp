#ifndef COMPONENT_FOG_REVEAL_SETTINGS_H
#define COMPONENT_FOG_REVEAL_SETTINGS_H

#include <cstdint>

namespace IRComponents {

struct C_FogRevealSettings {
    float showThreshold_ = 0.5f;
    float hideThreshold_ = 0.35f;
    std::uint32_t staggerPeriod_ = 1;
};

} // namespace IRComponents

#endif /* COMPONENT_FOG_REVEAL_SETTINGS_H */
