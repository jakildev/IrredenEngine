#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/common/modifier.hpp>
#include <irreden/common/transform_modifier_fields.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

#include <vector>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_Modifiers;
using IRComponents::C_ResolvedFields;
using IRComponents::C_WorldTransform;
using IRComponents::TransformKind;

constexpr float kEps = 1e-4f;

class PropagateTransformTest : public testing::Test {
  protected:
    PropagateTransformTest()
        : m_entity_manager{}
        , m_system_manager{} {
        m_system_id = IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>();
        m_system_manager.registerPipeline(IRTime::Events::UPDATE, {m_system_id});
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    static void expectVec3Near(IRMath::vec3 actual, IRMath::vec3 expected, float eps = kEps) {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    static void expectVec4Near(IRMath::vec4 actual, IRMath::vec4 expected, float eps = kEps) {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
        EXPECT_NEAR(actual.w, expected.w, eps);
    }

    // Build a quaternion that rotates `angleRad` around the canonical axis.
    static IRMath::vec4 quatRotateZ(float angleRad) {
        const float half = angleRad * 0.5f;
        return IRMath::vec4(0.0f, 0.0f, IRMath::sin(half), IRMath::cos(half));
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::SystemId m_system_id;
};

TEST_F(PropagateTransformTest, ComponentDefaultsAreIdentity) {
    C_LocalTransform local;
    EXPECT_FLOAT_EQ(local.scale_.x, 1.0f);
    EXPECT_FLOAT_EQ(local.scale_.y, 1.0f);
    EXPECT_FLOAT_EQ(local.scale_.z, 1.0f);
    expectVec4Near(local.rotation_, IRMath::vec4(0, 0, 0, 1));
    expectVec3Near(local.translation_, IRMath::vec3(0));

    C_WorldTransform world;
    expectVec3Near(world.scale_, IRMath::vec3(1));
    expectVec4Near(world.rotation_, IRMath::vec4(0, 0, 0, 1));
    expectVec3Near(world.translation_, IRMath::vec3(0));
}

TEST_F(PropagateTransformTest, CreateEntityAutoAttachesTransforms) {
    auto id = IREntity::createEntity();
    auto local = IREntity::getComponentOptional<C_LocalTransform>(id);
    auto world = IREntity::getComponentOptional<C_WorldTransform>(id);
    ASSERT_TRUE(local.has_value());
    ASSERT_TRUE(world.has_value());
}

TEST_F(PropagateTransformTest, RootEntityWorldEqualsLocal) {
    auto id = IREntity::createEntity(C_LocalTransform{IRMath::vec3(5, 6, 7)});

    tick();

    auto &world = IREntity::getComponent<C_WorldTransform>(id);
    expectVec3Near(world.translation_, IRMath::vec3(5, 6, 7));
    expectVec3Near(world.scale_, IRMath::vec3(1));
    expectVec4Near(world.rotation_, IRMath::vec4(0, 0, 0, 1));
}

TEST_F(PropagateTransformTest, TwoLevelChainCompositionTranslation) {
    auto parent = IREntity::createEntity(C_LocalTransform{IRMath::vec3(10, 0, 0)});
    auto child = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0, 3, 0)});
    IREntity::setParent(child, parent);

    tick();

    auto &parentWorld = IREntity::getComponent<C_WorldTransform>(parent);
    auto &childWorld = IREntity::getComponent<C_WorldTransform>(child);
    expectVec3Near(parentWorld.translation_, IRMath::vec3(10, 0, 0));
    expectVec3Near(childWorld.translation_, IRMath::vec3(10, 3, 0));
}

TEST_F(PropagateTransformTest, ThreeLevelChainComposesScaleAndRotation) {
    // Grandparent: scale 2, no rotation, translate (1,0,0).
    // Parent (child of grandparent): scale 3, no rotation, translate (0,1,0)
    //   in grandparent's local frame → grandparent.scale*parent.local.translation
    //   = (0, 2, 0); plus grandparent.translation = (1, 2, 0).
    // Child (grandchild): scale 0.5, no rotation, translate (0,0,1) in
    //   parent's local frame → parent.world.scale=6 times local = (0,0,6);
    //   plus parent.world = (1, 2, 0); grandchild.translation = (1, 2, 6).

    auto gp = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(1, 0, 0), IRMath::vec4(0, 0, 0, 1), IRMath::vec3(2)}
    );
    auto p = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0, 1, 0), IRMath::vec4(0, 0, 0, 1), IRMath::vec3(3)}
    );
    auto c = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0, 0, 1), IRMath::vec4(0, 0, 0, 1), IRMath::vec3(0.5f)}
    );
    IREntity::setParent(p, gp);
    IREntity::setParent(c, p);

    tick();

    auto &cw = IREntity::getComponent<C_WorldTransform>(c);
    expectVec3Near(cw.translation_, IRMath::vec3(1, 2, 6));
    expectVec3Near(cw.scale_, IRMath::vec3(3.0f));
}

