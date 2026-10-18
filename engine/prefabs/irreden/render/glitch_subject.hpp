#ifndef IR_PREFAB_GLITCH_SUBJECT_H
#define IR_PREFAB_GLITCH_SUBJECT_H

// Adoption surface for the canvas-space subject glitch.
//
// A creation registers the pass after every main-canvas colour producer
// (`FOG_TO_TRIXEL` when present) and immediately before the composite:
//
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_GLITCH>());
//   renderPipeline.push_back(IRSystem::createSystem<IRSystem::TRIXEL_TO_FRAMEBUFFER>());
//
// and marks entities through `setSubject` (C++) or `IRRender.setGlitchSubject`
// (Lua). The pass shares `C_GlitchState`'s period clock and seed with
// `FRAMEBUFFER_GLITCH`. With no active subject it encodes no GPU work.
// Contract: docs/design/glitch-pass.md.

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>

#include <irreden/render/components/component_glitch_subject.hpp>
#include <irreden/render/glitch.hpp>
#include <irreden/render/systems/system_trixel_glitch.hpp>
#include <irreden/render/trixel_glitch_math.hpp>

#include <string>

namespace IRPrefab::Glitch {

// Empty when @p candidate may be stored, else the first violated rule.
inline std::string validateSubject(const IRComponents::C_GlitchSubject &candidate) {
    const auto unit = [](float value) {
        return IRMath::isFinite(value) && value >= 0.0f && value <= 1.0f;
    };
    if (!unit(candidate.weight_)) {
        return "weight must be a finite number in [0, 1]";
    }
    if (!unit(candidate.tearWeight_) || !unit(candidate.holdWeight_) ||
        !unit(candidate.splitWeight_) || !unit(candidate.echoWeight_) ||
        !unit(candidate.dissolveWeight_)) {
        return "effect weights must be finite numbers in [0, 1]";
    }
    if (candidate.spill_ < 0 || candidate.spill_ > kSubjectMaxSpill) {
        return "spill must be an integer in [0, " + std::to_string(kSubjectMaxSpill) + "]";
    }
    if (candidate.depthEnabled_ && candidate.depthNear_ > candidate.depthFar_) {
        return "depthNear must not exceed depthFar";
    }
    return {};
}

// Marks @p entity, replacing any configuration it had. Adding the component
// is a structural change: outside a system tick only, or from
// `IREntity.deferredCall` in Lua. Replacing an existing one is not.
inline void setSubject(IREntity::EntityId entity, const IRComponents::C_GlitchSubject &subject) {
    IR_ASSERT(validateSubject(subject).empty(), "IRPrefab::Glitch::setSubject: invalid subject");
    auto existing = IREntity::getComponentOptional<IRComponents::C_GlitchSubject>(entity);
    if (existing.has_value()) {
        *existing.value() = subject;
        return;
    }
    IREntity::setComponent(entity, subject);
}

// Removes the mark; a structural change with the same rule as `setSubject`.
inline void clearSubject(IREntity::EntityId entity) {
    if (IREntity::getComponentOptional<IRComponents::C_GlitchSubject>(entity).has_value()) {
        IREntity::removeComponent<IRComponents::C_GlitchSubject>(entity);
    }
}

inline const IRComponents::C_GlitchSubject *subjectOrNull(IREntity::EntityId entity) {
    auto subject = IREntity::getComponentOptional<IRComponents::C_GlitchSubject>(entity);
    return subject.has_value() ? subject.value() : nullptr;
}

inline IRSystem::System<IRSystem::TRIXEL_GLITCH> *trixelSystemOrNull() {
    return glitchSystemOrNull<IRSystem::TRIXEL_GLITCH>();
}

} // namespace IRPrefab::Glitch

#endif /* IR_PREFAB_GLITCH_SUBJECT_H */
