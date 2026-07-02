#pragma once

#include <irreden/ir_math.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

// Carve a voxel set to match an SDF sphere of the given radius.
static void carveSphere(IRComponents::C_VoxelSetNew &vs, float radius) {
    vec4 params{radius, radius, radius, 0.0f};
    vs.carve([&](vec3 p) {
        return IRMath::SDF::evaluate(p, IRMath::SDF::ShapeType::SPHERE, params) >
               IRMath::SDF::kSurfaceThreshold;
    });
}
