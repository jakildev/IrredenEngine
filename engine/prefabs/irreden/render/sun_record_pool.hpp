#ifndef SUN_RECORD_POOL_H
#define SUN_RECORD_POOL_H

#include <irreden/ir_system.hpp>

#include <irreden/render/sun_face_query_layout.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>

#include <irreden/ir_profile.hpp>

#include <cstdint>

namespace IRPrefab::SunShadow {

// Diagnostic sizing of the sun face record pool, applied to the registered
// BAKE_SUN_SHADOW_MAP system. @p capacity reallocates the pool at that size
// now; @p ceiling bounds later frame-latent growth. A ceiling equal to the
// capacity freezes the pool, which reproduces a fixed-capacity build. Both
// must be powers of two in [kSourceFaceHierarchyLeafSize, lost-record bit).
inline bool configureRecordPool(std::uint32_t capacity, std::uint32_t ceiling) {
    const auto system = IRSystem::findSystem(IRSystem::BAKE_SUN_SHADOW_MAP);
    if (system == IRSystem::kNullSystemId || !sourceFaceCapacityValid(capacity) ||
        !sourceFaceCapacityValid(ceiling) || ceiling < capacity) {
        return false;
    }
    auto *bake = IRSystem::getSystemParams<IRSystem::System<IRSystem::BAKE_SUN_SHADOW_MAP>>(system);
    bake->recordCapacityCeiling_ = ceiling;
    if (capacity != bake->recordLayout_.capacity_) {
        bake->resizeRecordPool(capacity);
    }
    return true;
}

// Command-line form: 0 keeps the engine default for either value, and a
// ceiling below the requested capacity rises to meet it.
inline bool configureRecordPoolFromRequest(int capacity, int ceiling) {
    if (capacity <= 0 && ceiling <= 0) {
        return true;
    }
    const auto requestedCapacity =
        capacity > 0 ? static_cast<std::uint32_t>(capacity) : kSourceFaceCapacity;
    const auto requestedCeiling =
        ceiling > 0 ? static_cast<std::uint32_t>(ceiling) : kSourceFaceCapacityCeiling;
    const auto effectiveCeiling =
        requestedCeiling < requestedCapacity ? requestedCapacity : requestedCeiling;
    if (!configureRecordPool(requestedCapacity, effectiveCeiling)) {
        return false;
    }
    IR_LOG_INFO(
        "SUN-RECORD-POOL configured capacity={} ceiling={}",
        requestedCapacity,
        effectiveCeiling
    );
    return true;
}

} // namespace IRPrefab::SunShadow

#endif
