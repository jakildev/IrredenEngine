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
#include <irreden/ir_math.hpp>

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
            if (v[i].field_ != field)
                continue;
            switch (v[i].kind_) {
            case TransformKind::ADD:
                value += v[i].param_;
                break;
            case TransformKind::MULTIPLY:
                value *= v[i].param_;
                break;
            case TransformKind::SET:
                value = v[i].param_;
                break;
            default:
                break;
            }
        }
    };
    applyAlgebra(modsA, startA);
    applyAlgebra(modsB, startB);

    auto applyClamp = [&](const std::vector<IRComponents::Modifier> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field)
                continue;
            switch (v[i].kind_) {
            case TransformKind::CLAMP_MIN:
                value = IRMath::max(value, v[i].param_);
                break;
            case TransformKind::CLAMP_MAX:
                value = IRMath::min(value, v[i].param_);
                break;
            default:
                break;
            }
        }
    };
    applyClamp(modsA, startA);
    applyClamp(modsB, startB);

    return value;
}

inline const std::vector<IRComponents::Modifier> &emptyModifiers() {
    static const std::vector<IRComponents::Modifier> kEmpty;
    return kEmpty;
}

inline float composeForField(
    float base, IRComponents::FieldBindingId field, const std::vector<IRComponents::Modifier> &mods
) {
    return composeForField(base, field, emptyModifiers(), mods);
}

// vec3 counterpart. Same OVERRIDE-wins / push-order-algebra /
// clamp-last evaluation rule; ops are component-wise via IRMath
// (ADD/MULTIPLY/SET use vec3 operators, CLAMP_MIN/MAX use IRMath::max
// and IRMath::min, which template-dispatch to glm component-wise on
// vec3).
inline IRMath::vec3 composeForFieldVec3(
    IRMath::vec3 base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::ModifierVec3> &modsA,
    const std::vector<IRComponents::ModifierVec3> &modsB
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

    IRMath::vec3 value = base;
    std::size_t startA = 0, startB = 0;
    if (overrideB >= 0) {
        value = modsB[overrideB].param_;
        startA = modsA.size();
        startB = static_cast<std::size_t>(overrideB) + 1;
    } else if (overrideA >= 0) {
        value = modsA[overrideA].param_;
        startA = static_cast<std::size_t>(overrideA) + 1;
    }

    auto applyAlgebra = [&](const std::vector<IRComponents::ModifierVec3> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field)
                continue;
            switch (v[i].kind_) {
            case TransformKind::ADD:
                value += v[i].param_;
                break;
            case TransformKind::MULTIPLY:
                value *= v[i].param_;
                break;
            case TransformKind::SET:
                value = v[i].param_;
                break;
            default:
                break;
            }
        }
    };
    applyAlgebra(modsA, startA);
    applyAlgebra(modsB, startB);

    auto applyClamp = [&](const std::vector<IRComponents::ModifierVec3> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field)
                continue;
            switch (v[i].kind_) {
            case TransformKind::CLAMP_MIN:
                value = IRMath::max(value, v[i].param_);
                break;
            case TransformKind::CLAMP_MAX:
                value = IRMath::min(value, v[i].param_);
                break;
            default:
                break;
            }
        }
    };
    applyClamp(modsA, startA);
    applyClamp(modsB, startB);

    return value;
}

inline const std::vector<IRComponents::ModifierVec3> &emptyModifiersVec3() {
    static const std::vector<IRComponents::ModifierVec3> kEmpty;
    return kEmpty;
}

inline IRMath::vec3 composeForFieldVec3(
    IRMath::vec3 base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::ModifierVec3> &mods
) {
    return composeForFieldVec3(base, field, emptyModifiersVec3(), mods);
}

// Quat counterpart. Same OVERRIDE-wins / push-order-algebra evaluation
// rule as scalar/vec3, with the algebra reduced to MULTIPLY/SET (quat
// rotation compose / replace) — ADD and CLAMP_MIN/MAX are not meaningful
// on unit quaternions and are silently skipped here (the push API
// asserts in debug, so direct-vector construction is the only path
// that reaches this fallthrough).
//
// MULTIPLY is left-multiply (post-rotate): `value = quatMul(mod, value)`
// composes the new rotation on top of the running base. This convention
// matches `engine/math/ir_math.hpp::quatMul`'s comment ("rotates b first
// then a") — `quatMul(parent_world, local)` is the bone-chain idiom.
//
// Final normalization is applied once at the end of the compose pass
// to amortize stacked-MULTIPLY float drift; the per-step apply on
// `C_ResolvedFields` (`apply(const ModifierQuat&)`) does not normalize.
inline IRMath::vec4 composeForFieldQuat(
    IRMath::vec4 base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::ModifierQuat> &modsA,
    const std::vector<IRComponents::ModifierQuat> &modsB
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

    IRMath::vec4 value = base;
    std::size_t startA = 0, startB = 0;
    bool touched = false;
    if (overrideB >= 0) {
        value = modsB[overrideB].param_;
        startA = modsA.size();
        startB = static_cast<std::size_t>(overrideB) + 1;
        touched = true;
    } else if (overrideA >= 0) {
        value = modsA[overrideA].param_;
        startA = static_cast<std::size_t>(overrideA) + 1;
        touched = true;
    }

    auto applyAlgebra = [&](const std::vector<IRComponents::ModifierQuat> &v, std::size_t from) {
        for (std::size_t i = from; i < v.size(); ++i) {
            if (v[i].field_ != field)
                continue;
            switch (v[i].kind_) {
            case TransformKind::MULTIPLY:
                value = IRMath::quatMul(v[i].param_, value);
                touched = true;
                break;
            case TransformKind::SET:
                value = v[i].param_;
                touched = true;
                break;
            default:
                break;
            }
        }
    };
    applyAlgebra(modsA, startA);
    applyAlgebra(modsB, startB);

    // Only normalize when at least one modifier touched the value;
    // identity-only fast path avoids a normalize on every entity with a
    // C_Modifiers and a registered quat field but no active modifier.
    if (touched) {
        value = IRMath::normalize(value);
    }
    return value;
}

inline const std::vector<IRComponents::ModifierQuat> &emptyModifiersQuat() {
    static const std::vector<IRComponents::ModifierQuat> kEmpty;
    return kEmpty;
}

inline IRMath::vec4 composeForFieldQuat(
    IRMath::vec4 base,
    IRComponents::FieldBindingId field,
    const std::vector<IRComponents::ModifierQuat> &mods
) {
    return composeForFieldQuat(base, field, emptyModifiersQuat(), mods);
}

} // namespace IRPrefab::Modifier::detail

#endif /* MODIFIER_COMPOSE_H */
