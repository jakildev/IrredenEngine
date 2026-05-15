#include <gtest/gtest.h>

#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/modifier_compose.hpp>
#include <irreden/common/modifier_field_registry.hpp>
#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>

#include <stdexcept>
#include <vector>

namespace {

using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::FieldBindingId;
using IRComponents::FieldValueType;
using IRComponents::kInvalidFieldId;
using IRComponents::ModifierQuat;
using IRComponents::ResolvedFieldQuat;
using IRComponents::TransformKind;

using IRPrefab::Modifier::detail::composeForFieldQuat;
using IRPrefab::Modifier::detail::FieldRegistry;

constexpr FieldBindingId kFieldA = FieldBindingId{1};
constexpr FieldBindingId kFieldB = FieldBindingId{2};

ModifierQuat mkQ(FieldBindingId field, TransformKind kind, IRMath::vec4 param) {
    return ModifierQuat{field, kind, param, IREntity::EntityId{0}, -1};
}

// Quaternion around +Z by `radians` (the rotation axis that maps cleanly
// onto the iso depth axis). All tests below stack rotations around the
// same axis so float-stable expected values can be derived from a single
// trig table per multiply count.
IRMath::vec4 rotZ(float radians) {
    const float halfAngle = radians * 0.5f;
    return IRMath::vec4(0.0f, 0.0f, IRMath::sin(halfAngle), IRMath::cos(halfAngle));
}

constexpr float kEpsilon = 1e-5f;

// Rotate +X by the quaternion and inspect the resulting vec3 — that's
// the stable observable for "this quat actually represents the rotation
// I think it does", independent of sign-flip aliasing.
void expectRotZApplied(IRMath::vec4 q, float expectedRadians) {
    const IRMath::vec3 x{1.0f, 0.0f, 0.0f};
    const IRMath::vec3 rotated = IRMath::rotateVectorByQuat(x, q);
    EXPECT_NEAR(rotated.x, IRMath::cos(expectedRadians), kEpsilon);
    EXPECT_NEAR(rotated.y, IRMath::sin(expectedRadians), kEpsilon);
    EXPECT_NEAR(rotated.z, 0.0f, kEpsilon);
}

// ---- Field registry: typed binding ----------------------------------------

TEST(ModifierRegistryQuat, RegisterFieldQuatAssignsType) {
    FieldRegistry registry;
    auto scalarId = registry.registerField("scalar.field");
    auto vec3Id = registry.registerFieldVec3("vec3.field");
    auto quatId = registry.registerFieldQuat("quat.field");

    EXPECT_EQ(registry.fieldType(scalarId), FieldValueType::SCALAR);
    EXPECT_EQ(registry.fieldType(vec3Id), FieldValueType::VEC3);
    EXPECT_EQ(registry.fieldType(quatId), FieldValueType::QUAT);
    EXPECT_EQ(registry.fieldCount(), 3u);
}

// ---- Compose: structured quat transforms ---------------------------------

TEST(ModifierComposeQuat, NoModifiersReturnsBase) {
    std::vector<ModifierQuat> mods;
    IRMath::vec4 base = rotZ(0.5f);
    IRMath::vec4 result = composeForFieldQuat(base, kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, base.x);
    EXPECT_FLOAT_EQ(result.y, base.y);
    EXPECT_FLOAT_EQ(result.z, base.z);
    EXPECT_FLOAT_EQ(result.w, base.w);
}

TEST(ModifierComposeQuat, MultiplyComposesPostRotate) {
    // Base = rotZ(theta_base); mod = rotZ(theta_mod).
    // mod * base should yield rotZ(theta_mod + theta_base).
    const float thetaBase = 0.4f;
    const float thetaMod = 0.6f;
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(thetaMod)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(thetaBase), kFieldA, mods);
    expectRotZApplied(result, thetaMod + thetaBase);
}

TEST(ModifierComposeQuat, MultiplyStacksLeftMultiplyOrder) {
    // mods pushed in order [r1, r2, r3]; resolved = r3 * r2 * r1 * base.
    // For axis-aligned rotations around Z, that's additive in angle:
    // theta_total = base + r1 + r2 + r3.
    const float base = 0.1f;
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.2f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.3f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.4f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(base), kFieldA, mods);
    expectRotZApplied(result, base + 0.2f + 0.3f + 0.4f);
}

