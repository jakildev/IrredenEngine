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
#include <algorithm>
#include <limits>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

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

                auto computeClipProgress = [](const C_ActionAnimation &anim,
                                              const C_AnimationClip &clip) -> double {
                    if (clip.phaseCount_ <= 0) {
                        return 1.0;
                    }
                    if (anim.currentPhase_ < 0) {
                        return 0.0;
                    }
                    if (anim.currentPhase_ >= clip.phaseCount_) {
                        return 1.0;
                    }

                    double totalDuration = 0.0;
                    double elapsedBeforeCurrent = 0.0;
                    for (int i = 0; i < clip.phaseCount_; ++i) {
                        const double phaseDur =
                            std::max(0.001, clip.phases_[i].durationSeconds_);
                        totalDuration += phaseDur;
                        if (i < anim.currentPhase_) {
                            elapsedBeforeCurrent += phaseDur;
                        }
                    }
                    if (totalDuration <= 0.0) {
                        return 1.0;
                    }

                    const double currentPhaseDur =
                        std::max(0.001, clip.phases_[anim.currentPhase_].durationSeconds_);
                    const double phaseT =
                        std::clamp(anim.phaseElapsed_ / currentPhaseDur, 0.0, 1.0);
                    const double elapsed = elapsedBeforeCurrent + (phaseT * currentPhaseDur);
                    return std::clamp(elapsed / totalDuration, 0.0, 1.0);
                };

                auto computeElapsedAtPhaseStart = [](const C_AnimationClip &clip, int phase) -> double {
                    if (phase <= 0) {
                        return 0.0;
                    }
                    const int phaseClamped = std::clamp(phase, 0, clip.phaseCount_);
                    double elapsed = 0.0;
                    for (int i = 0; i < phaseClamped; ++i) {
                        elapsed += std::max(0.001, clip.phases_[i].durationSeconds_);
                    }
                    return elapsed;
                };

                auto computeCurrentClipElapsed = [&](const C_ActionAnimation &anim,
                                                     const C_AnimationClip &clip) -> double {
                    if (clip.phaseCount_ <= 0) {
                        return 0.0;
                    }
                    if (anim.currentPhase_ <= 0) {
                        const double phaseDur = std::max(0.001, clip.phases_[0].durationSeconds_);
                        return std::clamp(anim.phaseElapsed_, 0.0, phaseDur);
                    }
                    if (anim.currentPhase_ >= clip.phaseCount_) {
                        return computeElapsedAtPhaseStart(clip, clip.phaseCount_);
                    }

                    const double phaseDur =
                        std::max(0.001, clip.phases_[anim.currentPhase_].durationSeconds_);
                    return computeElapsedAtPhaseStart(clip, anim.currentPhase_) +
                           std::clamp(anim.phaseElapsed_, 0.0, phaseDur);
                };

                if (anim.isPlaying()) {
                    colorState.lastClipEntity_ = anim.activeClip_;

                    const C_AnimClipColorTrack *track = fetchColorTrack(anim.activeClip_);
                    const C_AnimationClip *clip = fetchClip(anim.activeClip_);

                    if (track && clip &&
                        anim.currentPhase_ >= 0 &&
                        anim.currentPhase_ < clip->phaseCount_) {
                        const auto &phase = clip->phases_[anim.currentPhase_];
                        double phaseDur = static_cast<double>(phase.durationSeconds_);
                        if (phaseDur <= 0.0) phaseDur = 0.001;

                        double t = anim.phaseElapsed_ / phaseDur;
                        if (t > 1.0) t = 1.0;

                        Color resultColor = colorState.baseColor_;

                        if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET &&
                            anim.currentPhase_ < track->phaseCount_) {
                            const auto &pm = track->phaseMods_[anim.currentPhase_];
                            float eased = kEasingFunctions.at(pm.easingFunction_)(
                                static_cast<float>(t));
                            ColorHSV offset = IRMath::lerpHSV(pm.startMod_, pm.endMod_, eased);
                            resultColor = IRMath::applyHSVOffset(colorState.baseColor_, offset);
                        } else if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET_STATE_BLEND) {
                            const double clipProgress = computeClipProgress(anim, *clip);
                            ColorHSV offset = IRMath::lerpHSV(
                                track->startMod_,
                                track->endMod_,
                                static_cast<float>(clipProgress)
                            );
                            resultColor = IRMath::applyHSVOffset(colorState.baseColor_, offset);
                        } else if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET_TIMELINE) {
                            ColorHSV offset = track->endMod_;
                            const double currentElapsed = computeCurrentClipElapsed(anim, *clip);
                            bool activeSegmentFound = false;

                            double nearestFutureStart = std::numeric_limits<double>::infinity();
                            ColorHSV nearestFutureStartMod = track->startMod_;

                            double latestPastEnd = -1.0;
                            ColorHSV latestPastEndMod = track->endMod_;

                            for (int i = 0; i < track->timelineModCount_; ++i) {
                                const auto &segment = track->timelineMods_[i];
                                int fromPhase = std::min(segment.fromPhase_, segment.toPhase_);
                                int toPhase = std::max(segment.fromPhase_, segment.toPhase_);
                                fromPhase = std::clamp(fromPhase, 0, clip->phaseCount_ - 1);
                                toPhase = std::clamp(toPhase, 0, clip->phaseCount_ - 1);

                                const double segmentStart =
                                    computeElapsedAtPhaseStart(*clip, fromPhase);
                                const double segmentEnd =
                                    computeElapsedAtPhaseStart(*clip, toPhase) +
                                    std::max(0.001, clip->phases_[toPhase].durationSeconds_);
                                const double segmentLength = std::max(0.001, segmentEnd - segmentStart);

                                if (currentElapsed >= segmentStart &&
                                    currentElapsed <= segmentEnd) {
                                    const double localT = std::clamp(
                                        (currentElapsed - segmentStart) / segmentLength,
                                        0.0,
                                        1.0
                                    );
                                    float eased = kEasingFunctions.at(segment.easingFunction_)(
                                        static_cast<float>(localT)
                                    );
                                    offset = IRMath::lerpHSV(
                                        segment.startMod_,
                                        segment.endMod_,
                                        eased
                                    );
                                    activeSegmentFound = true;
                                    break;
                                }

                                if (currentElapsed < segmentStart &&
                                    segmentStart < nearestFutureStart) {
                                    nearestFutureStart = segmentStart;
                                    nearestFutureStartMod = segment.startMod_;
                                }
                                if (currentElapsed > segmentEnd && segmentEnd > latestPastEnd) {
                                    latestPastEnd = segmentEnd;
                                    latestPastEndMod = segment.endMod_;
                                }
                            }

                            if (!activeSegmentFound) {
                                if (latestPastEnd >= 0.0) {
                                    offset = latestPastEndMod;
                                } else if (nearestFutureStart <
                                           std::numeric_limits<double>::infinity()) {
                                    offset = nearestFutureStartMod;
                                }
                            }

                            resultColor = IRMath::applyHSVOffset(colorState.baseColor_, offset);
                        } else {
                            if (anim.currentPhase_ < track->phaseCount_) {
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
                                    IRMath::applyHSVOffset(colorState.baseColor_, track->idleMod_);
                            } else if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET_STATE_BLEND) {
                                colorState.currentColor_ =
                                    IRMath::applyHSVOffset(colorState.baseColor_, track->endMod_);
                            } else if (track->mode_ == ANIM_COLOR_TRACK_HSV_OFFSET_TIMELINE) {
                                ColorHSV idleOffset = track->endMod_;
                                int bestToPhase = std::numeric_limits<int>::min();
                                for (int i = 0; i < track->timelineModCount_; ++i) {
                                    const auto &segment = track->timelineMods_[i];
                                    const int segmentTo =
                                        std::max(segment.fromPhase_, segment.toPhase_);
                                    if (segmentTo > bestToPhase) {
                                        bestToPhase = segmentTo;
                                        idleOffset = segment.endMod_;
                                    }
                                }
                                colorState.currentColor_ =
                                    IRMath::applyHSVOffset(colorState.baseColor_, idleOffset);
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
