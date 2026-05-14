#ifndef COMPONENT_BIND_POINTS_H
#define COMPONENT_BIND_POINTS_H

// Per-entity bind-point map; treat IREntity.bindPoint(entity, name) as a one-time query, not per-tick.

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
