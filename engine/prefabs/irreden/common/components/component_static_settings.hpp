#ifndef COMPONENT_STATIC_SETTINGS_H
#define COMPONENT_STATIC_SETTINGS_H

#include <cstdint>

namespace IRComponents {

/// Singleton: tuning for the static-resident transition step
/// (`IRPrefab::Static::drain`).
struct C_StaticSettings {
    /// Most settling entities promoted to settled per round; the rest wait
    /// for the next round, lowest settle stamp first. Zero removes the cap:
    /// a loader sets it for the duration of a load and accepts one long
    /// frame.
    std::uint32_t promotionsPerRound_ = 8192;
};

} // namespace IRComponents

#endif /* COMPONENT_STATIC_SETTINGS_H */
