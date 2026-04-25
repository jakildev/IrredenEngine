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
/// once the counter reaches 0.
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
};

} // namespace IRComponents

#endif /* COMPONENT_MODIFIERS_H */
