#ifndef SYSTEM_VELOCITY_H
#define SYSTEM_VELOCITY_H

#include <irreden/ir_system.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/update/components/component_velocity_3d.hpp>

using namespace IRComponents;

namespace IRSystem {

template <> struct System<VELOCITY_3D> {
    // The tick body writes only the iterating entity's own
    // `C_LocalTransform` and reads only its own `C_Velocity3D` + the
    // frame-scoped `IRTime::deltaTime(UPDATE)` constant — no manager
    // calls, no EntityId param, no globals besides IRTime's read-only
    // delta, so PARALLEL_FOR is safe here.
    //
    // Both components stay non-const in the registration pack: marking
    // `C_Velocity3D` const would exercise the SystemManager's
    // `getComponentData<const T>(node)` resolution, which the
    // const-in-pack dispatch path does not verify.
    static constexpr Concurrency kConcurrency = Concurrency::PARALLEL_FOR;

    void tick(C_LocalTransform &localXform, const C_Velocity3D &velocity) {
        localXform.translation_ += velocity.velocity_ * vec3(IRTime::deltaTime(IRTime::UPDATE));
    }

    static SystemId create() {
        return registerSystem<VELOCITY_3D, C_LocalTransform, C_Velocity3D>("Velocity3D");
    }
};

} // namespace IRSystem

#endif /* SYSTEM_VELOCITY_H */