TEST_F(PropagateTransformTest, RotationPropagatesAcrossThreeLevels) {
    // 90° around Z at each level; grandchild rotation = three 90° rotations
    // composed = 270° around Z.
    const auto q90 = quatRotateZ(IRMath::kPi * 0.5f);

    auto gp = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0), q90});
    auto p = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0), q90});
    auto c = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0), q90});
    IREntity::setParent(p, gp);
    IREntity::setParent(c, p);

    tick();

    const auto expected = quatRotateZ(IRMath::kPi * 1.5f);
    auto &cw = IREntity::getComponent<C_WorldTransform>(c);

    // Quaternion sign ambiguity: q and -q represent the same rotation.
    // Compare via rotated-axis result for robustness.
    auto rotatedX = IRMath::rotateVectorByQuat(IRMath::vec3(1, 0, 0), cw.rotation_);
    auto expectedX = IRMath::rotateVectorByQuat(IRMath::vec3(1, 0, 0), expected);
    expectVec3Near(rotatedX, expectedX);
}

TEST_F(PropagateTransformTest, TenLevelChainGrandchildWorldCorrect) {
    // 10-level chain with non-trivial scale at each level. Translation
    // (1,0,0) per link in local space; scale 0.9 per link compounds
    // through the chain. Final translation accumulates as a geometric
    // series under the scaled parent frame:
    //   sum_{k=0..9} (0.9^k) = (1 - 0.9^10) / 0.1 ≈ 6.5132.
    // Final scale = 0.9^10 ≈ 0.34868.
    std::vector<IREntity::EntityId> chain;
    chain.reserve(10);
    for (int i = 0; i < 10; ++i) {
        chain.push_back(IREntity::createEntity(C_LocalTransform{
            IRMath::vec3(1, 0, 0),
            IRMath::vec4(0, 0, 0, 1),
            IRMath::vec3(0.9f),
        }));
    }
    for (int i = 1; i < 10; ++i) {
        IREntity::setParent(chain[i], chain[i - 1]);
    }

    tick();

    auto &world = IREntity::getComponent<C_WorldTransform>(chain.back());

    float expectedTx = 0.0f;
    float expectedScale = 1.0f;
    for (int i = 0; i < 10; ++i) {
        expectedTx += expectedScale;
        expectedScale *= 0.9f;
    }
    EXPECT_NEAR(world.translation_.x, expectedTx, 1e-3f);
    EXPECT_NEAR(world.translation_.y, 0.0f, 1e-4f);
    EXPECT_NEAR(world.translation_.z, 0.0f, 1e-4f);

    EXPECT_NEAR(world.scale_.x, expectedScale, 1e-4f);
    EXPECT_NEAR(world.scale_.y, expectedScale, 1e-4f);
    EXPECT_NEAR(world.scale_.z, expectedScale, 1e-4f);
}

TEST_F(PropagateTransformTest, ModifierTranslationAppliedPostChain) {
    const auto translationField = IRPrefab::TransformModifier::translationField();
    const auto entity = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0)},
        C_Modifiers{},
        C_ResolvedFields{}
    );
    auto &resolved = IREntity::getComponent<C_ResolvedFields>(entity);
    resolved.resetVec3(translationField, IRMath::vec3(0));
    auto &mods = IREntity::getComponent<C_Modifiers>(entity);
    mods.modifiersVec3_.push_back(IRComponents::ModifierVec3{
        translationField,
        TransformKind::ADD,
        IRMath::vec3(2, 4, 8),
        IREntity::kNullEntity,
        -1
    });

    // Inline-resolve since this test doesn't register the resolver pipeline.
    resolved.resetVec3(translationField, IRMath::vec3(0));
    for (auto &m : mods.modifiersVec3_) {
        resolved.apply(m);
    }

    tick();

    auto &world = IREntity::getComponent<C_WorldTransform>(entity);
    expectVec3Near(world.translation_, IRMath::vec3(2, 4, 8));
}

TEST_F(PropagateTransformTest, ModifierScaleAppliedMultiplicatively) {
    const auto scaleField = IRPrefab::TransformModifier::scaleField();
    const auto entity = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0), IRMath::vec4(0, 0, 0, 1), IRMath::vec3(2)},
        C_Modifiers{},
        C_ResolvedFields{}
    );
    auto &resolved = IREntity::getComponent<C_ResolvedFields>(entity);
    resolved.resetVec3(scaleField, IRMath::vec3(1));
    auto &mods = IREntity::getComponent<C_Modifiers>(entity);
    mods.modifiersVec3_.push_back(IRComponents::ModifierVec3{
        scaleField,
        TransformKind::MULTIPLY,
        IRMath::vec3(0.5f, 1.0f, 4.0f),
        IREntity::kNullEntity,
        -1
    });

    resolved.resetVec3(scaleField, IRMath::vec3(1));
    for (auto &m : mods.modifiersVec3_) {
        resolved.apply(m);
    }

    tick();

    auto &world = IREntity::getComponent<C_WorldTransform>(entity);
    // Final scale = local.scale (2) * modifier_scale (0.5, 1, 4) = (1, 2, 8).
    expectVec3Near(world.scale_, IRMath::vec3(1, 2, 8));
}

TEST_F(PropagateTransformTest, BeginAndEndFireWithZeroEntities) {
    // Sanity: pipeline runs without entities and doesn't crash.
    tick();
    SUCCEED();
}

} // namespace
