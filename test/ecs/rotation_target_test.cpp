#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/update/components/component_rotation_target.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/update/systems/system_rotation_target_local_transform.hpp>

namespace {

using IRComponents::C_LocalTransform;
using IRComponents::C_RotationTarget;
using IRComponents::C_WorldTransform;

constexpr float kEps = 1e-4f;

class RotationTargetTest : public testing::Test {
  protected:
    RotationTargetTest()
        : m_entity_manager{}
        , m_system_manager{} {
        // Real pipeline order: write the local rotation, then propagate it into
        // the world transform — same contract as AUTO_SPIN_LOCAL_TRANSFORM.
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::ROTATION_TARGET_LOCAL_TRANSFORM>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
        );
    }

    void tick() { m_system_manager.executePipeline(IRTime::Events::UPDATE); }

    static void expectVec3Near(IRMath::vec3 actual, IRMath::vec3 expected, float eps = kEps) {
        EXPECT_NEAR(actual.x, expected.x, eps);
        EXPECT_NEAR(actual.y, expected.y, eps);
        EXPECT_NEAR(actual.z, expected.z, eps);
    }

    // Quaternion sign ambiguity: q and -q are the same rotation. Compare two
    // quaternions by their action on the basis vectors instead of componentwise.
    static void expectSameRotation(IRMath::vec4 actual, IRMath::vec4 expected) {
        for (const auto &v :
             {IRMath::vec3(1, 0, 0), IRMath::vec3(0, 1, 0), IRMath::vec3(0, 0, 1)}) {
            expectVec3Near(
                IRMath::rotateVectorByQuat(v, actual),
                IRMath::rotateVectorByQuat(v, expected)
            );
        }
    }

    // The rotation a correctly-mapped C_RotationTarget should produce.
    static IRMath::vec4 expectedRotation(IRMath::vec3 axis, float angle) {
        return IRMath::quatAxisAngle(axis, angle);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(RotationTargetTest, EndpointsMapToMinAndMaxAngle) {
    const IRMath::vec3 axis(0, 0, 1);
    auto id = IREntity::createEntity(
        C_LocalTransform{}, C_RotationTarget{axis, 0.0f, IRMath::kHalfPi, 0.0f}
    );

    tick();
    auto &world = IREntity::getComponent<C_WorldTransform>(id);
    expectSameRotation(world.rotation_, expectedRotation(axis, 0.0f));

    // Drive the input to the top of its range — angle should reach maxAngle.
    IREntity::getComponent<C_RotationTarget>(id).input_ = 1.0f;
    tick();
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, IRMath::kHalfPi)
    );
}

TEST_F(RotationTargetTest, MidpointInputMapsToMidAngleLinear) {
    const IRMath::vec3 axis(0, 0, 1);
    auto id = IREntity::createEntity(
        C_LocalTransform{}, C_RotationTarget{axis, 0.0f, IRMath::kHalfPi, 0.5f}
    );

    tick();
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, IRMath::kHalfPi * 0.5f)
    );
}

TEST_F(RotationTargetTest, NormalizesInputAcrossExplicitRange) {
    // MIDI-CC style: a 0..127 input range mapped onto the angle sweep.
    const IRMath::vec3 axis(0, 1, 0);
    auto id = IREntity::createEntity(
        C_LocalTransform{},
        C_RotationTarget{axis, -IRMath::kHalfPi, IRMath::kHalfPi, 63.5f, 0.0f, 127.0f}
    );

    tick();
    // input 63.5 of [0,127] → t = 0.5 → angle 0 (midpoint of [-pi/2, pi/2]).
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, 0.0f)
    );
}

TEST_F(RotationTargetTest, ClampsOutOfRangeInputToAngleBounds) {
    const IRMath::vec3 axis(0, 0, 1);
    auto id = IREntity::createEntity(
        C_LocalTransform{}, C_RotationTarget{axis, 0.0f, IRMath::kHalfPi, 5.0f}
    );

    tick();
    // input 5 (> inputMax 1) clamps to t=1 → maxAngle, no overshoot.
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, IRMath::kHalfPi)
    );

    IREntity::getComponent<C_RotationTarget>(id).input_ = -3.0f;
    tick();
    // input -3 (< inputMin 0) clamps to t=0 → minAngle.
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, 0.0f)
    );
}

TEST_F(RotationTargetTest, EasingCurveShapesResponse) {
    const IRMath::vec3 axis(0, 0, 1);
    // Quadratic ease-in: f(t) = t^2, so f(0.5) = 0.25 — below the linear 0.5.
    auto id = IREntity::createEntity(
        C_LocalTransform{},
        C_RotationTarget{
            axis, 0.0f, IRMath::kHalfPi, 0.5f, 0.0f, 1.0f,
            IREasingFunctions::kQuadraticEaseIn
        }
    );

    tick();
    expectSameRotation(
        IREntity::getComponent<C_WorldTransform>(id).rotation_,
        expectedRotation(axis, IRMath::kHalfPi * 0.25f)
    );
}

TEST_F(RotationTargetTest, ZeroAxisIsNoOp) {
    // Seed a known non-identity local rotation; a zero-axis target must leave
    // it untouched (no NaN quaternion leaking into the world transform).
    const IRMath::vec4 seed = IRMath::quatAxisAngle(IRMath::vec3(0, 0, 1), IRMath::kHalfPi);
    auto id = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0), seed},
        C_RotationTarget{IRMath::vec3(0, 0, 0), 0.0f, IRMath::kPi, 1.0f}
    );

    tick();
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(id).rotation_, seed);
}

TEST_F(RotationTargetTest, ComposesUnderParentRotation) {
    const IRMath::vec3 axis(0, 0, 1);
    const float parentAngle = IRMath::kHalfPi;
    const float childAngle = IRMath::kHalfPi; // input 1.0 → maxAngle

    // Parent carries a fixed local rotation (no rotation-target driver).
    auto parent = IREntity::createEntity(
        C_LocalTransform{IRMath::vec3(0), IRMath::quatAxisAngle(axis, parentAngle)}
    );
    auto child = IREntity::createEntity(
        C_LocalTransform{}, C_RotationTarget{axis, 0.0f, childAngle, 1.0f}
    );
    IREntity::setParent(child, parent);

    tick();

    // World = parent_world * child_local (Hamilton, column-vector convention).
    const IRMath::vec4 expected = IRMath::quatMul(
        IRMath::quatAxisAngle(axis, parentAngle),
        IRMath::quatAxisAngle(axis, childAngle)
    );
    expectSameRotation(IREntity::getComponent<C_WorldTransform>(child).rotation_, expected);
}

} // namespace
