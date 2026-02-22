#ifndef COMPONENT_GRAVITY_3D_H
#define COMPONENT_GRAVITY_3D_H

#include <irreden/ir_math.hpp>
#include <irreden/update/components/component_direction.hpp>
#include <irreden/update/components/component_magnitude.hpp>

namespace IRComponents {

constexpr float kGravityMagnitudeDefault = 0.01f;

// TODO: Example of component with sub-components
// perhaps this could be a entity archetype made of two
// compoennts: Gravity3d archetype has <C_Magnitude, C_Direction3d>
struct C_Gravity3D {
    C_Direction3D direction_;
    C_Magnitude magnitude_;

    C_Gravity3D(C_Magnitude magnitude, C_Direction3D direction)
        : magnitude_{magnitude}
        , direction_{direction} {}

    C_Gravity3D(C_Magnitude magnitude)
        : C_Gravity3D{magnitude, C_Direction3D{}} {}

    C_Gravity3D()
        : C_Gravity3D{C_Magnitude{kGravityMagnitudeDefault}, C_Direction3D{kDirecton3DDown}} {}

    vec3 getVector() const {
        return direction_.direction_ * magnitude_.magnitude_;
    }

    void setDirection(vec3 direction) {
        direction_.set(direction);
    }

    void setMagnitude(float magnitude) {
        magnitude_.set(magnitude);
    }
};

} // namespace IRComponents

#endif /* COMPONENT_GRAVITY_3D_H */
