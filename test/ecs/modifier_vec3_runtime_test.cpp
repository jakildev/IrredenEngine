#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/ir_entity.hpp>

#include <stdexcept>
#include <vector>

namespace {

using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::FieldBindingId;
using IRComponents::FieldValueType;
using IRComponents::kInvalidFieldId;
using IRComponents::ModifierVec3;
using IRComponents::ResolvedFieldVec3;
using IRComponents::TransformKind;

using IRPrefab::Modifier::detail::composeForFieldVec3;
using IRPrefab::Modifier::detail::FieldRegistry;

// ---- Field registry: typed binding -----------------------------------------

TEST(ModifierRegistryVec3, RegisterFieldVec3AssignsType) {
    FieldRegistry registry;
    auto scalarId = registry.registerField("scalar.field");
    auto vec3Id = registry.registerFieldVec3("vec3.field");

    EXPECT_EQ(registry.fieldType(scalarId), FieldValueType::SCALAR);
    EXPECT_EQ(registry.fieldType(vec3Id), FieldValueType::VEC3);
    EXPECT_EQ(registry.fieldCount(), 2u);
}

TEST(ModifierRegistryVec3, FieldTypeForInvalidIdIsScalar) {
    FieldRegistry registry;
    EXPECT_EQ(registry.fieldType(kInvalidFieldId), FieldValueType::SCALAR);
    EXPECT_EQ(registry.fieldType(FieldBindingId{42}), FieldValueType::SCALAR);
}

// ---- Compose: structured vec3 transforms ----------------------------------

namespace {

constexpr FieldBindingId kFieldA = FieldBindingId{1};
constexpr FieldBindingId kFieldB = FieldBindingId{2};

ModifierVec3 mkV(FieldBindingId field, TransformKind kind, IRMath::vec3 param) {
    return ModifierVec3{field, kind, param, IREntity::EntityId{0}, -1};
}

} // namespace

TEST(ModifierComposeVec3, NoModifiersReturnsBase) {
    std::vector<ModifierVec3> mods;
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(1.0f, 2.0f, 3.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(ModifierComposeVec3, AddStacksComponentWise) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(1.0f, 0.0f, 0.0f)),
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(0.0f, 2.0f, 0.0f)),
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(0.0f, 0.0f, 3.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 11.0f);
    EXPECT_FLOAT_EQ(result.y, 12.0f);
    EXPECT_FLOAT_EQ(result.z, 13.0f);
}

TEST(ModifierComposeVec3, MultiplyScalesPerAxis) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(2.0f, 0.5f, 1.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f, 10.0f, 10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 20.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 10.0f);
}

TEST(ModifierComposeVec3, AddThenMultiplyAppliesInPushOrder) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f, 5.0f, 5.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(2.0f, 2.0f, 2.0f)),
    };
    // (10 + 5) * 2 = 30 per axis
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 30.0f);
    EXPECT_FLOAT_EQ(result.y, 30.0f);
    EXPECT_FLOAT_EQ(result.z, 30.0f);
}

TEST(ModifierComposeVec3, SetReplacesEntireVector) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f)),
        mkV(kFieldA, TransformKind::SET, IRMath::vec3(100.0f, 200.0f, 300.0f)),
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(1.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 101.0f);
    EXPECT_FLOAT_EQ(result.y, 201.0f);
    EXPECT_FLOAT_EQ(result.z, 301.0f);
}

TEST(ModifierComposeVec3, ClampMinPerAxis) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::CLAMP_MIN, IRMath::vec3(0.0f, 5.0f, -10.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(0.0f, 0.0f, 0.0f)),
    };
    // All axes go to 0; clamp_min bounds each axis to its own minimum.
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 0.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 0.0f);
}

TEST(ModifierComposeVec3, ClampMaxPerAxis) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::CLAMP_MAX, IRMath::vec3(50.0f, 10.0f, 5.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(100.0f)),
    };
    // 10 * 100 = 1000 per axis; clamp_max bounds each axis independently.
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 50.0f);
    EXPECT_FLOAT_EQ(result.y, 10.0f);
    EXPECT_FLOAT_EQ(result.z, 5.0f);
}

