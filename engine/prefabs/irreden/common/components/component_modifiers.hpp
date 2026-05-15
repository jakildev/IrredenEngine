#ifndef COMPONENT_MODIFIERS_H
#define COMPONENT_MODIFIERS_H

// Generic modifier framework — data declarations.
// See docs/design/modifiers.md for the locked design choices, the
// resolver-pipeline contract, and the existing-pattern audit.

#include <irreden/entity/ir_entity_types.hpp>
#include <irreden/math/ir_math_types.hpp>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace IRComponents {

using FieldBindingId = std::uint16_t;
inline constexpr FieldBindingId kInvalidFieldId = 0;

/// Typed dispatch for a field-binding id. Set at registration time
/// (`registerField` for scalars, `registerFieldVec3` for vector fields).
/// Push/read paths consult this on the registry to route to the
/// correct internal vector. Pushing a typed param against a field of
/// the wrong type is a no-op (defensive — caller bug, not data error).
enum class FieldValueType : std::uint8_t {
    SCALAR,
    VEC3,
};

enum class TransformKind : std::uint8_t {
    ADD,
    MULTIPLY,
    SET,
    CLAMP_MIN,
    CLAMP_MAX,
    OVERRIDE,
};

/// Dense modulation request on one scalar field. The `static_assert` below
/// enforces trivial-copyability; anything that needs `std::function`
/// or `std::string` belongs in `LambdaModifier`. `ticksRemaining_ == -1`
/// is the sentinel for "no decay"; the decay system drops the modifier
/// once the counter reaches 0. See `docs/design/modifiers.md` §Data shapes
/// for per-field semantics.
struct Modifier {
    FieldBindingId field_;
    TransformKind kind_;
    float param_;
    IREntity::EntityId source_;
    std::int32_t ticksRemaining_;
};

static_assert(
    std::is_trivially_copyable_v<Modifier>,
    "Modifier must remain trivially-copyable; anything needing "
    "std::function/std::string belongs in LambdaModifier."
);

/// vec3 counterpart to `Modifier`. Compose semantics are component-wise:
/// ADD/MULTIPLY/SET apply per-axis in push-order; CLAMP_MIN/CLAMP_MAX
/// bound each axis independently. OVERRIDE replaces the entire vec3 and
/// short-circuits prior ops just like the scalar path.
struct ModifierVec3 {
    FieldBindingId field_;
    TransformKind kind_;
    IRMath::vec3 param_;
    IREntity::EntityId source_;
    std::int32_t ticksRemaining_;
};

static_assert(
    std::is_trivially_copyable_v<ModifierVec3>,
    "ModifierVec3 must remain trivially-copyable; the param_ field stays vec3."
);

struct LambdaModifier {
    FieldBindingId field_;
    std::function<float(float)> fn_;
    IREntity::EntityId source_;
    std::int32_t ticksRemaining_;
};

struct ResolvedField {
    FieldBindingId field_;
    float value_;
};

struct ResolvedFieldVec3 {
    FieldBindingId field_;
    IRMath::vec3 value_;
};

namespace detail {

// Linear scan over the resolved-field vector. v1 fields-per-entity
// counts are small (~5); a hash map would cost more than it saves. If a
// future entity carries dozens of fields, swap this for a sorted-binary
// or small-flat-map lookup — the API stays unchanged.
inline ResolvedField *findResolvedField(std::vector<ResolvedField> &fields, FieldBindingId field) {
    for (auto &rf : fields) {
        if (rf.field_ == field)
            return &rf;
    }
    return nullptr;
}

inline ResolvedFieldVec3 *
findResolvedFieldVec3(std::vector<ResolvedFieldVec3> &fields, FieldBindingId field) {
    for (auto &rf : fields) {
        if (rf.field_ == field)
            return &rf;
    }
    return nullptr;
}

// Shared decay predicate for the three modifier-decay systems
// (`MODIFIER_DECAY`, `GLOBAL_MODIFIER_DECAY`, `LAMBDA_MODIFIER_DECAY`).
// `T` is `Modifier`, `ModifierVec3`, or `LambdaModifier` — all three
// expose `ticksRemaining_` with identical semantics. Mutates
// `mod.ticksRemaining_` in place.
template <typename T> inline bool tickAndExpired(T &mod) {
    if (mod.ticksRemaining_ == -1)
        return false;
    --mod.ticksRemaining_;
    return mod.ticksRemaining_ <= 0;
}

} // namespace detail

struct C_Modifiers {
    std::vector<Modifier> modifiers_;
    std::vector<ModifierVec3> modifiersVec3_;
};

struct C_GlobalModifiers {
    std::vector<Modifier> modifiers_;
    std::vector<ModifierVec3> modifiersVec3_;
};

