#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <vector>

namespace {

using IRComponents::C_GizmoHandle;
using IRComponents::C_LocalTransform;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_WorldTransform;
using IRComponents::GizmoAxis;
using IRComponents::GizmoKind;
using IRMath::vec3;
using IRMath::vec4;

constexpr float kEps = 1e-4f;

struct Ring {
    GizmoAxis axis_;
    vec3 axisUnit_;
    vec4 localRotation_;
    vec4 worldRotation_;
    vec4 params_;
};

class GizmoRingOrientationTest : public testing::Test {
  protected:
    GizmoRingOrientationTest() {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
        );
    }

    std::vector<Ring> ringsOf(IREntity::EntityId anchor) {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
        std::vector<Ring> rings;
        IREntity::forEachComponent<C_GizmoHandle>([&](IREntity::EntityId id,
                                                      C_GizmoHandle &handle) {
            if (handle.anchorEntity_ != anchor || handle.kind_ != GizmoKind::ROTATE_RING) {
                return;
            }
            const auto &shape = IREntity::getComponent<C_ShapeDescriptor>(id);
            EXPECT_EQ(shape.shapeType_, IRMath::SDF::ShapeType::TORUS);
            rings.push_back(
                Ring{
                    handle.axis_,
                    IRPrefab::Gizmo::detail::axisOffset(handle.axis_, 1.0f),
                    IREntity::getComponent<C_LocalTransform>(id).rotation_,
                    IREntity::getComponent<C_WorldTransform>(id).rotation_,
                    shape.params_,
                }
            );
        });
        return rings;
    }

    static void expectNear(vec3 actual, vec3 expected) {
        EXPECT_NEAR(actual.x, expected.x, kEps);
        EXPECT_NEAR(actual.y, expected.y, kEps);
        EXPECT_NEAR(actual.z, expected.z, kEps);
    }

    // Each ring's axis of revolution, the torus's shape-local +Z, is its
    // handle's axis, and the three rings are mutually perpendicular.
    static void expectOneRingPerAxis(const std::vector<Ring> &rings) {
        ASSERT_EQ(rings.size(), 3u);
        const vec3 torusAxis(0.0f, 0.0f, 1.0f);
        for (const Ring &ring : rings) {
            SCOPED_TRACE(static_cast<int>(ring.axis_));
            expectNear(IRMath::rotateVectorByQuat(torusAxis, ring.localRotation_), ring.axisUnit_);
        }
        for (std::size_t a = 0; a < rings.size(); ++a) {
            for (std::size_t b = a + 1; b < rings.size(); ++b) {
                EXPECT_NE(rings[a].axis_, rings[b].axis_);
                EXPECT_NEAR(
                    IRMath::dot(
                        IRMath::rotateVectorByQuat(torusAxis, rings[a].localRotation_),
                        IRMath::rotateVectorByQuat(torusAxis, rings[b].localRotation_)
                    ),
                    0.0f,
                    kEps
                );
            }
        }
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(GizmoRingOrientationTest, EachRingOfAGroupGizmoTurnsAboutItsOwnAxis) {
    expectOneRingPerAxis(ringsOf(IRPrefab::Gizmo::createRotateGizmo()));
}

TEST_F(GizmoRingOrientationTest, EachRingOfAnAnchorGizmoTurnsAboutItsOwnAxis) {
    const IREntity::EntityId anchor = IREntity::createEntity(C_LocalTransform{vec3(0.0f)});
    IRPrefab::Gizmo::createRotateGizmoForAnchor(anchor, 15.0f);
    expectOneRingPerAxis(ringsOf(anchor));
}

// The surface a ring draws: a point on the ring's circle in the plane normal
// to its axis is inside the torus, and the same distance along the axis is
// outside it.
TEST_F(GizmoRingOrientationTest, ARingLiesInThePlaneNormalToItsAxis) {
    const std::vector<Ring> rings = ringsOf(IRPrefab::Gizmo::createRotateGizmo());
    ASSERT_EQ(rings.size(), 3u);
    for (const Ring &ring : rings) {
        SCOPED_TRACE(static_cast<int>(ring.axis_));
        const float radius = ring.params_.x;
        const vec4 toShape = IRMath::quatInverse(ring.localRotation_);
        const auto distanceAt = [&](vec3 anchorLocal) {
            return IRMath::SDF::evaluate(
                IRMath::rotateVectorByQuat(anchorLocal, toShape),
                IRMath::SDF::ShapeType::TORUS,
                ring.params_
            );
        };
        for (const Ring &other : rings) {
            const float distance = distanceAt(other.axisUnit_ * radius);
            if (other.axis_ == ring.axis_) {
                EXPECT_GT(distance, 0.0f);
            } else {
                EXPECT_LT(distance, 0.0f);
            }
        }
    }
}

// A ring is a child of its anchor, so on a turned anchor it names the
// anchor's local axis, which is the axis a drag on it turns.
TEST_F(GizmoRingOrientationTest, ARingFollowsItsAnchorsRotation) {
    const vec4 anchorRotation = IRMath::quatAxisAngle(vec3(1.0f, 2.0f, -0.5f), 0.7f);
    const IREntity::EntityId anchor =
        IREntity::createEntity(C_LocalTransform{vec3(3.0f, -2.0f, 5.0f), anchorRotation});
    IRPrefab::Gizmo::createRotateGizmoForAnchor(anchor);

    const std::vector<Ring> rings = ringsOf(anchor);
    ASSERT_EQ(rings.size(), 3u);
    for (const Ring &ring : rings) {
        SCOPED_TRACE(static_cast<int>(ring.axis_));
        expectNear(
            IRMath::rotateVectorByQuat(vec3(0.0f, 0.0f, 1.0f), ring.worldRotation_),
            IRMath::rotateVectorByQuat(ring.axisUnit_, anchorRotation)
        );
    }
}

} // namespace