TEST(ModifierComposeVec3, ClampAppliedAfterAlgebraIrrespectiveOfPushOrder) {
    std::vector<ModifierVec3> mods{
        // Clamp pushed BEFORE the multiply; resolver still applies clamp last.
        mkV(kFieldA, TransformKind::CLAMP_MIN, IRMath::vec3(5.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(0.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 5.0f);
    EXPECT_FLOAT_EQ(result.y, 5.0f);
    EXPECT_FLOAT_EQ(result.z, 5.0f);
}

TEST(ModifierComposeVec3, OverrideShortCircuitsPriorModifiers) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(10.0f)),
        mkV(kFieldA, TransformKind::OVERRIDE, IRMath::vec3(7.0f, 8.0f, 9.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(100.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 7.0f);
    EXPECT_FLOAT_EQ(result.y, 8.0f);
    EXPECT_FLOAT_EQ(result.z, 9.0f);
}

TEST(ModifierComposeVec3, OverrideLetsLaterModifiersApply) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(100.0f)),
        mkV(kFieldA, TransformKind::OVERRIDE, IRMath::vec3(5.0f, 5.0f, 5.0f)),
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(2.0f)),
        mkV(kFieldA, TransformKind::CLAMP_MAX, IRMath::vec3(6.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    // OVERRIDE → 5, ADD 2 → 7, CLAMP_MAX 6 → 6 (per axis).
    EXPECT_FLOAT_EQ(result.x, 6.0f);
    EXPECT_FLOAT_EQ(result.y, 6.0f);
    EXPECT_FLOAT_EQ(result.z, 6.0f);
}

TEST(ModifierComposeVec3, LatestOverrideWins) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::OVERRIDE, IRMath::vec3(1.0f)),
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(99.0f)),
        mkV(kFieldA, TransformKind::OVERRIDE, IRMath::vec3(2.0f, 3.0f, 4.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 2.0f);
    EXPECT_FLOAT_EQ(result.y, 3.0f);
    EXPECT_FLOAT_EQ(result.z, 4.0f);
}

TEST(ModifierComposeVec3, OtherFieldsDoNotInfluenceTarget) {
    std::vector<ModifierVec3> mods{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f)),
        mkV(kFieldB, TransformKind::ADD, IRMath::vec3(1000.0f)),
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(2.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 30.0f);
    EXPECT_FLOAT_EQ(result.y, 30.0f);
    EXPECT_FLOAT_EQ(result.z, 30.0f);
}

// ---- Compose: globals + entity layering -----------------------------------