TEST(ModifierComposeQuat, MultiplyResultIsNormalized) {
    // Even without intentional unit-vec deviation, stacked MULTIPLYs
    // accumulate float drift in real workloads. Force-feed an
    // un-normalized base to confirm the compose pass normalizes the
    // final output.
    IRMath::vec4 unnormalized = rotZ(0.7f) * 1.5f; // intentionally off-unit
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.0f)), // identity
    };
    IRMath::vec4 result = composeForFieldQuat(unnormalized, kFieldA, mods);
    const float length = IRMath::sqrt(IRMath::dot(result, result));
    EXPECT_NEAR(length, 1.0f, kEpsilon);
}

TEST(ModifierComposeQuat, IdentityFastPathSkipsNormalize) {
    // No modifiers means no MULTIPLY drift; the compose path should
    // return base unchanged (including length) rather than normalizing
    // an arbitrary caller-supplied vec4. Validates the
    // `if (touched) normalize` gate.
    IRMath::vec4 unnormalized{0.3f, 0.4f, 0.5f, 0.5f};
    std::vector<ModifierQuat> mods;
    IRMath::vec4 result = composeForFieldQuat(unnormalized, kFieldA, mods);
    EXPECT_FLOAT_EQ(result.x, 0.3f);
    EXPECT_FLOAT_EQ(result.y, 0.4f);
    EXPECT_FLOAT_EQ(result.z, 0.5f);
    EXPECT_FLOAT_EQ(result.w, 0.5f);
}

TEST(ModifierComposeQuat, SetReplacesValueInPushOrder) {
    // SET replaces the running value in place — subsequent MULTIPLYs
    // compose on top of the SET'd value, not the base.
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.5f)), // discarded by SET
        mkQ(kFieldA, TransformKind::SET, rotZ(0.1f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.2f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, mods);
    expectRotZApplied(result, 0.1f + 0.2f);
}

TEST(ModifierComposeQuat, OverrideShortCircuitsPriorModifiers) {
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.5f)),
        mkQ(kFieldA, TransformKind::SET, rotZ(0.7f)),
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.2f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, mods);
    expectRotZApplied(result, 0.2f);
}

TEST(ModifierComposeQuat, OverrideLetsLaterModifiersApply) {
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(100.0f)),
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.1f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.2f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, mods);
    expectRotZApplied(result, 0.1f + 0.2f);
}

TEST(ModifierComposeQuat, LatestOverrideWins) {
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.1f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.5f)),
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.3f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, mods);
    expectRotZApplied(result, 0.3f);
}

TEST(ModifierComposeQuat, OtherFieldsDoNotInfluenceTarget) {
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.1f)),
        mkQ(kFieldB, TransformKind::MULTIPLY, rotZ(99.0f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.2f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(0.0f), kFieldA, mods);
    expectRotZApplied(result, 0.1f + 0.2f);
}

TEST(ModifierComposeQuat, AddCLAMPMINCLAMPMAXSilentlySkipped) {
    // Direct-vector construction bypasses the push-API assertion;
    // verify the compose path treats these as no-ops rather than
    // applying nonsense.
    std::vector<ModifierQuat> mods{
        mkQ(kFieldA, TransformKind::ADD, rotZ(99.0f)),       // skipped
        mkQ(kFieldA, TransformKind::CLAMP_MIN, rotZ(99.0f)), // skipped
        mkQ(kFieldA, TransformKind::CLAMP_MAX, rotZ(99.0f)), // skipped
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.3f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(0.2f), kFieldA, mods);
    expectRotZApplied(result, 0.3f + 0.2f);
}

// ---- Compose: globals + entity layering ----------------------------------

TEST(ModifierComposeQuatGlobals, GlobalsApplyBeforeEntity) {
    // Combined sequence is globals first, then entity (per
    // modifier_compose.hpp's `modsA` = globals, `modsB` = entity-mods).
    // Left-multiply order means entity's MULTIPLY ends up outermost:
    //   resolved = entity * globals * base.
    // For axis-aligned rotZ that's additive in angle.
    std::vector<ModifierQuat> globals{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.1f)),
    };
    std::vector<ModifierQuat> entity{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.2f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(0.3f), kFieldA, globals, entity);
    expectRotZApplied(result, 0.1f + 0.2f + 0.3f);
}

