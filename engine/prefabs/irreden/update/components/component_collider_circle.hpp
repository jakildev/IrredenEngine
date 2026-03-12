#ifndef COMPONENT_COLLIDER_CIRCLE_H
#define COMPONENT_COLLIDER_CIRCLE_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

namespace IRComponents {

// 2D circle in xy plane for movement collision (e.g. wall proximity).
struct C_ColliderCircle {
    float radius_;
    float movementCollisionRadius_;
    float preferredMovementRadius_;
    vec3 centerOffset_;

    C_ColliderCircle(
        float radius,
        float movementCollisionRadius,
        float preferredMovementRadius,
        vec3 centerOffset
    )
        : radius_{radius}
        , movementCollisionRadius_{movementCollisionRadius}
        , preferredMovementRadius_{preferredMovementRadius}
        , centerOffset_{centerOffset} {}

    C_ColliderCircle(float radius, float movementCollisionRadius, float preferredMovementRadius)
        : radius_{radius}
        , movementCollisionRadius_{movementCollisionRadius}
        , preferredMovementRadius_{preferredMovementRadius}
        , centerOffset_{vec3(0.0f)} {}

    C_ColliderCircle(float radius, float movementCollisionRadius, vec3 centerOffset)
        : radius_{radius}
        , movementCollisionRadius_{movementCollisionRadius}
        , preferredMovementRadius_{0.5f * (radius + movementCollisionRadius)}
        , centerOffset_{centerOffset} {}

    C_ColliderCircle(float radius, float movementCollisionRadius)
        : radius_{radius}
        , movementCollisionRadius_{movementCollisionRadius}
        , preferredMovementRadius_{0.5f * (radius + movementCollisionRadius)}
        , centerOffset_{vec3(0.0f)} {}

    explicit C_ColliderCircle(float radius)
        : radius_{radius}
        , movementCollisionRadius_{radius * 0.3f}
        , preferredMovementRadius_{radius * 0.65f}
        , centerOffset_{vec3(0.0f)} {}

    C_ColliderCircle(float radius, vec3 centerOffset)
        : radius_{radius}
        , movementCollisionRadius_{radius * 0.3f}
        , preferredMovementRadius_{radius * 0.65f}
        , centerOffset_{centerOffset} {}

    C_ColliderCircle()
        : C_ColliderCircle(0.5f, vec3(0.0f)) {}
};

} // namespace IRComponents

#endif /* COMPONENT_COLLIDER_CIRCLE_H */