TEST(ModifierComposeVec3Globals, GlobalsApplyBeforeEntity) {
    std::vector<ModifierVec3> globals{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f)),
    };
    std::vector<ModifierVec3> entity{
        mkV(kFieldA, TransformKind::MULTIPLY, IRMath::vec3(2.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, globals, entity);
    EXPECT_FLOAT_EQ(result.x, 30.0f);
    EXPECT_FLOAT_EQ(result.y, 30.0f);
    EXPECT_FLOAT_EQ(result.z, 30.0f);
}

TEST(ModifierComposeVec3Globals, EntityOverrideTrumpsGlobalAlgebra) {
    std::vector<ModifierVec3> globals{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(100.0f)),
    };
    std::vector<ModifierVec3> entity{
        mkV(kFieldA, TransformKind::OVERRIDE, IRMath::vec3(1.0f, 2.0f, 3.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, globals, entity);
    EXPECT_FLOAT_EQ(result.x, 1.0f);
    EXPECT_FLOAT_EQ(result.y, 2.0f);
    EXPECT_FLOAT_EQ(result.z, 3.0f);
}

TEST(ModifierComposeVec3Globals, ClampSpansBothVectors) {
    std::vector<ModifierVec3> globals{
        mkV(kFieldA, TransformKind::CLAMP_MAX, IRMath::vec3(20.0f)),
    };
    std::vector<ModifierVec3> entity{
        mkV(kFieldA, TransformKind::ADD, IRMath::vec3(100.0f)),
    };
    IRMath::vec3 result = composeForFieldVec3(IRMath::vec3(10.0f), kFieldA, globals, entity);
    EXPECT_FLOAT_EQ(result.x, 20.0f);
    EXPECT_FLOAT_EQ(result.y, 20.0f);
    EXPECT_FLOAT_EQ(result.z, 20.0f);
}

// ---- C_ResolvedFields helpers ---------------------------------------------

TEST(CResolvedFieldsVec3, ResetInsertsAndUpdates) {
    C_ResolvedFields rf;
    rf.resetVec3(kFieldA, IRMath::vec3(1.0f, 2.0f, 3.0f));
    ASSERT_EQ(rf.fieldsVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(rf.getVec3(kFieldA).y, 2.0f);

    rf.resetVec3(kFieldA, IRMath::vec3(7.0f));
    ASSERT_EQ(rf.fieldsVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(rf.getVec3(kFieldA).x, 7.0f);

    rf.resetVec3(kFieldB, IRMath::vec3(3.0f));
    ASSERT_EQ(rf.fieldsVec3_.size(), 2u);
    EXPECT_FLOAT_EQ(rf.getVec3(kFieldB).z, 3.0f);
}

TEST(CResolvedFieldsVec3, ApplyMutatesValuePerAxis) {
    C_ResolvedFields rf;
    rf.resetVec3(kFieldA, IRMath::vec3(10.0f));
    rf.apply(mkV(kFieldA, TransformKind::ADD, IRMath::vec3(5.0f, 0.0f, -5.0f)));
    auto v = rf.getVec3(kFieldA);
    EXPECT_FLOAT_EQ(v.x, 15.0f);
    EXPECT_FLOAT_EQ(v.y, 10.0f);
    EXPECT_FLOAT_EQ(v.z, 5.0f);

    rf.apply(mkV(kFieldA, TransformKind::CLAMP_MIN, IRMath::vec3(8.0f)));
    v = rf.getVec3(kFieldA);
    EXPECT_FLOAT_EQ(v.x, 15.0f);
    EXPECT_FLOAT_EQ(v.y, 10.0f);
    EXPECT_FLOAT_EQ(v.z, 8.0f);
}

TEST(CResolvedFieldsVec3, GetFallbackForUnknownField) {
    C_ResolvedFields rf;
    rf.resetVec3(kFieldA, IRMath::vec3(1.0f));
    auto fallback = rf.getVec3(kFieldB, IRMath::vec3(-1.0f, -2.0f, -3.0f));
    EXPECT_FLOAT_EQ(fallback.y, -2.0f);
}

TEST(CResolvedFieldsVec3, ScalarAndVec3PathsAreIndependent) {
    // Both vectors live on the same C_ResolvedFields; resetVec3 must
    // not stomp the scalar field with the same id, and vice versa.
    C_ResolvedFields rf;
    rf.reset(kFieldA, 1.5f);
    rf.resetVec3(kFieldA, IRMath::vec3(10.0f, 20.0f, 30.0f));

    EXPECT_FLOAT_EQ(rf.get(kFieldA), 1.5f);
    EXPECT_FLOAT_EQ(rf.getVec3(kFieldA).y, 20.0f);
}

// ---- Decay semantics: in-place via std::remove_if -----------------------

namespace {

void decayVec3Once(std::vector<ModifierVec3> &v) {
    auto end = std::remove_if(
        v.begin(), v.end(), IRComponents::detail::tickAndExpired<ModifierVec3>
    );
    v.erase(end, v.end());
}

} // namespace

TEST(ModifierVec3Decay, MinusOneSentinelIsKeptForever) {
    std::vector<ModifierVec3> mods{
        ModifierVec3{kFieldA, TransformKind::ADD, IRMath::vec3(1.0f), IREntity::EntityId{0}, -1},
    };
    decayVec3Once(mods);
    EXPECT_EQ(mods.size(), 1u);
}

TEST(ModifierVec3Decay, ExactExpiryDropsAtZero) {
    std::vector<ModifierVec3> mods{
        ModifierVec3{kFieldA, TransformKind::ADD, IRMath::vec3(1.0f), IREntity::EntityId{0}, 1},
    };
    decayVec3Once(mods);
    EXPECT_EQ(mods.size(), 0u);
}

TEST(ModifierVec3Decay, SixtyTickModifierExpiresAtSixty) {
    std::vector<ModifierVec3> mods{
        ModifierVec3{kFieldA, TransformKind::ADD, IRMath::vec3(1.0f), IREntity::EntityId{0}, 60},
    };
    for (int i = 0; i < 59; ++i) {
        decayVec3Once(mods);
        ASSERT_EQ(mods.size(), 1u) << "premature drop at tick " << i;
    }
    decayVec3Once(mods);
    EXPECT_EQ(mods.size(), 0u);
}

// ---- Direct-reference overloads (C_Modifiers&) ----------------------------

TEST(ModifierDirectRef, PushFrameLocalVec3DepositsInVec3VectorWithTicks1) {
    IRComponents::C_Modifiers mods;
    IRPrefab::Modifier::pushFrameLocal(
        mods,
        kFieldA,
        TransformKind::ADD,
        IRMath::vec3(1.0f, 2.0f, 3.0f),
        IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiers_.size(), 0u);
    ASSERT_EQ(mods.modifiersVec3_.size(), 1u);
    EXPECT_EQ(mods.modifiersVec3_[0].field_, kFieldA);
    EXPECT_EQ(mods.modifiersVec3_[0].kind_, TransformKind::ADD);
    EXPECT_FLOAT_EQ(mods.modifiersVec3_[0].param_.x, 1.0f);
    EXPECT_FLOAT_EQ(mods.modifiersVec3_[0].param_.y, 2.0f);
    EXPECT_FLOAT_EQ(mods.modifiersVec3_[0].param_.z, 3.0f);
    EXPECT_EQ(mods.modifiersVec3_[0].ticksRemaining_, 1);
}

TEST(ModifierDirectRef, PushFrameLocalScalarDepositsInScalarVectorWithTicks1) {
    IRComponents::C_Modifiers mods;
    IRPrefab::Modifier::pushFrameLocal(
        mods, kFieldA, TransformKind::MULTIPLY, 2.5f, IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiersVec3_.size(), 0u);
    ASSERT_EQ(mods.modifiers_.size(), 1u);
    EXPECT_EQ(mods.modifiers_[0].field_, kFieldA);
    EXPECT_FLOAT_EQ(mods.modifiers_[0].param_, 2.5f);
    EXPECT_EQ(mods.modifiers_[0].ticksRemaining_, 1);
}

TEST(ModifierDirectRef, PushOneFrameVec3DepositsWithTicks2) {
    IRComponents::C_Modifiers mods;
    IRPrefab::Modifier::pushOneFrame(
        mods, kFieldA, TransformKind::ADD, IRMath::vec3(4.0f), IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiersVec3_.size(), 1u);
    EXPECT_EQ(mods.modifiersVec3_[0].ticksRemaining_, 2);
}

TEST(ModifierDirectRef, PushFrameLocalVec3DoesNotTriggerEntityLookup) {
    // Verify the direct-reference overload compiles and runs without an entity
    // or an entity manager in scope — confirming no getComponentOptional path
    // is taken.
    IRComponents::C_Modifiers mods;
    IRPrefab::Modifier::pushFrameLocal(
        mods, kFieldA, TransformKind::ADD, IRMath::vec3(0.0f), IREntity::kNullEntity
    );
    EXPECT_EQ(mods.modifiersVec3_.size(), 1u);
}

TEST(ModifierDirectRef, PushOneFrameScalarDepositsInScalarVectorWithTicks2) {
    IRComponents::C_Modifiers mods;
    IRPrefab::Modifier::pushOneFrame(
        mods, kFieldA, TransformKind::MULTIPLY, 3.0f, IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiersVec3_.size(), 0u);
    ASSERT_EQ(mods.modifiers_.size(), 1u);
    EXPECT_EQ(mods.modifiers_[0].field_, kFieldA);
    EXPECT_FLOAT_EQ(mods.modifiers_[0].param_, 3.0f);
    EXPECT_EQ(mods.modifiers_[0].ticksRemaining_, 2);
}

TEST(ModifierDirectRef, PushFrameLocalQuatDepositsInQuatVectorWithTicks1) {
    IRComponents::C_Modifiers mods;
    IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);
    IRPrefab::Modifier::pushFrameLocal(
        mods, kFieldA, TransformKind::MULTIPLY, identity, IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiers_.size(), 0u);
    ASSERT_EQ(mods.modifiersVec3_.size(), 0u);
    ASSERT_EQ(mods.modifiersQuat_.size(), 1u);
    EXPECT_EQ(mods.modifiersQuat_[0].field_, kFieldA);
    EXPECT_EQ(mods.modifiersQuat_[0].kind_, TransformKind::MULTIPLY);
    EXPECT_EQ(mods.modifiersQuat_[0].ticksRemaining_, 1);
}

TEST(ModifierDirectRef, PushOneFrameQuatDepositsInQuatVectorWithTicks2) {
    IRComponents::C_Modifiers mods;
    IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);
    IRPrefab::Modifier::pushOneFrame(
        mods, kFieldA, TransformKind::MULTIPLY, identity, IREntity::kNullEntity
    );

    ASSERT_EQ(mods.modifiersQuat_.size(), 1u);
    EXPECT_EQ(mods.modifiersQuat_[0].field_, kFieldA);
    EXPECT_EQ(mods.modifiersQuat_[0].kind_, TransformKind::MULTIPLY);
    EXPECT_EQ(mods.modifiersQuat_[0].ticksRemaining_, 2);
}

TEST(ModifierDirectRef, PushFrameLocalQuatAddKindFiresAssertAndNoOps) {
    IRComponents::C_Modifiers mods;
    IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_THROW(
        IRPrefab::Modifier::pushFrameLocal(
            mods, kFieldA, TransformKind::ADD, identity, IREntity::kNullEntity),
        std::runtime_error
    );
    EXPECT_EQ(mods.modifiersQuat_.size(), 0u);
}

TEST(ModifierDirectRef, PushOneFrameQuatClampMinKindFiresAssertAndNoOps) {
    IRComponents::C_Modifiers mods;
    IRMath::vec4 identity(0.0f, 0.0f, 0.0f, 1.0f);
    EXPECT_THROW(
        IRPrefab::Modifier::pushOneFrame(
            mods, kFieldA, TransformKind::CLAMP_MIN, identity, IREntity::kNullEntity),
        std::runtime_error
    );
    EXPECT_EQ(mods.modifiersQuat_.size(), 0u);
}

// ---- Push API: type-mismatch dispatch ------------------------------------

class IRModifierVec3PushTest : public testing::Test {
  protected:
    IRModifierVec3PushTest()
        : m_entity_manager{} {}

    IREntity::EntityManager m_entity_manager;
};

TEST_F(IRModifierVec3PushTest, ScalarPushAgainstVec3FieldNoOps) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto vec3FieldId = IRPrefab::Modifier::registerFieldVec3("test.vec3_only");

    IRPrefab::Modifier::push(target, vec3FieldId, TransformKind::ADD, 1.0f, IREntity::kNullEntity);

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
}

TEST_F(IRModifierVec3PushTest, Vec3PushAgainstScalarFieldNoOps) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto scalarFieldId = IRPrefab::Modifier::registerField("test.scalar_only");

    IRPrefab::Modifier::push(
        target,
        scalarFieldId,
        TransformKind::ADD,
        IRMath::vec3(1.0f),
        IREntity::kNullEntity
    );

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
}

