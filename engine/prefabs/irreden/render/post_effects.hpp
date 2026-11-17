#ifndef IR_PREFAB_POST_EFFECTS_H
#define IR_PREFAB_POST_EFFECTS_H

// Adoption surface for the framebuffer post effects (bloom, blur, colour
// grade).
//
// A creation registers the pass immediately before the screen blit, after
// every main-framebuffer producer and after the glitch pass when it has one:
//
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_POST_EFFECTS>());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());
//
// and drives it through `setState` (C++) or `IRRender.setPostEffects` (Lua).
// With every effect off the pass encodes no GPU work and the blit runs
// unchanged. Contract and effect model: docs/design/post-effects.md.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_post_effects_state.hpp>
#include <irreden/render/post_effects_math.hpp>
#include <irreden/render/prefab_system_probe.hpp>
#include <irreden/render/systems/system_framebuffer_post_effects.hpp>

#include <string>

namespace IRPrefab::PostEffects {

inline const IRComponents::C_PostEffectsState *stateOrNull() {
    return IREntity::singletonOrNull<IRComponents::C_PostEffectsState>();
}

// The stored state, or the off default when nothing has set one.
inline IRComponents::C_PostEffectsState stateOrDefault() {
    const IRComponents::C_PostEffectsState *stored = stateOrNull();
    return stored != nullptr ? *stored : IRComponents::C_PostEffectsState{};
}

// Empty when @p candidate may be stored, else the first violated rule.
inline std::string validate(const IRComponents::C_PostEffectsState &candidate) {
    const auto within = [](float value, float lo, float hi) {
        return IRMath::isFinite(value) && value >= lo && value <= hi;
    };
    const auto channelsWithin = [&within](IRMath::vec3 value, float lo, float hi) {
        return within(value.r, lo, hi) && within(value.g, lo, hi) && within(value.b, lo, hi);
    };
    if (!within(candidate.bloomThreshold_, 0.0f, 1.0f)) {
        return "bloomThreshold must be a finite number in [0, 1]";
    }
    if (!within(candidate.bloomKnee_, 0.0f, 1.0f)) {
        return "bloomKnee must be a finite number in [0, 1]";
    }
    if (!within(candidate.bloomIntensity_, 0.0f, kMaxBloomIntensity)) {
        return "bloomIntensity must be a finite number in [0, 8]";
    }
    if (!within(candidate.bloomScatter_, 0.0f, 1.0f)) {
        return "bloomScatter must be a finite number in [0, 1]";
    }
    if (candidate.bloomLevels_ < 1 || candidate.bloomLevels_ > kMaxBloomLevels) {
        return "bloomLevels must be an integer in [1, 6]";
    }
    switch (candidate.bloomBlend_) {
    case IRComponents::BloomBlend::ADD:
    case IRComponents::BloomBlend::SCREEN:
        break;
    default:
        return "bloomBlend must be a BloomBlend member";
    }
    if (!within(candidate.blurRadius_, 0.0f, kMaxBlurRadius)) {
        return "blurRadius must be a finite number in [0, 32]";
    }
    if (!within(candidate.exposureStops_, -kMaxExposureStops, kMaxExposureStops)) {
        return "exposure must be a finite number in [-8, 8]";
    }
    if (!channelsWithin(candidate.lift_, -1.0f, 1.0f)) {
        return "lift channels must be finite numbers in [-1, 1]";
    }
    if (!channelsWithin(candidate.gamma_, kMinGamma, kMaxGamma)) {
        return "gamma channels must be finite numbers in [0.1, 8]";
    }
    if (!channelsWithin(candidate.gain_, 0.0f, kMaxGain)) {
        return "gain channels must be finite numbers in [0, 8]";
    }
    if (!within(candidate.saturation_, 0.0f, kMaxSaturation)) {
        return "saturation must be a finite number in [0, 4]";
    }
    if (!within(candidate.contrast_, 0.0f, kMaxContrast)) {
        return "contrast must be a finite number in [0, 4]";
    }
    return {};
}

// Stores @p candidate, creating the singleton on first use. The candidate
// must pass `validate`; untrusted input (Lua) checks it there first and
// raises instead.
inline void setState(const IRComponents::C_PostEffectsState &candidate) {
    IR_ASSERT(
        validate(candidate).empty(),
        "IRPrefab::PostEffects::setState: candidate fails validate()"
    );
    IREntity::singleton<IRComponents::C_PostEffectsState>() = candidate;
}

// The registered pass, or null when the creation did not register it.
inline IRSystem::System<IRSystem::FRAMEBUFFER_POST_EFFECTS> *systemOrNull() {
    return IRPrefab::detail::systemOrNull<IRSystem::FRAMEBUFFER_POST_EFFECTS>();
}

// True only on the RENDER tick in which `FRAMEBUFFER_POST_EFFECTS` has
// already drawn its output image. On every other tick (absent system, every
// effect off, stale publication) the pass presents nothing.
inline bool outputPublishedForCurrentRenderTick() {
    const auto *system = systemOrNull();
    return system != nullptr &&
           system->isPublishedForRenderTick(IRSystem::getEventTickCount(IRTime::Events::RENDER));
}

// The pass's output on a RENDER tick it drew one, else null. Call it after
// the pass ran this tick; before, it reports the previous tick's answer.
inline const IRRender::Texture2D *outputColorOrNull() {
    if (!outputPublishedForCurrentRenderTick()) {
        return nullptr;
    }
    return systemOrNull()->outputColorOrNull();
}

} // namespace IRPrefab::PostEffects

#endif /* IR_PREFAB_POST_EFFECTS_H */
