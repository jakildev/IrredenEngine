#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_rotation_mode.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/canvas_pose.hpp>
#include <irreden/render/components/component_canvas_local_rotation.hpp>
#include <irreden/render/components/component_entity_canvas.hpp>
#include <irreden/render/systems/system_propagate_canvas_rotation.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>

// PROPAGATE_CANVAS_ROTATION bakes a detached entity's rotation onto its canvas.
// The rotation it bakes is the entity's WORLD rotation: a detached child under
// a rotating parent already follows the parent's translation through the
// composite, and must turn with it too.
//
// Headless: the canvas is a plain entity carrying only the pose component the
// system writes, and the camera is a named entity with a transform — the two
// things the system reads through the entity manager.

namespace {

using IRComponents::C_CanvasLocalRotation;
using IRComponents::C_EntityCanvas;
using IRComponents::C_LocalTransform;
using IRComponents::C_RotationMode;
using IRComponents::C_WorldTransform;
using IRComponents::RotationMode;
using IRMath::vec3;
using IRMath::vec4;

constexpr float kEps = 1e-5f;

class CanvasRotation : public testing::Test {
  protected:
    CanvasRotation() {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>(),
             IRSystem::createSystem<IRSystem::PROPAGATE_CANVAS_ROTATION>()}
        );
        m_camera = IREntity::createEntity();
        IREntity::setName(m_camera, "camera");
    }

    void tick() {
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    void setCameraRotation(vec4 rotation) {
        IREntity::getComponent<C_LocalTransform>(m_camera).rotation_ = rotation;
    }

    // A detached entity plus the canvas entity it owns.
    struct Detached {
        IREntity::EntityId entity_;
        IREntity::EntityId canvas_;
    };

    static Detached makeDetached(
        const C_LocalTransform &local, RotationMode mode = RotationMode::DETACHED_REVOXELIZE
    ) {
        const IREntity::EntityId canvas = IREntity::createEntity(C_CanvasLocalRotation{});
        const IREntity::EntityId entity = IREntity::createEntity(
            local,
            C_RotationMode{mode},
            C_EntityCanvas{canvas, IRMath::ivec2(64, 64)}
        );
        return {entity, canvas};
    }

    static const C_CanvasLocalRotation &poseOf(const Detached &detached) {
        return IREntity::getComponent<C_CanvasLocalRotation>(detached.canvas_);
    }

    static void expectQuatNear(vec4 actual, vec4 expected) {
        EXPECT_NEAR(actual.x, expected.x, kEps);
        EXPECT_NEAR(actual.y, expected.y, kEps);
        EXPECT_NEAR(actual.z, expected.z, kEps);
        EXPECT_NEAR(actual.w, expected.w, kEps);
    }

    static float quatDistance(vec4 a, vec4 b) {
        return IRMath::length(a - b);
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IREntity::EntityId m_camera = IREntity::kNullEntity;
};

TEST_F(CanvasRotation, ChildComposesParentRotation) {
    const vec4 parentRotation = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.7f);
    const vec4 childRotation =
        IRMath::quatAxisAngle(IRMath::normalize(vec3(1.0f, 0.4f, 0.0f)), 0.5f);
    const IREntity::EntityId parent =
        IREntity::createEntity(C_LocalTransform{vec3(4.0f, 0.0f, 0.0f), parentRotation});
    const Detached child = makeDetached(C_LocalTransform{vec3(2.0f, 0.0f, 0.0f), childRotation});
    IREntity::setParent(child.entity_, parent);

    tick();

    const vec4 worldRotation = IRMath::quatMul(parentRotation, childRotation);
    expectQuatNear(poseOf(child).rotation_, worldRotation);
    // Not vacuous: the local-only composition this replaces is a different
    // quaternion for this pair, so the assertion above cannot pass on it.
    EXPECT_GT(quatDistance(worldRotation, childRotation), 0.1f);
}

