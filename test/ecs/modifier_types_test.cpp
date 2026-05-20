#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>

#include <type_traits>
#include <vector>

namespace {

using IRComponents::C_GlobalModifiers;
using IRComponents::C_LambdaModifiers;
using IRComponents::C_Modifiers;
using IRComponents::C_NoGlobalModifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::FieldBindingId;
using IRComponents::FieldValueType;
using IRComponents::kInvalidFieldId;
using IRComponents::LambdaModifier;
using IRComponents::Modifier;
using IRComponents::ModifierQuat;
using IRComponents::ModifierVec3;
using IRComponents::ResolvedField;
using IRComponents::ResolvedFieldQuat;
using IRComponents::ResolvedFieldVec3;
using IRComponents::TransformKind;

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
    c.modifiers_.push_back(
        Modifier{
            FieldBindingId{1},
            TransformKind::ADD,
            0.5f,
            IREntity::EntityId{42},
            std::int32_t{-1},
        }
    );

    ASSERT_EQ(c.modifiers_.size(), std::size_t{1});
    EXPECT_EQ(c.modifiers_[0].field_, FieldBindingId{1});
    EXPECT_EQ(c.modifiers_[0].kind_, TransformKind::ADD);
    EXPECT_FLOAT_EQ(c.modifiers_[0].param_, 0.5f);
    EXPECT_EQ(c.modifiers_[0].source_, IREntity::EntityId{42});
    EXPECT_EQ(c.modifiers_[0].ticksRemaining_, std::int32_t{-1});
}

TEST(ModifierTypes, LambdaModifierStoresFunction) {
    C_LambdaModifiers c{};
    c.modifiers_.push_back(
        LambdaModifier{
            FieldBindingId{2},
            [](float base) { return base * 2.0f; },
            IREntity::EntityId{7},
            std::int32_t{60},
        }
    );

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

TEST(ModifierTypes, ModifierVec3IsTriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<ModifierVec3>);
    SUCCEED();
}

TEST(ModifierTypes, ModifierVec3LayoutLocked) {
    static_assert(sizeof(ModifierVec3) == 32);
    static_assert(alignof(ModifierVec3) == 8);
    SUCCEED();
}

TEST(ModifierTypes, FieldValueTypeSize) {
    static_assert(sizeof(FieldValueType) == 1);
    SUCCEED();
}

TEST(ModifierTypes, CResolvedFieldsHoldsBothVectorsIndependently) {
    C_ResolvedFields c{};
    c.fields_.push_back(ResolvedField{FieldBindingId{1}, 1.5f});
    c.fieldsVec3_.push_back(ResolvedFieldVec3{FieldBindingId{2}, IRMath::vec3(1.0f, 2.0f, 3.0f)});

    EXPECT_EQ(c.fields_.size(), std::size_t{1});
    EXPECT_EQ(c.fieldsVec3_.size(), std::size_t{1});
    EXPECT_FLOAT_EQ(c.fields_[0].value_, 1.5f);
    EXPECT_FLOAT_EQ(c.fieldsVec3_[0].value_.y, 2.0f);
}

TEST(ModifierTypes, ModifierQuatIsTriviallyCopyable) {
    static_assert(std::is_trivially_copyable_v<ModifierQuat>);
    SUCCEED();
}

TEST(ModifierTypes, ModifierQuatLayoutLocked) {
    // 2 (field_) + 1 (kind_) + 1 pad + 16 (vec4 param_) + 4 pad for
    // 8-byte EntityId align + 8 (source_) + 4 (ticks_) + 4 tail = 40.
    static_assert(sizeof(ModifierQuat) == 40);
    static_assert(alignof(ModifierQuat) == 8);
    SUCCEED();
}

TEST(ModifierTypes, CResolvedFieldsHoldsAllThreeVectorsIndependently) {
    C_ResolvedFields c{};
    c.fields_.push_back(ResolvedField{FieldBindingId{1}, 1.5f});
    c.fieldsVec3_.push_back(ResolvedFieldVec3{FieldBindingId{2}, IRMath::vec3(1.0f, 2.0f, 3.0f)});
    c.fieldsQuat_.push_back(
        ResolvedFieldQuat{FieldBindingId{3}, IRMath::vec4(0.0f, 0.0f, 0.0f, 1.0f)}
    );

    EXPECT_EQ(c.fields_.size(), std::size_t{1});
    EXPECT_EQ(c.fieldsVec3_.size(), std::size_t{1});
    EXPECT_EQ(c.fieldsQuat_.size(), std::size_t{1});
    EXPECT_FLOAT_EQ(c.fieldsQuat_[0].value_.w, 1.0f);
}

} // namespace
