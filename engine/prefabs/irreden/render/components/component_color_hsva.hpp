#ifndef COMPONENT_COLOR_HSVA_H
#define COMPONENT_COLOR_HSVA_H

#include <irreden/ir_math.hpp>

using IRMath::vec3;

// Ranges
// Hue: 0.0f-360.0f
// Saturation: 0.0f-1.0f
// Value: 0.0f-1.0f

namespace IRComponents {

struct C_ColorHSV {
    vec3 color_;

    C_ColorHSV(ColorHSV base)
        : color_{vec3(base.hue_, base.saturation_, base.value_)} {}

    // Default
    C_ColorHSV()
        : color_{vec3(150.0f, 0.10f, 0.80f)} {}
};

} // namespace IRComponents

#endif /* COMPONENT_COLOR_PALLET_H */