TEST(ModifierComposeQuatGlobals, EntityOverrideTrumpsGlobalAlgebra) {
    std::vector<ModifierQuat> globals{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(99.0f)),
    };
    std::vector<ModifierQuat> entity{
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.4f)),
    };
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, globals, entity);
    expectRotZApplied(result, 0.4f);
}

TEST(ModifierComposeQuatGlobals, GlobalOverrideAppliedWhenEntityHasNone) {
    std::vector<ModifierQuat> globals{
        mkQ(kFieldA, TransformKind::OVERRIDE, rotZ(0.2f)),
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.1f)),
    };
    std::vector<ModifierQuat> entity{
        mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.3f)),
    };
    // Globals OVERRIDE → 0.2; globals MULTIPLY 0.1 → 0.3; entity MULTIPLY
    // 0.3 (outermost) → 0.6.
    IRMath::vec4 result = composeForFieldQuat(rotZ(99.0f), kFieldA, globals, entity);
    expectRotZApplied(result, 0.2f + 0.1f + 0.3f);
}

// ---- C_ResolvedFields helpers -------------------------------------------

TEST(CResolvedFieldsQuat, ResetInsertsAndUpdates) {
    C_ResolvedFields rf;
    rf.resetQuat(kFieldA, rotZ(0.1f));
    ASSERT_EQ(rf.fieldsQuat_.size(), 1u);
    EXPECT_NEAR(rf.getQuat(kFieldA).w, rotZ(0.1f).w, kEpsilon);

    rf.resetQuat(kFieldA, rotZ(0.5f));
    ASSERT_EQ(rf.fieldsQuat_.size(), 1u);
    EXPECT_NEAR(rf.getQuat(kFieldA).w, rotZ(0.5f).w, kEpsilon);

    rf.resetQuat(kFieldB, rotZ(0.2f));
    ASSERT_EQ(rf.fieldsQuat_.size(), 2u);
    EXPECT_NEAR(rf.getQuat(kFieldB).z, rotZ(0.2f).z, kEpsilon);
}

TEST(CResolvedFieldsQuat, ApplyMultiplyComposesPostRotate) {
    C_ResolvedFields rf;
    rf.resetQuat(kFieldA, rotZ(0.4f));
    rf.apply(mkQ(kFieldA, TransformKind::MULTIPLY, rotZ(0.6f)));
    // mod * base = rotZ(0.6 + 0.4).
    expectRotZApplied(rf.getQuat(kFieldA), 0.6f + 0.4f);
}

TEST(CResolvedFieldsQuat, ApplyAddCLAMPNoops) {
    C_ResolvedFields rf;
    rf.resetQuat(kFieldA, rotZ(0.5f));
    rf.apply(mkQ(kFieldA, TransformKind::ADD, rotZ(99.0f)));
    rf.apply(mkQ(kFieldA, TransformKind::CLAMP_MIN, rotZ(99.0f)));
    rf.apply(mkQ(kFieldA, TransformKind::CLAMP_MAX, rotZ(99.0f)));
    expectRotZApplied(rf.getQuat(kFieldA), 0.5f);
}

TEST(CResolvedFieldsQuat, GetFallbackForUnknownFieldIsIdentity) {
    C_ResolvedFields rf;
    auto q = rf.getQuat(kFieldA);
    EXPECT_FLOAT_EQ(q.x, 0.0f);
    EXPECT_FLOAT_EQ(q.y, 0.0f);
    EXPECT_FLOAT_EQ(q.z, 0.0f);
    EXPECT_FLOAT_EQ(q.w, 1.0f);
}

TEST(CResolvedFieldsQuat, GetFallbackCustomOverrideRespected) {
    C_ResolvedFields rf;
    IRMath::vec4 customFallback = rotZ(1.234f);
    auto q = rf.getQuat(kFieldA, customFallback);
    EXPECT_FLOAT_EQ(q.x, customFallback.x);
    EXPECT_FLOAT_EQ(q.y, customFallback.y);
    EXPECT_FLOAT_EQ(q.z, customFallback.z);
    EXPECT_FLOAT_EQ(q.w, customFallback.w);
}

