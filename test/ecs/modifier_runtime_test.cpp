#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/modifier_field_registry.hpp>

#include <vector>

namespace {

using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::FieldBindingId;
using IRComponents::kInvalidFieldId;
using IRComponents::Modifier;
using IRComponents::ResolvedField;
using IRComponents::TransformKind;

using IRPrefab::Modifier::detail::composeForField;
using IRPrefab::Modifier::detail::FieldRegistry;
using IRPrefab::Modifier::detail::globalFieldRegistry;

// ---- Field registry --------------------------------------------------------

TEST(ModifierRegistry, RegisterAssignsDenseSequentialIds) {
    FieldRegistry registry;

    EXPECT_EQ(registry.fieldCount(), 0u);

    auto idA = registry.registerField("field.a");
    auto idB = registry.registerField("field.b");
    auto idC = registry.registerField("field.c");

    EXPECT_EQ(idA, FieldBindingId{1});
    EXPECT_EQ(idB, FieldBindingId{2});
    EXPECT_EQ(idC, FieldBindingId{3});
    EXPECT_EQ(registry.fieldCount(), 3u);
}

TEST(ModifierRegistry, FieldNameRoundTrips) {
    FieldRegistry registry;
    auto idA = registry.registerField("velocity.x");
    auto idB = registry.registerField("velocity.y");

    EXPECT_STREQ(registry.fieldName(idA), "velocity.x");
    EXPECT_STREQ(registry.fieldName(idB), "velocity.y");
}

TEST(ModifierRegistry, InvalidIdReturnsNullName) {
    FieldRegistry registry;
    registry.registerField("field.a");

    EXPECT_EQ(registry.fieldName(kInvalidFieldId), nullptr);
    // Out-of-range id is also null (not a registered binding).
    EXPECT_EQ(registry.fieldName(FieldBindingId{42}), nullptr);
}

TEST(ModifierRegistry, GlobalSingletonIsStable) {
    auto &r1 = globalFieldRegistry();
    auto &r2 = globalFieldRegistry();
    EXPECT_EQ(&r1, &r2);
}

// ---- Compose: structured transforms ---------------------------------------

namespace {

constexpr FieldBindingId kFieldA = FieldBindingId{1};
constexpr FieldBindingId kFieldB = FieldBindingId{2};

Modifier mk(FieldBindingId field, TransformKind kind, float param) {
    return Modifier{field, kind, param, IREntity::EntityId{0}, -1};
}

} // namespace

TEST(ModifierCompose, NoModifiersReturnsBase) {
    std::vector<Modifier> mods;
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 10.0f);
}

TEST(ModifierCompose, AddOnly) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 2.0f),
        mk(kFieldA, TransformKind::ADD, 3.0f),
    };
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 15.0f);
}

TEST(ModifierCompose, MultiplyOnly) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::MULTIPLY, 0.5f),
        mk(kFieldA, TransformKind::MULTIPLY, 0.5f),
    };
    EXPECT_FLOAT_EQ(composeForField(8.0f, kFieldA, mods), 2.0f);
}

TEST(ModifierCompose, AddThenMultiplyAppliesInPushOrder) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 5.0f),
        mk(kFieldA, TransformKind::MULTIPLY, 2.0f),
    };
    // (10 + 5) * 2 = 30
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 30.0f);
}

TEST(ModifierCompose, MultiplyThenAddAppliesInPushOrder) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::MULTIPLY, 2.0f),
        mk(kFieldA, TransformKind::ADD, 5.0f),
    };
    // 10 * 2 + 5 = 25
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 25.0f);
}

TEST(ModifierCompose, SetReplacesValue) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 5.0f),
        mk(kFieldA, TransformKind::SET, 100.0f),
        mk(kFieldA, TransformKind::ADD, 1.0f),
    };
    // (10 + 5) → SET 100 → 100 + 1 = 101
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 101.0f);
}

TEST(ModifierCompose, ClampMinAlwaysAfterAlgebra) {
    std::vector<Modifier> mods{
        // Clamp pushed BEFORE the multiply, but the resolver applies
        // clamps after all algebra.
        mk(kFieldA, TransformKind::CLAMP_MIN, 5.0f),
        mk(kFieldA, TransformKind::MULTIPLY, 0.0f),
    };
    // Value goes to 0 from MULTIPLY; CLAMP_MIN 5 then bounds it to 5.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 5.0f);
}

