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

TEST_F(PropagateTransformTest, WideHierarchyAtFivePlusDepths) {
    // T-378: validate BFS-parallel composition produces identical
    // world-transforms to the serial baseline at 6+ depth levels with
    // multiple siblings per level (each level has 3 children of the
    // prior level's first node).
    constexpr int kDepth = 6;
    constexpr int kSiblings = 3;
    std::vector<IREntity::EntityId> firstAtDepth;
    firstAtDepth.reserve(kDepth);
    auto root = IREntity::createEntity(C_LocalTransform{IRMath::vec3(1, 0, 0)});
    firstAtDepth.push_back(root);
    for (int d = 1; d < kDepth; ++d) {
        IREntity::EntityId firstChild = IREntity::kNullEntity;
        for (int s = 0; s < kSiblings; ++s) {
            auto e = IREntity::createEntity(C_LocalTransform{IRMath::vec3(1, 0, 0)});
            IREntity::setParent(e, firstAtDepth.back());
            if (s == 0)
                firstChild = e;
        }
        firstAtDepth.push_back(firstChild);
    }

    tick();

    // The straight-line chain (first-child at each level) accumulates
    // translation +1 along x at each depth — deepest node's world.x = kDepth.
    auto &deepWorld = IREntity::getComponent<C_WorldTransform>(firstAtDepth.back());
    expectVec3Near(deepWorld.translation_, IRMath::vec3(static_cast<float>(kDepth), 0, 0));
    expectVec3Near(deepWorld.scale_, IRMath::vec3(1));
}

TEST_F(PropagateTransformTest, CacheInvalidationOnSpawnDestroyReparent) {
    // T-378 acceptance: the cached level partition rebuilds on any
    // archetype-graph change. Spawning a NEW archetype shape, destroying
    // an entity whose archetype becomes empty, and reparenting to a
    // previously-unseen parent all trigger a re-sort on the next tick.
    auto *sys = m_system_manager.getSystemParams<IRSystem::System<IRSystem::PROPAGATE_TRANSFORM>>(
        m_system_id
    );
    ASSERT_NE(sys, nullptr);

    // First tick: empty world. Cache settles on whatever archetype set
    // queryArchetypeNodesSimple returns (likely the empty
    // C_LocalTransform+C_WorldTransform archetype).
    tick();
    auto signatureAfterEmpty = sys->cachedSignature_;

    // Spawn a root entity — new archetype if it's the first
    // C_LocalTransform+C_WorldTransform shape (root archetype, no
    // CHILD_OF, so its signature entry has parentNodeId == 0).
    auto root = IREntity::createEntity(C_LocalTransform{IRMath::vec3(1, 2, 3)});
    tick();
    auto signatureAfterSpawn = sys->cachedSignature_;
    // Spawn either added a new node or kept the same one; either way
    // we should now have at least one entry.
    EXPECT_FALSE(signatureAfterSpawn.empty());

    // Reparent — creates a new archetype "C_LocalTransform+C_WorldTransform
    // CHILD_OF root" — guaranteed new node, so cache must invalidate.
    auto child = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0, 0, 1)});
    IREntity::setParent(child, root);
    tick();
    auto signatureAfterReparent = sys->cachedSignature_;
    EXPECT_NE(signatureAfterSpawn, signatureAfterReparent);
    // Composition still correct.
    auto &childWorld = IREntity::getComponent<C_WorldTransform>(child);
    expectVec3Near(childWorld.translation_, IRMath::vec3(1, 2, 4));

    // Destroy the child. If the child's archetype was singly-occupied,
    // the archetype may be reclaimed, changing the signature; if it
    // still hangs around with length=0, the signature stays — either
    // way the cache-validity check is safe and a re-sort doesn't break
    // correctness on the next tick.
    IREntity::destroyEntity(child);
    tick();
    // Composition for the root still produces (1, 2, 3).
    auto &rootWorld = IREntity::getComponent<C_WorldTransform>(root);
    expectVec3Near(rootWorld.translation_, IRMath::vec3(1, 2, 3));
}

TEST_F(PropagateTransformTest, CacheHitsWhenTopologyIsStable) {
    // Once the partition is built, repeating ticks on the same scene
    // should reuse the cached level structure (signature comparison
    // returns equal). The cached level vector itself stays addressable
    // and stable across ticks.
    auto p1 = IREntity::createEntity(C_LocalTransform{IRMath::vec3(1, 0, 0)});
    auto c1 = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0, 1, 0)});
    auto c2 = IREntity::createEntity(C_LocalTransform{IRMath::vec3(0, 0, 1)});
    IREntity::setParent(c1, p1);
    IREntity::setParent(c2, p1);

    tick();
    auto *sys = m_system_manager.getSystemParams<IRSystem::System<IRSystem::PROPAGATE_TRANSFORM>>(
        m_system_id
    );
    ASSERT_NE(sys, nullptr);
    const auto signatureFirst = sys->cachedSignature_;
    const auto levelCountFirst = sys->levels_.size();

    // Mutate per-entity values (not the graph). Cache should stay valid.
    auto &c1Local = IREntity::getComponent<C_LocalTransform>(c1);
    c1Local.translation_ = IRMath::vec3(0, 5, 0);
    tick();
    EXPECT_EQ(sys->cachedSignature_, signatureFirst);
    EXPECT_EQ(sys->levels_.size(), levelCountFirst);

    // Composition reflects the updated local.
    auto &c1World = IREntity::getComponent<C_WorldTransform>(c1);
    expectVec3Near(c1World.translation_, IRMath::vec3(1, 5, 0));
}

} // namespace