TEST(CResolvedFieldsQuat, ScalarVec3AndQuatPathsAreIndependent) {
    // All three vectors live on the same C_ResolvedFields; resetQuat must
    // not stomp the scalar / vec3 fields with the same id, and vice versa.
    C_ResolvedFields rf;
    rf.reset(kFieldA, 1.5f);
    rf.resetVec3(kFieldA, IRMath::vec3(10.0f, 20.0f, 30.0f));
    rf.resetQuat(kFieldA, rotZ(0.7f));

    EXPECT_FLOAT_EQ(rf.get(kFieldA), 1.5f);
    EXPECT_FLOAT_EQ(rf.getVec3(kFieldA).y, 20.0f);
    EXPECT_NEAR(rf.getQuat(kFieldA).w, rotZ(0.7f).w, kEpsilon);
}

// ---- Decay semantics: in-place via std::remove_if -----------------------

void decayQuatOnce(std::vector<ModifierQuat> &v) {
    auto end = std::remove_if(
        v.begin(), v.end(), IRComponents::detail::tickAndExpired<ModifierQuat>
    );
    v.erase(end, v.end());
}

TEST(ModifierQuatDecay, MinusOneSentinelIsKeptForever) {
    std::vector<ModifierQuat> mods{
        ModifierQuat{kFieldA, TransformKind::MULTIPLY, rotZ(0.1f), IREntity::EntityId{0}, -1},
    };
    decayQuatOnce(mods);
    EXPECT_EQ(mods.size(), 1u);
}

TEST(ModifierQuatDecay, ExactExpiryDropsAtZero) {
    std::vector<ModifierQuat> mods{
        ModifierQuat{kFieldA, TransformKind::MULTIPLY, rotZ(0.1f), IREntity::EntityId{0}, 1},
    };
    decayQuatOnce(mods);
    EXPECT_EQ(mods.size(), 0u);
}

TEST(ModifierQuatDecay, SixtyTickModifierExpiresAtSixty) {
    std::vector<ModifierQuat> mods{
        ModifierQuat{kFieldA, TransformKind::MULTIPLY, rotZ(0.1f), IREntity::EntityId{0}, 60},
    };
    for (int i = 0; i < 59; ++i) {
        decayQuatOnce(mods);
        ASSERT_EQ(mods.size(), 1u) << "premature drop at tick " << i;
    }
    decayQuatOnce(mods);
    EXPECT_EQ(mods.size(), 0u);
}

// ---- Push API: type-mismatch dispatch + assertions -----------------------

class IRModifierQuatPushTest : public testing::Test {
  protected:
    IRModifierQuatPushTest()
        : m_entity_manager{} {}

    IREntity::EntityManager m_entity_manager;
};

TEST_F(IRModifierQuatPushTest, ScalarPushAgainstQuatFieldNoOps) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto quatFieldId = IRPrefab::Modifier::registerFieldQuat("test.quat_only");

    IRPrefab::Modifier::push(target, quatFieldId, TransformKind::SET, 1.0f, IREntity::kNullEntity);

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
    EXPECT_EQ(c.modifiersQuat_.size(), 0u);
}

TEST_F(IRModifierQuatPushTest, QuatPushAgainstScalarFieldNoOps) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto scalarFieldId = IRPrefab::Modifier::registerField("test.scalar_only_quat");

    IRPrefab::Modifier::push(
        target, scalarFieldId, TransformKind::MULTIPLY, rotZ(0.5f), IREntity::kNullEntity
    );

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersQuat_.size(), 0u);
}

TEST_F(IRModifierQuatPushTest, QuatPushAgainstVec3FieldNoOps) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto vec3FieldId = IRPrefab::Modifier::registerFieldVec3("test.vec3_only_for_quat");

    IRPrefab::Modifier::push(
        target, vec3FieldId, TransformKind::MULTIPLY, rotZ(0.5f), IREntity::kNullEntity
    );

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiersQuat_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
}

TEST_F(IRModifierQuatPushTest, QuatPushAgainstQuatFieldLandsInQuatVector) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto quatFieldId = IRPrefab::Modifier::registerFieldQuat("test.quat_landing");

    IRPrefab::Modifier::push(
        target, quatFieldId, TransformKind::MULTIPLY, rotZ(0.3f), IREntity::kNullEntity
    );

    auto &c = IREntity::getComponent<C_Modifiers>(target);
    EXPECT_EQ(c.modifiers_.size(), 0u);
    EXPECT_EQ(c.modifiersVec3_.size(), 0u);
    ASSERT_EQ(c.modifiersQuat_.size(), 1u);
    EXPECT_NEAR(c.modifiersQuat_[0].param_.z, rotZ(0.3f).z, kEpsilon);
}