TEST(ModifierCompose, ClampMaxAlwaysAfterAlgebra) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::CLAMP_MAX, 50.0f),
        mk(kFieldA, TransformKind::MULTIPLY, 100.0f),
    };
    // 10 * 100 = 1000; clamp_max 50 → 50.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 50.0f);
}

TEST(ModifierCompose, OverrideShortCircuitsPriorModifiers) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 5.0f),
        mk(kFieldA, TransformKind::MULTIPLY, 10.0f),
        mk(kFieldA, TransformKind::OVERRIDE, 7.0f),
    };
    // OVERRIDE discards everything earlier; nothing follows it. Value = 7.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 7.0f);
}

TEST(ModifierCompose, OverrideLetsLaterModifiersApply) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 100.0f),
        mk(kFieldA, TransformKind::OVERRIDE, 5.0f),
        mk(kFieldA, TransformKind::ADD, 2.0f),
        mk(kFieldA, TransformKind::CLAMP_MAX, 6.0f),
    };
    // OVERRIDE → 5; ADD 2 → 7; CLAMP_MAX 6 → 6.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 6.0f);
}

TEST(ModifierCompose, LatestOverrideWins) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::OVERRIDE, 1.0f),
        mk(kFieldA, TransformKind::ADD, 99.0f), // discarded by override #2
        mk(kFieldA, TransformKind::OVERRIDE, 2.0f),
    };
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 2.0f);
}

TEST(ModifierCompose, OtherFieldsDoNotInfluenceTarget) {
    std::vector<Modifier> mods{
        mk(kFieldA, TransformKind::ADD, 5.0f),
        mk(kFieldB, TransformKind::ADD, 1000.0f),
        mk(kFieldA, TransformKind::MULTIPLY, 2.0f),
    };
    // Only kFieldA's modifiers apply: (10 + 5) * 2 = 30.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, mods), 30.0f);
}

// ---- Compose: globals + entity layering ----------------------------------

TEST(ModifierComposeGlobals, GlobalsApplyBeforeEntity) {
    std::vector<Modifier> globals{
        mk(kFieldA, TransformKind::ADD, 5.0f),
    };
    std::vector<Modifier> entity{
        mk(kFieldA, TransformKind::MULTIPLY, 2.0f),
    };
    // Globals first: 10 + 5 = 15. Then entity: 15 * 2 = 30.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, globals, entity), 30.0f);
}

TEST(ModifierComposeGlobals, EntityOverrideTrumpsGlobalAlgebra) {
    std::vector<Modifier> globals{
        mk(kFieldA, TransformKind::ADD, 100.0f),
    };
    std::vector<Modifier> entity{
        mk(kFieldA, TransformKind::OVERRIDE, 1.0f),
    };
    // Entity OVERRIDE discards globals' contribution.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, globals, entity), 1.0f);
}

TEST(ModifierComposeGlobals, GlobalOverrideStillApplied) {
    std::vector<Modifier> globals{
        mk(kFieldA, TransformKind::OVERRIDE, 7.0f),
    };
    std::vector<Modifier> entity;
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, globals, entity), 7.0f);
}

TEST(ModifierComposeGlobals, ClampSpansBothVectors) {
    std::vector<Modifier> globals{
        mk(kFieldA, TransformKind::CLAMP_MAX, 20.0f),
    };
    std::vector<Modifier> entity{
        mk(kFieldA, TransformKind::ADD, 100.0f),
    };
    // 10 + 100 = 110; clamp_max 20 (from globals, but applied last) → 20.
    EXPECT_FLOAT_EQ(composeForField(10.0f, kFieldA, globals, entity), 20.0f);
}

// ---- C_ResolvedFields helpers --------------------------------------------

TEST(CResolvedFields, ResetInsertsAndUpdates) {
    C_ResolvedFields rf;
    rf.reset(kFieldA, 1.0f);
    ASSERT_EQ(rf.fields_.size(), 1u);
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 1.0f);

    rf.reset(kFieldA, 7.0f); // overwrite, no duplicate insert
    ASSERT_EQ(rf.fields_.size(), 1u);
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 7.0f);

    rf.reset(kFieldB, 3.0f); // new field appends
    ASSERT_EQ(rf.fields_.size(), 2u);
    EXPECT_FLOAT_EQ(rf.get(kFieldB), 3.0f);
}