TEST_F(IRModifierVec3PushTest, Vec3PushAgainstVec3FieldLandsInVec3Vector) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto vec3FieldId = IRPrefab::Modifier::registerFieldVec3("test.bob_offset");

    IRPrefab::Modifier::push(
        target,
        vec3FieldId,
        TransformKind::ADD,
        IRMath::vec3(0.5f, 1.0f, 1.5f),
        IREntity::kNullEntity
    );

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    ASSERT_EQ(c.modifiersVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(c.modifiersVec3_[0].param_.y, 1.0f);
}

TEST_F(IRModifierVec3PushTest, ApplyToFieldVec3ComposesDirectly) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto field = IRPrefab::Modifier::registerFieldVec3("test.apply_query");
    IRPrefab::Modifier::push(
        target, field, TransformKind::ADD, IRMath::vec3(2.0f, 4.0f, 6.0f), IREntity::kNullEntity
    );

    auto result = IRPrefab::Modifier::applyToFieldVec3(target, field, IRMath::vec3(10.0f));
    EXPECT_FLOAT_EQ(result.x, 12.0f);
    EXPECT_FLOAT_EQ(result.y, 14.0f);
    EXPECT_FLOAT_EQ(result.z, 16.0f);
}

// ---- Auto-sweep on entity destruction (vec3 vectors) ---------------------

