#ifndef SYSTEM_SPAWN_GLOW_H
#define SYSTEM_SPAWN_GLOW_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_spawn_glow.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<SPAWN_GLOW> {
    static SystemId create() {
        return createSystem<C_SpawnGlow, C_VoxelSetNew>(
            "SpawnGlow",
            [](C_SpawnGlow &glow, C_VoxelSetNew &voxelSet) {
                if (voxelSet.numVoxels_ <= 0) {
                    return;
                }

                if (!glow.active_) {
                    return;
                }

                float dt = IRTime::deltaTime(IRTime::UPDATE);
                glow.elapsedSeconds_ += dt;

                float blendToTarget = 0.0f;
                if (glow.elapsedSeconds_ <= glow.holdSeconds_) {
                    blendToTarget = 1.0f;
                } else {
                    float fadeTime = glow.elapsedSeconds_ - glow.holdSeconds_;
                    float fadeT = glow.fadeSeconds_ <= 0.0f
                                      ? 1.0f
                                      : IRMath::clamp(fadeTime / glow.fadeSeconds_, 0.0f, 1.0f);
                    float eased = IRMath::kEasingFunctions.at(glow.easingFunction_)(fadeT);
                    blendToTarget = IRMath::clamp(1.0f - eased, 0.0f, 1.0f);
                    if (fadeT >= 1.0f) {
                        glow.active_ = false;
                    }
                }

                voxelSet.changeVoxelColorAll(
                    IRMath::lerpColor(glow.baseColor_, glow.targetColor_, blendToTarget)
                );
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_SPAWN_GLOW_H */