/// Empty tag opting an entity out of `C_GlobalModifiers`. The
/// resolver's global vs exempt dispatch is **archetype-routed** — two
/// resolver systems with include / exclude filters on this tag —
/// never a runtime branch inside a tick body.
struct C_NoGlobalModifiers {};

struct C_LambdaModifiers {
    std::vector<LambdaModifier> modifiers_;
};

struct C_ResolvedFields {
    std::vector<ResolvedField> fields_;
    std::vector<ResolvedFieldVec3> fieldsVec3_;

    // Insert or update (field, base) so the resolver's compose pass
    // starts from `base`. Consumer systems call this in a pre-resolver
    // tick to seed the field for this frame's compose pass.
    void reset(FieldBindingId field, float base) {
        if (auto *rf = detail::findResolvedField(fields_, field)) {
            rf->value_ = base;
            return;
        }
        fields_.push_back(ResolvedField{field, base});
    }

    void resetVec3(FieldBindingId field, IRMath::vec3 base) {
        if (auto *rf = detail::findResolvedFieldVec3(fieldsVec3_, field)) {
            rf->value_ = base;
            return;
        }
        fieldsVec3_.push_back(ResolvedFieldVec3{field, base});
    }

    // Convenience — apply one structured modifier in place. The
    // resolver pipeline does not use this incrementally (OVERRIDE
    // semantics need a full pass; see modifier_compose.hpp); the helper
    // is here for callers building tests or one-off composes.
    void apply(const Modifier &mod) {
        auto *rf = detail::findResolvedField(fields_, mod.field_);
        if (!rf)
            return;
        switch (mod.kind_) {
        case TransformKind::ADD:
            rf->value_ += mod.param_;
            break;
        case TransformKind::MULTIPLY:
            rf->value_ *= mod.param_;
            break;
        case TransformKind::SET:
            rf->value_ = mod.param_;
            break;
        case TransformKind::OVERRIDE:
            rf->value_ = mod.param_;
            break;
        case TransformKind::CLAMP_MIN:
            if (rf->value_ < mod.param_)
                rf->value_ = mod.param_;
            break;
        case TransformKind::CLAMP_MAX:
            if (rf->value_ > mod.param_)
                rf->value_ = mod.param_;
            break;
        }
    }

    // vec3 counterpart of `apply`. CLAMP_MIN/MAX bound each axis
    // independently; OVERRIDE replaces the whole vec3. Same single-step
    // semantics as the scalar variant — the resolver pipeline uses the
    // pure-function compose in modifier_compose.hpp instead.
    void apply(const ModifierVec3 &mod) {
        auto *rf = detail::findResolvedFieldVec3(fieldsVec3_, mod.field_);
        if (!rf)
            return;
        switch (mod.kind_) {
        case TransformKind::ADD:
            rf->value_ += mod.param_;
            break;
        case TransformKind::MULTIPLY:
            rf->value_ *= mod.param_;
            break;
        case TransformKind::SET:
            rf->value_ = mod.param_;
            break;
        case TransformKind::OVERRIDE:
            rf->value_ = mod.param_;
            break;
        case TransformKind::CLAMP_MIN:
            rf->value_.x = (rf->value_.x < mod.param_.x) ? mod.param_.x : rf->value_.x;
            rf->value_.y = (rf->value_.y < mod.param_.y) ? mod.param_.y : rf->value_.y;
            rf->value_.z = (rf->value_.z < mod.param_.z) ? mod.param_.z : rf->value_.z;
            break;
        case TransformKind::CLAMP_MAX:
            rf->value_.x = (rf->value_.x > mod.param_.x) ? mod.param_.x : rf->value_.x;
            rf->value_.y = (rf->value_.y > mod.param_.y) ? mod.param_.y : rf->value_.y;
            rf->value_.z = (rf->value_.z > mod.param_.z) ? mod.param_.z : rf->value_.z;
            break;
        }
    }

    void applyLambda(const LambdaModifier &lambda) {
        if (!lambda.fn_)
            return;
        auto *rf = detail::findResolvedField(fields_, lambda.field_);
        if (!rf)
            return;
        rf->value_ = lambda.fn_(rf->value_);
    }

    // Read with a fallback when the field hasn't been registered.
    float get(FieldBindingId field, float fallback = 0.0f) const {
        for (const auto &rf : fields_) {
            if (rf.field_ == field)
                return rf.value_;
        }
        return fallback;
    }

    IRMath::vec3 getVec3(FieldBindingId field, IRMath::vec3 fallback = IRMath::vec3(0.0f)) const {
        for (const auto &rf : fieldsVec3_) {
            if (rf.field_ == field)
                return rf.value_;
        }
        return fallback;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_MODIFIERS_H */
