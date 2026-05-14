#ifndef COMPONENT_BIND_POINTS_H
#define COMPONENT_BIND_POINTS_H

/// Runtime mirror of the asset-side bind-point set
/// (`IRAsset::Rig::bindPoints_`). Each entry records a named attachment
/// point parented to a bone — local-space offset + rotation relative to
/// that bone. The world transform for a named point is composed from the
/// joint chain at query time; see `IRPrefab::Rig::worldTransformForBindPoint`
/// in `rig_bridge.hpp` and the `entity:bindPoint(name)` Lua method.
///
/// Storage is `std::unordered_map<string, BindPointRuntime>` — name lookup
/// is the only access pattern, and binding points per entity are small (a
/// few to a few dozen). Per-frame lookups are documented as a no-go in
/// `engine/prefabs/irreden/voxel/CLAUDE.md`; resolve once at spawn or on
/// interaction, then cache the integer bone index in a hot-path component
/// if a use case appears.

#include <irreden/ir_math.hpp>

#include <cstdint>
#include <string>
#include <unordered_map>

namespace IRComponents {

struct BindPointRuntime {
    std::uint32_t boneId_ = 0;
    IRMath::vec3 offset_{0.0f, 0.0f, 0.0f};
    IRMath::vec4 rotation_{0.0f, 0.0f, 0.0f, 1.0f};
};

struct C_BindPoints {
    std::unordered_map<std::string, BindPointRuntime> points_;

    C_BindPoints() = default;

    void setPoint(const std::string &name, const BindPointRuntime &point) {
        points_[name] = point;
    }

    bool hasPoint(const std::string &name) const {
        return points_.find(name) != points_.end();
    }
};

} // namespace IRComponents

#endif /* COMPONENT_BIND_POINTS_H */