TEST_F(CanvasRotation, UnparentedEntityBakesItsOwnRotation) {
    const vec4 rotation = IRMath::quatAxisAngle(IRMath::normalize(vec3(0.3f, 1.0f, 0.2f)), 1.1f);
    const Detached detached = makeDetached(C_LocalTransform{vec3(0.0f), rotation});

    tick();

    expectQuatNear(poseOf(detached).rotation_, rotation);
}

TEST_F(CanvasRotation, CameraBasisIsRemovedFromTheWorldRotation) {
    const vec4 cameraRotation = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.9f);
    setCameraRotation(cameraRotation);
    const vec4 parentRotation = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.4f);
    const vec4 childRotation = IRMath::quatAxisAngle(vec3(1.0f, 0.0f, 0.0f), 0.3f);
    const IREntity::EntityId parent =
        IREntity::createEntity(C_LocalTransform{vec3(0.0f), parentRotation});
    const Detached child = makeDetached(C_LocalTransform{vec3(0.0f), childRotation});
    IREntity::setParent(child.entity_, parent);

    tick();

    expectQuatNear(
        poseOf(child).rotation_,
        IRMath::quatMul(
            IRMath::quatInverse(cameraRotation),
            IRMath::quatMul(parentRotation, childRotation)
        )
    );
}

TEST_F(CanvasRotation, StampsTheOwnersPlacementOnTheCanvas) {
    const IREntity::EntityId parent =
        IREntity::createEntity(C_LocalTransform{vec3(10.0f, -3.0f, 2.0f)});
    const Detached child = makeDetached(C_LocalTransform{vec3(0.6f, 0.5f, -0.5f)});
    IREntity::setParent(child.entity_, parent);

    tick();

    const C_CanvasLocalRotation &pose = poseOf(child);
    const vec3 world(10.6f, -2.5f, 1.5f);
    EXPECT_NEAR(pose.ownerWorldTranslation_.x, world.x, kEps);
    EXPECT_NEAR(pose.ownerWorldTranslation_.y, world.y, kEps);
    EXPECT_NEAR(pose.ownerWorldTranslation_.z, world.z, kEps);
    // The world cell is the half-up rounding the composite depth uses.
    EXPECT_EQ(pose.worldCellOffset_, vec3(11.0f, -2.0f, 2.0f));
    EXPECT_TRUE(pose.reVoxelize_);
    EXPECT_TRUE(pose.worldPlaced_);
}

TEST_F(CanvasRotation, GridEntityLeavesTheCanvasUnposed) {
    const Detached grid = makeDetached(
        C_LocalTransform{vec3(0.0f), IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.5f)},
        RotationMode::GRID
    );

    tick();

    EXPECT_FALSE(poseOf(grid).isDetached());
}

TEST_F(CanvasRotation, ForwardScatterModeClearsTheReVoxelizeRoute) {
    const Detached detached = makeDetached(C_LocalTransform{vec3(0.0f)}, RotationMode::DETACHED);

    tick();

    EXPECT_TRUE(poseOf(detached).isDetached());
    EXPECT_FALSE(poseOf(detached).reVoxelize_);
}

TEST(CanvasPose, OffsetIsMeasuredFromTheOwnerInTheCanvasFrame) {
    const vec4 cameraInverse =
        IRMath::quatInverse(IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), IRMath::kHalfPi));
    const vec3 offset = IRPrefab::CanvasPose::canvasOffset(
        cameraInverse,
        vec3(5.0f, 1.0f, 3.0f),
        vec3(2.0f, 1.0f, 3.0f)
    );
    // (3, 0, 0) seen from a camera yawed +90° lies along -Y of the canvas.
    EXPECT_NEAR(offset.x, 0.0f, kEps);
    EXPECT_NEAR(offset.y, -3.0f, kEps);
    EXPECT_NEAR(offset.z, 0.0f, kEps);
}

} // namespace