TEST_F(IRModifierQuatPushTest, ApplyToFieldQuatComposesDirectly) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto field = IRPrefab::Modifier::registerFieldQuat("test.apply_quat_query");
    IRPrefab::Modifier::push(
        target, field, TransformKind::MULTIPLY, rotZ(0.4f), IREntity::kNullEntity
    );

    auto result = IRPrefab::Modifier::applyToFieldQuat(target, field, rotZ(0.3f));
    expectRotZApplied(result, 0.4f + 0.3f);
}

TEST_F(IRModifierQuatPushTest, AddOnQuatFieldAssertsAndSkips) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto field = IRPrefab::Modifier::registerFieldQuat("test.quat_add_assert");

    // IR_ASSERT throws std::runtime_error in debug builds; the test
    // binary is built debug, so EXPECT_THROW is the right harness.
    EXPECT_THROW(
        IRPrefab::Modifier::push(
            target, field, TransformKind::ADD, rotZ(0.5f), IREntity::kNullEntity
        ),
        std::runtime_error
    );
}

TEST_F(IRModifierQuatPushTest, ClampMinOnQuatFieldAssertsAndSkips) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto field = IRPrefab::Modifier::registerFieldQuat("test.quat_clamp_min_assert");

    EXPECT_THROW(
        IRPrefab::Modifier::push(
            target, field, TransformKind::CLAMP_MIN, rotZ(0.5f), IREntity::kNullEntity
        ),
        std::runtime_error
    );
}

TEST_F(IRModifierQuatPushTest, ClampMaxOnQuatFieldAssertsAndSkips) {
    auto target = IREntity::createEntity(C_Modifiers{});
    auto field = IRPrefab::Modifier::registerFieldQuat("test.quat_clamp_max_assert");

    EXPECT_THROW(
        IRPrefab::Modifier::push(
            target, field, TransformKind::CLAMP_MAX, rotZ(0.5f), IREntity::kNullEntity
        ),
        std::runtime_error
    );
}

// ---- Auto-sweep on entity destruction (quat vectors) --------------------

class IRModifierQuatAutoSweepTest : public testing::Test {
  protected:
    IRModifierQuatAutoSweepTest()
        : m_entity_manager{} {
        m_hook_id = IREntity::getEntityManager().registerPreDestroyHook(
            [](IREntity::EntityId destroyed) {
                IRPrefab::Modifier::removeBySource(destroyed);
            }
        );
    }

    ~IRModifierQuatAutoSweepTest() override {
        IREntity::getEntityManager().unregisterPreDestroyHook(m_hook_id);
    }

    IREntity::EntityManager m_entity_manager;
    IREntity::PreDestroyHookId m_hook_id{IREntity::kInvalidPreDestroyHookId};
};

TEST_F(IRModifierQuatAutoSweepTest, DestroyingSourceStripsQuatModifiersFromTarget) {
    auto field = IRPrefab::Modifier::registerFieldQuat("test.quat_sweep");
    auto source = IREntity::createEntity();
    auto target = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::push(target, field, TransformKind::MULTIPLY, rotZ(0.4f), source);
    ASSERT_EQ(IREntity::getComponent<C_Modifiers>(target).modifiersQuat_.size(), 1u);

    m_entity_manager.destroyEntity(source);

    EXPECT_EQ(IREntity::getComponent<C_Modifiers>(target).modifiersQuat_.size(), 0u);
}

TEST_F(IRModifierQuatAutoSweepTest, DestroyingSourceLeavesOtherSourceQuatModifiersAlone) {
    auto field = IRPrefab::Modifier::registerFieldQuat("test.quat_sweep_partial");
    auto sourceA = IREntity::createEntity();
    auto sourceB = IREntity::createEntity();
    auto target  = IREntity::createEntity(C_Modifiers{});

    IRPrefab::Modifier::push(target, field, TransformKind::MULTIPLY, rotZ(0.1f), sourceA);
    IRPrefab::Modifier::push(target, field, TransformKind::MULTIPLY, rotZ(0.2f), sourceB);

    m_entity_manager.destroyEntity(sourceA);

    auto &after = IREntity::getComponent<C_Modifiers>(target).modifiersQuat_;
    ASSERT_EQ(after.size(), 1u);
    EXPECT_EQ(after[0].source_, sourceB);
}

} // namespace
