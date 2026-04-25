#ifndef COMPONENT_MODIFIERS_H
#define COMPONENT_MODIFIERS_H

// Generic modifier framework — data declarations.
// See docs/design/modifiers.md for the locked design choices, the
// resolver-pipeline contract, and the existing-pattern audit.

#include <irreden/entity/ir_entity_types.hpp>

#include <cstdint>
#include <functional>
#include <type_traits>
#include <vector>

namespace IRComponents {

using FieldBindingId = std::uint16_t;
inline constexpr FieldBindingId kInvalidFieldId = 0;

enum class TransformKind : std::uint8_t {
    ADD,
    MULTIPLY,
    SET,
    CLAMP_MIN,
    CLAMP_MAX,
    OVERRIDE,
};

/// Dense modulation request on one field. The `static_assert` below
/// enforces trivial-copyability; anything that needs `std::function`
/// or `std::string` belongs in `LambdaModifier`. `ticksRemaining_ == -1`
/// is the sentinel for "no decay"; the decay system drops the modifier
/// once the counter reaches 0. See `docs/design/modifiers.md` §Data shapes
/// for per-field semantics.
struct Modifier {
    FieldBindingId       field_;
    TransformKind        kind_;
    float                param_;
    IREntity::EntityId   source_;
    std::int32_t         ticksRemaining_;
};

static_assert(
    std::is_trivially_copyable_v<Modifier>,
    "Modifier must remain trivially-copyable; anything needing "
    "std::function/std::string belongs in LambdaModifier."
);

struct LambdaModifier {
    FieldBindingId              field_;
    std::function<float(float)> fn_;
    IREntity::EntityId          source_;
    std::int32_t                ticksRemaining_;
};

struct ResolvedField {
    FieldBindingId field_;
    float          value_;
};

namespace detail {

// Linear scan over the resolved-field vector. v1 fields-per-entity
// counts are small (~5); a hash map would cost more than it saves. If a
// future entity carries dozens of fields, swap this for a sorted-binary
// or small-flat-map lookup — the API stays unchanged.
inline ResolvedField *findResolvedField(std::vector<ResolvedField> &fields,
                                        FieldBindingId field) {
    for (auto &rf : fields) {
        if (rf.field_ == field) return &rf;
    }
    return nullptr;
}

} // namespace detail

struct C_Modifiers {
    std::vector<Modifier> modifiers_;
};

struct C_GlobalModifiers {
    std::vector<Modifier> modifiers_;
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

    // Convenience — apply one structured modifier in place. The
    // resolver pipeline does not use this incrementally (OVERRIDE
    // semantics need a full pass; see modifier_compose.hpp); the helper
    // is here for callers building tests or one-off composes.
    void apply(const Modifier &mod) {
        auto *rf = detail::findResolvedField(fields_, mod.field_);
        if (!rf) return;
        switch (mod.kind_) {
            case TransformKind::ADD:       rf->value_ += mod.param_; break;
            case TransformKind::MULTIPLY:  rf->value_ *= mod.param_; break;
            case TransformKind::SET:       rf->value_  = mod.param_; break;
            case TransformKind::OVERRIDE:  rf->value_  = mod.param_; break;
            case TransformKind::CLAMP_MIN:
                if (rf->value_ < mod.param_) rf->value_ = mod.param_;
                break;
            case TransformKind::CLAMP_MAX:
                if (rf->value_ > mod.param_) rf->value_ = mod.param_;
                break;
        }
    }

    void applyLambda(const LambdaModifier &lambda) {
        if (!lambda.fn_) return;
        auto *rf = detail::findResolvedField(fields_, lambda.field_);
        if (!rf) return;
        rf->value_ = lambda.fn_(rf->value_);
    }

    // Read with a fallback when the field hasn't been registered.
    float get(FieldBindingId field, float fallback = 0.0f) const {
        for (const auto &rf : fields_) {
            if (rf.field_ == field) return rf.value_;
        }
        return fallback;
    }
};

} // namespace IRComponents

#endif /* COMPONENT_MODIFIERS_H */
