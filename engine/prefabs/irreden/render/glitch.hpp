#ifndef IR_PREFAB_GLITCH_H
#define IR_PREFAB_GLITCH_H

// Adoption surface for the period-quantised framebuffer glitch.
//
// A creation registers the pass immediately before the screen blit, after
// every main-framebuffer producer:
//
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_GLITCH>());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::FRAMEBUFFER_TO_SCREEN>());
//
// and drives it through `setState` (C++) or `IRRender.setGlitch` (Lua). With
// the state off the pass encodes no GPU work and the blit runs unchanged.
// Contract and effect model: docs/design/glitch-pass.md.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/render/components/component_glitch_state.hpp>
#include <irreden/render/glitch_math.hpp>
#include <irreden/render/systems/system_framebuffer_glitch.hpp>

#include <string>

namespace IRPrefab::Glitch {

inline const IRComponents::C_GlitchState *stateOrNull() {
    return IREntity::singletonOrNull<IRComponents::C_GlitchState>();
}

// The stored state, or the off default when nothing has set one.
inline IRComponents::C_GlitchState stateOrDefault() {
    const IRComponents::C_GlitchState *stored = stateOrNull();
    return stored != nullptr ? *stored : IRComponents::C_GlitchState{};
}

// Empty when @p candidate may be stored at @p timeSeconds, else the first
// violated rule. Rejects every value the tick math cannot turn into a defined
// int64 tick at that time, including a hold period that underflows to zero.
inline std::string validate(const IRComponents::C_GlitchState &candidate, double timeSeconds) {
    const auto unit = [](float value) {
        return IRMath::isFinite(value) && value >= 0.0f && value <= 1.0f;
    };
    if (!unit(candidate.intensity_)) {
        return "intensity must be a finite number in [0, 1]";
    }
    if (!unit(candidate.tearWeight_) || !unit(candidate.holdWeight_) ||
        !unit(candidate.splitWeight_) || !unit(candidate.barsWeight_) ||
        !unit(candidate.cellsWeight_) || !unit(candidate.depthWeight_)) {
        return "effect weights must be finite numbers in [0, 1]";
    }
    if (!IRMath::isFinite(candidate.periodSeconds_) || candidate.periodSeconds_ <= 0.0) {
        return "period must be a finite number greater than 0";
    }
    if (!IRMath::isFinite(candidate.phaseSeconds_)) {
        return "phase must be a finite number";
    }
    if (candidate.holdDivisor_ < 1) {
        return "holdDivisor must be an integer >= 1";
    }
    if (!(holdPeriodSeconds(candidate) > 0.0)) {
        return "period / holdDivisor underflows to 0";
    }
    if (!isRepresentableTick(
            tickQuotient(timeSeconds, candidate.periodSeconds_, candidate.phaseSeconds_)
        ) ||
        !isRepresentableTick(
            tickQuotient(timeSeconds, holdPeriodSeconds(candidate), candidate.phaseSeconds_)
        )) {
        return "period, holdDivisor and phase give a tick outside the int64 range";
    }
    return {};
}

// Stores @p candidate, creating the singleton on first use. The candidate must
// pass `validate` at the current engine time; untrusted input (Lua) checks it
// there first and raises instead.
inline void setState(const IRComponents::C_GlitchState &candidate) {
    IR_ASSERT(
        validate(candidate, engineTimeSeconds()).empty(),
        "IRPrefab::Glitch::setState: candidate fails validate()"
    );
    IREntity::singleton<IRComponents::C_GlitchState>() = candidate;
}

// The current state's clock on the engine clock.
inline Clock clock() {
    return clockAt(stateOrDefault(), engineTimeSeconds());
}

// The registered glitch pass named @p Name, or null when the creation did not
// register it.
template <IRSystem::SystemName Name> IRSystem::System<Name> *glitchSystemOrNull() {
    const IRSystem::SystemId id = IRSystem::findSystem(Name);
    if (id == IRSystem::kNullSystemId) {
        return nullptr;
    }
    return IRSystem::getSystemParams<IRSystem::System<Name>>(id);
}

inline IRSystem::System<IRSystem::FRAMEBUFFER_GLITCH> *systemOrNull() {
    return glitchSystemOrNull<IRSystem::FRAMEBUFFER_GLITCH>();
}

// True only on the RENDER tick in which `FRAMEBUFFER_GLITCH` has already
// cleared the default target and drawn the glitched main framebuffer onto it.
// `FRAMEBUFFER_TO_SCREEN` then skips its whole blit; on every other tick
// (absent system, off state, stale publication) it runs unchanged.
inline bool screenCompositedForCurrentRenderTick() {
    const auto *system = systemOrNull();
    return system != nullptr &&
           system->isPublishedForRenderTick(IRSystem::getEventTickCount(IRTime::Events::RENDER));
}

} // namespace IRPrefab::Glitch

#endif /* IR_PREFAB_GLITCH_H */