class IRModifierVec3AutoSweepTest : public testing::Test {
  protected:
    IRModifierVec3AutoSweepTest()
        : m_entity_manager{} {
        m_hook_id = IREntity::getEntityManager().registerPreDestroyHook(
            [](IREntity::EntityId destroyed) {
                IRPrefab::Modifier::removeBySource(destroyed);
            }
        );
    }

    ~IRModifierVec3AutoSweepTest() override {
        IREntity::getEntityManager().unregisterPreDestroyHook(m_hook_id);
    }

    IREntity::EntityManager m_entity_manager;
    IREntity::PreDestroyHookId m_hook_id{IREntity::kInvalidPreDestroyHookId};
};

TEST_F(IRModifierVec3AutoSweepTest, DestroyingSourceStripsVec3ModifiersFromTarget) {
    auto field = IRPrefab::Modifier::registerFieldVec3("test.sweep");
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::push(
        target, field, TransformKind::ADD, IRMath::vec3(1.0f, 2.0f, 3.0f), source
    );
    auto &before = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(before.size(), 1u);

    m_entity_manager.destroyEntity(source);

    auto &after = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    EXPECT_EQ(after.size(), 0u);
}

TEST_F(IRModifierVec3AutoSweepTest, DestroyingSourceLeavesOtherSourceVec3ModifiersAlone) {
    auto field = IRPrefab::Modifier::registerFieldVec3("test.sweep_partial");
    auto sourceA = IREntity::createEntity();
    auto sourceB = IREntity::createEntity();
    auto target  = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::push(target, field, TransformKind::ADD, IRMath::vec3(1.0f), sourceA);
    IRPrefab::Modifier::push(target, field, TransformKind::ADD, IRMath::vec3(7.0f), sourceB);

    m_entity_manager.destroyEntity(sourceA);

    auto &after = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].source_, sourceB);
    EXPECT_FLOAT_EQ(after[0].param_.x, 7.0f);
}

