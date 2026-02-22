#ifndef SYSTEM_CONTACT_TRIGGER_GLOW_H
#define SYSTEM_CONTACT_TRIGGER_GLOW_H

#include <irreden/ir_system.hpp>

#include <irreden/update/components/component_contact_event.hpp>
#include <irreden/update/components/component_trigger_glow.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

template <> struct System<CONTACT_TRIGGER_GLOW> {
    static SystemId create() {
        return createSystem<C_ContactEvent, C_TriggerGlow, C_VoxelSetNew>(
            "ContactTriggerGlow",
            [](const C_ContactEvent &contact, C_TriggerGlow &glow, C_VoxelSetNew &voxelSet) {
                if (voxelSet.numVoxels_ <= 0) {
                    return;
                }

                if (!glow.baseColorInitialized_) {
                    glow.baseColor_ = voxelSet.voxels_[0].color_;
                    glow.baseColorInitialized_ = true;
                }

                if (glow.triggerOnContactEnter_ && contact.entered_) {
                    glow.elapsedSeconds_ = 0.0f;
                    glow.active_ = true;
                }

                if (!glow.active_) {
                    voxelSet.changeVoxelColorAll(glow.baseColor_);
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
                    blendToTarget = 1.0f - eased;
                    if (fadeT >= 1.0f) {
                        glow.active_ = false;
                    }
                }

                voxelSet.changeVoxelColorAll(
                    IRMath::lerpColor(glow.baseColor_, glow.targetColor_, blendToTarget));
            });
    }
};

} // namespace IRSystem

#endif /* SYSTEM_CONTACT_TRIGGER_GLOW_H */
