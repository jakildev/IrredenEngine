#pragma once

#include <irreden/ir_math.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;

// Carve a voxel set to match an SDF sphere of the given radius.
static void carveSphere(C_VoxelSetNew &vs, float radius) {
    for (int i = 0; i < vs.numVoxels_; ++i) {
        vec3 p = vs.positions_[i].pos_;
        vec4 params{radius, radius, radius, 0.0f};
        float sdf = IRMath::SDF::evaluate(p, IRMath::SDF::ShapeType::SPHERE, params);
        if (sdf > IRMath::SDF::kSurfaceThreshold) {
            vs.voxels_[i].deactivate();
        }
    }
}
