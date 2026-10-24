#ifndef IR_PREFAB_SYSTEM_PROBE_H
#define IR_PREFAB_SYSTEM_PROBE_H

#include <irreden/ir_system.hpp>

namespace IRPrefab::detail {

// The registered `System<Name>` instance, or null when the creation never
// registered it. Every prefab's headless probe resolves through this so a
// creation without the feature reads a clean negative instead of dereferencing
// null.
//
// The miss sentinel is `IRSystem::kNullSystemId`, NOT `IREntity::kNullEntity`:
// 0 is a live system id (the first system a process registers gets it), and
// the wrong sentinel breaks a probe both ways — a genuine miss walks past the
// guard into an out-of-range `getSystemParams`, and a system registered as id
// 0 reads as absent. `prefab_system_probe_test.cpp` pins both directions.
//
// Not a per-frame surface — one hash lookup per call.
template <IRSystem::SystemName Name> IRSystem::System<Name> *systemOrNull() {
    const IRSystem::SystemId id = IRSystem::findSystem(Name);
    if (id == IRSystem::kNullSystemId) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<Name>>(id);
}

} // namespace IRPrefab::detail

#endif /* IR_PREFAB_SYSTEM_PROBE_H */
