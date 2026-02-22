#ifndef COMPONENT_COLLIDER_ISO3D_AABB_H
#define COMPONENT_COLLIDER_ISO3D_AABB_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {

// Local-space AABB around an entity's visual origin.
struct C_ColliderIso3DAABB {
    vec3 halfExtents_;
    vec3 centerOffset_;

    C_ColliderIso3DAABB(vec3 halfExtents, vec3 centerOffset = vec3(0.0f))
        : halfExtents_{halfExtents}
        , centerOffset_{centerOffset} {}

    C_ColliderIso3DAABB(float hx, float hy, float hz)
        : C_ColliderIso3DAABB(vec3(hx, hy, hz), vec3(0.0f)) {}

    C_ColliderIso3DAABB()
        : C_ColliderIso3DAABB(vec3(0.5f), vec3(0.0f)) {}
};

} // namespace IRComponents

#endif /* COMPONENT_COLLIDER_ISO3D_AABB_H */
