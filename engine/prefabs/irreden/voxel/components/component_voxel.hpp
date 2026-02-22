#ifndef COMPONENT_VOXEL_H
#define COMPONENT_VOXEL_H

#include <irreden/ir_math.hpp>

using IRMath::Color;

struct C_Voxel {
    Color color_;

    C_Voxel(Color color)
        : color_{color} {}

    // Default
    C_Voxel()
        : C_Voxel{Color{0, 0, 0, 255}} {}

    void activate() {
        color_.alpha_ = 255;
    }

    void deactivate() {
        color_.alpha_ = 0;
    }
};

#endif /* COMPONENT_VOXEL_H */
