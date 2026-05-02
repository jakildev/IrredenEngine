#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>

#include <type_traits>
#include <vector>

namespace {

using IRComponents::FieldBindingId;
using IRComponents::kInvalidFieldId;
using IRComponents::TransformKind;
using IRComponents::Modifier;
using IRComponents::LambdaModifier;
using IRComponents::ResolvedField;
using IRComponents::C_Modifiers;
using IRComponents::C_GlobalModifiers;
using IRComponents::C_NoGlobalModifiers;
using IRComponents::C_LambdaModifiers;
using IRComponents::C_ResolvedFields;

TEST(ModifierTypes, ModifierIsTriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<Modifier>);
    SUCCEED();
}

TEST(ModifierTypes, ModifierLayoutLocked) {
    static_assert(sizeof(Modifier) == 24);
    static_assert(alignof(Modifier) == 8);
    SUCCEED();
}

TEST(ModifierTypes, FieldBindingSentinelIsZero) {
    static_assert(kInvalidFieldId == FieldBindingId{0});
    SUCCEED();
}

TEST(ModifierTypes, TransformKindSize) {
    static_assert(sizeof(TransformKind) == 1);
    SUCCEED();
}

TEST(ModifierTypes, ComponentsDefaultConstructEmpty) {
    C_Modifiers per_entity{};
    C_GlobalModifiers global{};
    C_NoGlobalModifiers tag{};
    C_LambdaModifiers lambdas{};
    C_ResolvedFields resolved{};

    EXPECT_TRUE(per_entity.modifiers_.empty());
    EXPECT_TRUE(global.modifiers_.empty());
    EXPECT_TRUE(lambdas.modifiers_.empty());
    EXPECT_TRUE(resolved.fields_.empty());
    (void)tag;
}

TEST(ModifierTypes, ModifierPushAndRead) {
    C_Modifiers c{};
    c.modifiers_.push_back(Modifier{
        FieldBindingId{1},
        TransformKind::ADD,
        0.5f,
        IREntity::EntityId{42},
        std::int32_t{-1},
    });

    ASSERT_EQ(c.modifiers_.size(), std::size_t{1});
    EXPECT_EQ(c.modifiers_[0].field_, FieldBindingId{1});
    EXPECT_EQ(c.modifiers_[0].kind_, TransformKind::ADD);
    EXPECT_FLOAT_EQ(c.modifiers_[0].param_, 0.5f);
    EXPECT_EQ(c.modifiers_[0].source_, IREntity::EntityId{42});
    EXPECT_EQ(c.modifiers_[0].ticksRemaining_, std::int32_t{-1});
}

TEST(ModifierTypes, LambdaModifierStoresFunction) {
    C_LambdaModifiers c{};
    c.modifiers_.push_back(LambdaModifier{
        FieldBindingId{2},
        [](float base) { return base * 2.0f; },
        IREntity::EntityId{7},
        std::int32_t{60},
    });

    ASSERT_EQ(c.modifiers_.size(), std::size_t{1});
    EXPECT_FLOAT_EQ(c.modifiers_[0].fn_(3.0f), 6.0f);
    EXPECT_EQ(c.modifiers_[0].source_, IREntity::EntityId{7});
    EXPECT_EQ(c.modifiers_[0].ticksRemaining_, std::int32_t{60});
}

TEST(ModifierTypes, ResolvedFieldsLookup) {
    C_ResolvedFields c{};
    c.fields_.push_back(ResolvedField{FieldBindingId{1}, 1.5f});
    c.fields_.push_back(ResolvedField{FieldBindingId{2}, 2.5f});

    EXPECT_EQ(c.fields_.size(), std::size_t{2});
    EXPECT_FLOAT_EQ(c.fields_[1].value_, 2.5f);
}

} // namespace