// ---- upsertBySource: vec3 ---------------------------------------------------

class IRModifierVec3UpsertTest : public testing::Test {
  protected:
    IRModifierVec3UpsertTest()
        : m_entity_manager{} {
        m_field = IRPrefab::Modifier::registerFieldVec3("test.upsert_vec3");
    }

    IREntity::EntityManager m_entity_manager;
    IRComponents::FieldBindingId m_field{IRComponents::kInvalidFieldId};
};

TEST_F(IRModifierVec3UpsertTest, FirstCallAppends) {
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(1.0f, 2.0f, 3.0f), source
    );

    auto &mods = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_FLOAT_EQ(mods[0].param_.y, 2.0f);
    EXPECT_EQ(mods[0].ticksRemaining_, -1);
}

TEST_F(IRModifierVec3UpsertTest, SecondCallOverwrites) {
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(1.0f), source
    );
    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(9.0f, 8.0f, 7.0f), source
    );

    auto &mods = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_FLOAT_EQ(mods[0].param_.x, 9.0f);
    EXPECT_FLOAT_EQ(mods[0].param_.y, 8.0f);
    EXPECT_FLOAT_EQ(mods[0].param_.z, 7.0f);
    EXPECT_EQ(mods[0].ticksRemaining_, -1);
}

TEST_F(IRModifierVec3UpsertTest, DifferentKindGetsItsOwnSlot) {
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(1.0f), source
    );
    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::MULTIPLY, IRMath::vec3(2.0f), source
    );

    auto &mods = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(mods.size(), 2u);
}

TEST_F(IRModifierVec3UpsertTest, DifferentSourceGetsItsOwnSlot) {
    auto sourceA = IREntity::createEntity();
    auto sourceB = IREntity::createEntity();
    auto target  = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(1.0f), sourceA
    );
    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(2.0f), sourceB
    );

    auto &mods = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(mods.size(), 2u);
}

