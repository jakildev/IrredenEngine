#ifndef MODIFIER_COMPOSE_H
#define MODIFIER_COMPOSE_H

// Pure-function composition core for the modifier framework.
// Takes a base value, a field id, and zero-to-two modifier vectors
// (logically concatenated, vector A before vector B), returns the
// effective value per the locked evaluation order:
//
//   1. Latest OVERRIDE across the combined sequence wins; everything
//      earlier than it is discarded. OVERRIDE in B trumps OVERRIDE in A.
//   2. ADD / MULTIPLY / SET applied in push-order.
//   3. CLAMP_MIN / CLAMP_MAX applied last across the combined remainder
//      (always after the algebra so they bound the result).
//
// See docs/design/modifiers.md §Resolver evaluation order.

#include <irreden/common/components/component_modifiers.hpp>

#include <algorithm>
#include <cstddef>
#include <vector>

namespace IRPrefab::Modifier::detail {

inline float composeForField(
    float base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::Modifier> &modsA,
    const std::vector<IRComponents::Modifier> &modsB
) {
    using IRComponents::TransformKind;

    int overrideA = -1;
    for (std::size_t i = 0; i < modsA.size(); ++i) {
        if (modsA[i].field_ == field && modsA[i].kind_ == TransformKind::OVERRIDE) {
            overrideA = static_cast<int>(i);
        }
    }
    int overrideB = -1;
    for (std::size_t i = 0; i < modsB.size(); ++i) {
        if (modsB[i].field_ == field && modsB[i].kind_ == TransformKind::OVERRIDE) {
            overrideB = static_cast<int>(i);
        }
    }

    float value = base;
    std::size_t startA = 0, startB = 0;
    if (overrideB >= 0) {
        value = modsB[overrideB].param_;
        startA = modsA.size();
        startB = static_cast<std::size_t>(overrideB) + 1;
    } else if (overrideA >= 0) {
        value = modsA[overrideA].param_;
        startA = static_cast<std::size_t>(overrideA) + 1;
    }

    auto applyAlgebra = [&](const std::vector<IRComponents::Modifier> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field) continue;
            switch (v[i].kind_) {
                case TransformKind::ADD:      value += v[i].param_; break;
                case TransformKind::MULTIPLY: value *= v[i].param_; break;
                case TransformKind::SET:      value  = v[i].param_; break;
                default: break;
            }
        }
    };
    applyAlgebra(modsA, startA);
    applyAlgebra(modsB, startB);

    auto applyClamp = [&](const std::vector<IRComponents::Modifier> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field) continue;
            switch (v[i].kind_) {
                case TransformKind::CLAMP_MIN: value = std::max(value, v[i].param_); break;
                case TransformKind::CLAMP_MAX: value = std::min(value, v[i].param_); break;
                default: break;
            }
        }
    };
    applyClamp(modsA, startA);
    applyClamp(modsB, startB);

    return value;
}

inline float composeForField(
    float base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::Modifier> &mods
) {
    static const std::vector<IRComponents::Modifier> kEmpty;
    return composeForField(base, field, kEmpty, mods);
}

} // namespace IRPrefab::Modifier::detail

#endif /* MODIFIER_COMPOSE_H */
