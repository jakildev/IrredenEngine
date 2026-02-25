#ifndef SYSTEM_ANIMATION_COLOR_H
#define SYSTEM_ANIMATION_COLOR_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <irreden/update/components/component_action_animation.hpp>
#include <irreden/update/components/component_animation_clip.hpp>
#include <irreden/update/components/component_anim_clip_color_track.hpp>
#include <irreden/update/components/component_anim_color_state.hpp>
#include <irreden/voxel/components/component_voxel_set.hpp>

#include <unordered_map>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

namespace detail {

inline Color applyHSVOffset(const Color &base, const ColorHSV &offset) {
    ColorHSV hsv = colorToColorHSV(base);
    hsv.hue_        = IRMath::fract(hsv.hue_ + offset.hue_);
    hsv.saturation_ = IRMath::clamp(hsv.saturation_ + offset.saturation_, 0.0f, 1.0f);
    hsv.value_      = IRMath::clamp(hsv.value_ + offset.value_, 0.0f, 1.0f);
    hsv.alpha_      = IRMath::clamp(hsv.alpha_ + offset.alpha_, 0.0f, 1.0f);
    return colorHSVToColor(hsv);
}

inline ColorHSV lerpHSV(const ColorHSV &a, const ColorHSV &b, float t) {
    return ColorHSV{
        a.hue_        + (b.hue_        - a.hue_)        * t,
        a.saturation_ + (b.saturation_ - a.saturation_) * t,
        a.value_      + (b.value_      - a.value_)      * t,
        a.alpha_      + (b.alpha_      - a.alpha_)      * t
    };
}

} // namespace detail

template <> struct System<ANIMATION_COLOR> {
    static SystemId create() {
        static std::unordered_map<IREntity::EntityId, const C_AnimClipColorTrack*> colorTrackCache;
        static std::unordered_map<IREntity::EntityId, const C_AnimationClip*> clipCache;

        return createSystem<C_ActionAnimation, C_AnimColorState, C_VoxelSetNew>(
            "AnimationColor",
            [](const C_ActionAnimation &anim,
               C_AnimColorState &colorState,
               C_VoxelSetNew &voxelSet) {
                if (voxelSet.numVoxels_ <= 0) return;

                if (!colorState.baseInitialized_) {
                    colorState.baseColor_ = voxelSet.voxels_[0].color_;
                    colorState.currentColor_ = colorState.baseColor_;
                    colorState.baseInitialized_ = true;
                }

                auto fetchColorTrack = [](IREntity::EntityId id) -> const C_AnimClipColorTrack* {
                    if (id == IREntity::kNullEntity) return nullptr;
                    auto it = colorTrackCache.find(id);
                    if (it != colorTrackCache.end()) return it->second;
                    auto opt = IREntity::getComponentOptional<C_AnimClipColorTrack>(id);
                    const C_AnimClipColorTrack *ptr = opt.has_value() ? opt.value() : nullptr;
                    colorTrackCache[id] = ptr;
                    return ptr;
                };

                auto fetchClip = [](IREntity::EntityId id) -> const C_AnimationClip* {
                    if (id == IREntity::kNullEntity) return nullptr;
                    auto it = clipCache.find(id);
                    if (it != clipCache.end()) return it->second;
                    auto opt = IREntity::getComponentOptional<C_AnimationClip>(id);
                    const C_AnimationClip *ptr = opt.has_value() ? opt.value() : nullptr;
                    clipCache[id] = ptr;
                    return ptr;
                };

                if (anim.isPlaying()) {
                    colorState.lastClipEntity_ = anim.activeClip_;

                    const C_AnimClipColorTrack *track = fetchColorTrack(anim.activeClip_);
                    const C_AnimationClip *clip = fetchClip(anim.activeClip_);

                    if (track && clip &&
                        track->phaseCount_ > 0 &&
                        anim.currentPhase_ >= 0 &&
                        anim.currentPhase_ < track->phaseCount_)
                    {
                        const auto &phase = clip->phases_[anim.currentPhase_];
                        double phaseDur = static_cast<double>(phase.durationSeconds_);
                        if (phaseDur <= 0.0) phaseDur = 0.001;

                        double t = anim.phaseElapsed_ / phaseDur;
                        if (t > 1.0) t = 1.0;

                        Color resultColor = colorState.baseColor_;

                        if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET) {
                            const auto &pm = track->phaseMods_[anim.currentPhase_];
                            float eased = kEasingFunctions.at(pm.easingFunction_)(
                                static_cast<float>(t));
                            ColorHSV offset = detail::lerpHSV(pm.startMod_, pm.endMod_, eased);
                            resultColor = detail::applyHSVOffset(colorState.baseColor_, offset);
                        } else {
                            const auto &pc = track->phaseColors_[anim.currentPhase_];
                            float eased = kEasingFunctions.at(pc.easingFunction_)(
                                static_cast<float>(t));
                            Color trackColor = IRMath::lerpColor(
                                pc.startColor_, pc.endColor_, eased);

                            switch (colorState.blendMode_) {
                            case ANIM_COLOR_BLEND_MULTIPLY:
                                resultColor = Color{
                                    static_cast<uint8_t>((colorState.baseColor_.red_ * trackColor.red_) / 255),
                                    static_cast<uint8_t>((colorState.baseColor_.green_ * trackColor.green_) / 255),
                                    static_cast<uint8_t>((colorState.baseColor_.blue_ * trackColor.blue_) / 255),
                                    static_cast<uint8_t>((colorState.baseColor_.alpha_ * trackColor.alpha_) / 255)
                                };
                                break;
                            case ANIM_COLOR_BLEND_LERP:
                                resultColor = IRMath::lerpColor(
                                    colorState.baseColor_, trackColor, eased);
                                break;
                            case ANIM_COLOR_BLEND_REPLACE:
                            default:
                                resultColor = trackColor;
                                break;
                            }
                        }

                        colorState.currentColor_ = resultColor;
                    }
                    // phaseCount == 0: no per-phase color, keep currentColor_ as-is
                } else {
                    // Idle: apply the last-played clip's idle color/modifier
                    if (colorState.lastClipEntity_ != IREntity::kNullEntity) {
                        const C_AnimClipColorTrack *track =
                            fetchColorTrack(colorState.lastClipEntity_);
                        if (track) {
                            if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET) {
                                colorState.currentColor_ =
                                    detail::applyHSVOffset(colorState.baseColor_, track->idleMod_);
                            } else {
                                colorState.currentColor_ = track->idleColor_;
                            }
                        }
                    }
                }

                voxelSet.changeVoxelColorAll(colorState.currentColor_);
            },
            []() {
                colorTrackCache.clear();
                clipCache.clear();
            }
        );
    }
};

} // namespace IRSystem

#endif /* SYSTEM_ANIMATION_COLOR_H */