TEST_F(IRModifierVec3UpsertTest, OverridesPriorTickRemaining) {
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    // Simulate a prior push() with decay semantics.
    IRPrefab::Modifier::push(
        target, m_field, TransformKind::ADD, IRMath::vec3(1.0f), source, 1
    );
    ASSERT_EQ(IREntity::getComponent<C_Modifiers>(target).modifiersVec3_.size(), 1u);

    // Upsert from the same triple must reset ticksRemaining_ to -1.
    IRPrefab::Modifier::upsertBySource(
        target, m_field, TransformKind::ADD, IRMath::vec3(5.0f), source
    );

    auto &mods = IREntity::getComponent<C_Modifiers>(target).modifiersVec3_;
    ASSERT_EQ(mods.size(), 1u);
    EXPECT_EQ(mods[0].ticksRemaining_, -1);
    EXPECT_FLOAT_EQ(mods[0].param_.x, 5.0f);
}

// ---- upsertBySourceGlobal: vec3 ---------------------------------------------

class IRModifierGlobalVec3UpsertTest : public testing::Test {
  protected:
    IRModifierGlobalVec3UpsertTest()
        : m_entity_manager{} {
        IREntity::singletonEntity<IRComponents::C_GlobalModifiers>();
        m_field = IRPrefab::Modifier::registerFieldVec3("test.upsert_global_vec3");
    }

    IREntity::EntityManager m_entity_manager;
    IRComponents::FieldBindingId m_field{IRComponents::kInvalidFieldId};
};

TEST_F(IRModifierGlobalVec3UpsertTest, FirstCallAppends) {
    auto source = IREntity::createEntity();

    IRPrefab::Modifier::upsertBySourceGlobal(
        m_field, TransformKind::ADD, IRMath::vec3(1.0f, 2.0f, 3.0f), source
    );

    auto &c = IREntity::getComponent<IRComponents::C_GlobalModifiers>(
        IRPrefab::Modifier::globalsEntity()
    );
    ASSERT_EQ(c.modifiersVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(c.modifiersVec3_[0].param_.y, 2.0f);
    EXPECT_EQ(c.modifiersVec3_[0].ticksRemaining_, -1);
}

TEST_F(IRModifierGlobalVec3UpsertTest, SecondCallOverwrites) {
    auto source = IREntity::createEntity();

    IRPrefab::Modifier::upsertBySourceGlobal(
        m_field, TransformKind::ADD, IRMath::vec3(1.0f), source
    );
    IRPrefab::Modifier::upsertBySourceGlobal(
        m_field, TransformKind::ADD, IRMath::vec3(9.0f, 8.0f, 7.0f), source
    );

    auto &c = IREntity::getComponent<IRComponents::C_GlobalModifiers>(
        IRPrefab::Modifier::globalsEntity()
    );
    ASSERT_EQ(c.modifiersVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(c.modifiersVec3_[0].param_.x, 9.0f);
    EXPECT_FLOAT_EQ(c.modifiersVec3_[0].param_.y, 8.0f);
    EXPECT_FLOAT_EQ(c.modifiersVec3_[0].param_.z, 7.0f);
    EXPECT_EQ(c.modifiersVec3_[0].ticksRemaining_, -1);
}

// ---- upsertBySourceInPlace: vec3 (simulates PERIODIC_IDLE_POSITION_OFFSET) --

TEST(ModifierUpsertInPlaceVec3, RepeatedCallStaysSizeOne) {
    // Simulates 100 ticks of PERIODIC_IDLE_POSITION_OFFSET without
    // MODIFIER_DECAY in the pipeline: the slot count must stay at 1.
    C_Modifiers mods;
    constexpr FieldBindingId kField{3};
    auto source = IREntity::EntityId{1};

    for (int i = 0; i < 100; ++i) {
        IRPrefab::Modifier::upsertBySourceInPlace(
            mods,
            kField,
            TransformKind::ADD,
            IRMath::vec3(static_cast<float>(i), 0.0f, 0.0f),
            source
        );
    }

    ASSERT_EQ(mods.modifiersVec3_.size(), 1u);
    EXPECT_FLOAT_EQ(mods.modifiersVec3_[0].param_.x, 99.0f);
    EXPECT_EQ(mods.modifiersVec3_[0].ticksRemaining_, -1);
}

} // namespace