TEST(CResolvedFields, ApplyMutatesValue) {
    C_ResolvedFields rf;
    rf.reset(kFieldA, 10.0f);
    rf.apply(mk(kFieldA, TransformKind::ADD, 5.0f));
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 15.0f);

    rf.apply(mk(kFieldA, TransformKind::MULTIPLY, 2.0f));
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 30.0f);

    rf.apply(mk(kFieldA, TransformKind::CLAMP_MAX, 25.0f));
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 25.0f);
}

TEST(CResolvedFields, ApplyLambdaUsesCurrentValue) {
    C_ResolvedFields rf;
    rf.reset(kFieldA, 4.0f);
    IRComponents::LambdaModifier lambda{
        kFieldA,
        [](float v) { return v * v; },
        IREntity::EntityId{0},
        -1
    };
    rf.applyLambda(lambda);
    EXPECT_FLOAT_EQ(rf.get(kFieldA), 16.0f);
}

TEST(CResolvedFields, GetFallbackForUnknownField) {
    C_ResolvedFields rf;
    rf.reset(kFieldA, 1.0f);
    EXPECT_FLOAT_EQ(rf.get(kFieldB, -1.0f), -1.0f);
}

// ---- Decay semantics: in-place via std::remove_if -----------------------

TEST(ModifierDecay, MinusOneSentinelIsKeptForever) {
    // Mirror what system_modifier_decay does: decrement-and-prune.
    std::vector<Modifier> mods{
        Modifier{kFieldA, TransformKind::ADD, 1.0f, IREntity::EntityId{0}, -1},
    };
    auto end = std::remove_if(mods.begin(), mods.end(), [](Modifier &m) {
        if (m.ticksRemaining_ == -1) return false;
        --m.ticksRemaining_;
        return m.ticksRemaining_ <= 0;
    });
    mods.erase(end, mods.end());
    EXPECT_EQ(mods.size(), 1u);
}

TEST(ModifierDecay, ExactExpiryDropsAtZero) {
    std::vector<Modifier> mods{
        Modifier{kFieldA, TransformKind::ADD, 1.0f, IREntity::EntityId{0}, 1},
    };
    auto decayOnce = [&]() {
        auto end = std::remove_if(mods.begin(), mods.end(), [](Modifier &m) {
            if (m.ticksRemaining_ == -1) return false;
            --m.ticksRemaining_;
            return m.ticksRemaining_ <= 0;
        });
        mods.erase(end, mods.end());
    };
    decayOnce();
    EXPECT_EQ(mods.size(), 0u);
}

TEST(ModifierDecay, SixtyTickModifierExpiresAtSixty) {
    std::vector<Modifier> mods{
        Modifier{kFieldA, TransformKind::ADD, 1.0f, IREntity::EntityId{0}, 60},
    };
    auto decayOnce = [&]() {
        auto end = std::remove_if(mods.begin(), mods.end(), [](Modifier &m) {
            if (m.ticksRemaining_ == -1) return false;
            --m.ticksRemaining_;
            return m.ticksRemaining_ <= 0;
        });
        mods.erase(end, mods.end());
    };
    for (int i = 0; i < 59; ++i) {
        decayOnce();
        EXPECT_EQ(mods.size(), 1u) << "premature drop at tick " << i;
    }
    decayOnce(); // 60th tick decrements to 0 → drop
    EXPECT_EQ(mods.size(), 0u);
}

TEST(ModifierDecay, SourceRemovalKeepsOnlyMatchingSourceOut) {
    // Mirror the removeBySource sweep on a single vector.
    std::vector<Modifier> mods{
        Modifier{kFieldA, TransformKind::ADD, 1.0f, IREntity::EntityId{1}, -1},
        Modifier{kFieldA, TransformKind::ADD, 2.0f, IREntity::EntityId{2}, -1},
        Modifier{kFieldA, TransformKind::ADD, 3.0f, IREntity::EntityId{1}, -1},
    };
    auto target = IREntity::EntityId{1};
    mods.erase(
        std::remove_if(mods.begin(), mods.end(), [&](const Modifier &m) {
            return m.source_ == target;
        }),
        mods.end()
    );
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_EQ(mods[0].source_, IREntity::EntityId{2});
    EXPECT_FLOAT_EQ(mods[0].param_, 2.0f);
}

} // namespace
