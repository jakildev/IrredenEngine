#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_time.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_modifiers.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/components/component_gizmo_handle.hpp>
#include <irreden/render/gizmo.hpp>
#include <irreden/render/systems/system_gizmo_screen_space_size.hpp>
#include <irreden/update/systems/system_propagate_transform.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <vector>

namespace {

using IRComponents::C_GizmoHandle;
using IRComponents::C_LocalTransform;
using IRComponents::C_Modifiers;
using IRComponents::C_ShapeDescriptor;
using IRComponents::C_WorldTransform;
using IRMath::vec2;
using IRMath::vec3;

constexpr float kEps = 1e-3f;
constexpr float kZoom = 4.0f;
// At kZoom a ring's own extent and an arrow head's fall on either side of a
// whole view-depth step at this clearance, so a length taken per handle
// would not move the gizmo as a unit.
constexpr float kClearance = 15.0f;

// One handle of an anchor after a frame: where PROPAGATE_TRANSFORM put it and
// the radius of the ball that bounds its shape.
struct PlacedHandle {
    IRComponents::GizmoKind kind_;
    IRComponents::GizmoAxis axis_;
    IRMath::SDF::ShapeType shapeType_;
    vec3 world_;
    float radius_;
};

// Drives GIZMO_SCREEN_SPACE_SIZE through `beginFrame`, which takes the zoom
// the registered `beginTick` reads from the render manager, then runs the
// real PROPAGATE_TRANSFORM so the assertions read resolved world transforms.
class GizmoViewClearanceTest : public testing::Test {
  protected:
    GizmoViewClearanceTest() {
        m_system_manager.registerPipeline(
            IRTime::Events::UPDATE,
            {IRSystem::createSystem<IRSystem::PROPAGATE_TRANSFORM>()}
        );
        IREntity::setName(IREntity::createEntity(), "camera");
    }

    void runFrame() {
        m_sizing.beginFrame(kZoom);
        IRSystem::executeQuery<C_GizmoHandle, C_ShapeDescriptor, C_LocalTransform>(
            [this](C_GizmoHandle &handle, C_ShapeDescriptor &shape, C_LocalTransform &local) {
                m_sizing.tick(handle, shape, local);
            }
        );
        m_system_manager.executePipeline(IRTime::Events::UPDATE);
    }

    static IREntity::EntityId makeAnchor(const C_LocalTransform &transform, float viewClearance) {
        const IREntity::EntityId anchor = IREntity::createEntity(transform);
        IRPrefab::Gizmo::createTranslateGizmoForAnchor(anchor, viewClearance);
        IRPrefab::Gizmo::createRotateGizmoForAnchor(anchor, viewClearance);
        return anchor;
    }

    static std::vector<PlacedHandle> placedHandles(IREntity::EntityId anchor) {
        std::vector<PlacedHandle> placed;
        IREntity::forEachComponent<C_GizmoHandle>([&](IREntity::EntityId id,
                                                      C_GizmoHandle &handle) {
            if (handle.anchorEntity_ != anchor) {
                return;
            }
            const auto &shape = IREntity::getComponent<C_ShapeDescriptor>(id);
            placed.push_back(
                PlacedHandle{
                    handle.kind_,
                    handle.axis_,
                    shape.shapeType_,
                    IREntity::getComponent<C_WorldTransform>(id).translation_,
                    IRMath::SDF::boundingRadius(shape.shapeType_, shape.params_),
                }
            );
        });
        return placed;
    }

    static const PlacedHandle *
    twinOf(const std::vector<PlacedHandle> &handles, const PlacedHandle &wanted) {
        for (const PlacedHandle &handle : handles) {
            if (handle.kind_ == wanted.kind_ && handle.axis_ == wanted.axis_ &&
                handle.shapeType_ == wanted.shapeType_) {
                return &handle;
            }
        }
        return nullptr;
    }

    // The far direction of the view at `yaw`: the yawed iso projection does
    // not change along it.
    static vec3 viewDepthAxis(float yaw) {
        return IRMath::normalize(IRMath::isoPixelToPos3DYawed(vec2(0.0f), 1.0f, yaw));
    }

    // Every cleared handle sits on the pixel of its zero-clearance twin, its
    // whole bounding ball is nearer than the clearance sphere, and the gizmo
    // moved as a unit: one displacement for every handle.
    static void expectClearedInFrontOnTheSamePixels(
        IREntity::EntityId plain, IREntity::EntityId cleared, vec3 anchorWorld, float yaw
    ) {
        const std::vector<PlacedHandle> plainHandles = placedHandles(plain);
        const std::vector<PlacedHandle> clearedHandles = placedHandles(cleared);
        ASSERT_EQ(plainHandles.size(), 9u);
        ASSERT_EQ(clearedHandles.size(), 9u);

        const vec3 depthAxis = viewDepthAxis(yaw);
        const PlacedHandle *firstTwin = twinOf(plainHandles, clearedHandles.front());
        ASSERT_NE(firstTwin, nullptr);
        const vec3 gizmoDisplacement = clearedHandles.front().world_ - firstTwin->world_;
        for (const PlacedHandle &handle : clearedHandles) {
            const PlacedHandle *twin = twinOf(plainHandles, handle);
            ASSERT_NE(twin, nullptr);

            const vec3 displacement = handle.world_ - twin->world_;
            EXPECT_NEAR(displacement.x, gizmoDisplacement.x, kEps) << "yaw " << yaw;
            EXPECT_NEAR(displacement.y, gizmoDisplacement.y, kEps) << "yaw " << yaw;
            EXPECT_NEAR(displacement.z, gizmoDisplacement.z, kEps) << "yaw " << yaw;

            const vec2 clearedIso = IRMath::pos3DtoPos2DIsoYawed(handle.world_, yaw);
            const vec2 plainIso = IRMath::pos3DtoPos2DIsoYawed(twin->world_, yaw);
            EXPECT_NEAR(clearedIso.x, plainIso.x, kEps) << "yaw " << yaw;
            EXPECT_NEAR(clearedIso.y, plainIso.y, kEps) << "yaw " << yaw;

            EXPECT_LT(
                IRMath::pos3DtoDistanceYawed(handle.world_, yaw),
                IRMath::pos3DtoDistanceYawed(twin->world_, yaw)
            ) << "yaw "
              << yaw;

            const float farthestPointDepth =
                IRMath::dot(handle.world_ - anchorWorld, depthAxis) + handle.radius_;
            EXPECT_LE(farthestPointDepth, -kClearance + kEps) << "yaw " << yaw;
        }
    }

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
    IRSystem::System<IRSystem::GIZMO_SCREEN_SPACE_SIZE> m_sizing;
};

// Neither yaw is a cardinal, and their nearest cardinals differ (0 and a
// quarter turn), so a displacement taken at yaw 0 or at the snapped raster
// yaw leaves the twin's pixel at both.
TEST_F(GizmoViewClearanceTest, AClearedHandleKeepsItsPixelsAtANonCardinalYaw) {
    C_LocalTransform transform{vec3(3.0f, -2.0f, 5.0f)};
    transform.rotation_ = IRMath::quatAxisAngle(IRMath::normalize(vec3(1.0f, 2.0f, -1.0f)), 0.9f);
    const IREntity::EntityId plain = makeAnchor(transform, 0.0f);
    const IREntity::EntityId cleared = makeAnchor(transform, kClearance);

    for (const float yaw : {0.6f, 2.2f}) {
        IRPrefab::Camera::setYaw(yaw);
        runFrame();
        expectClearedInFrontOnTheSamePixels(plain, cleared, transform.translation_, yaw);
    }
}

// A handle thinner than a cell draws only where it meets the voxel lattice's
// sample points, so at a cardinal yaw the displacement must be a whole number
// of units on every axis. The camera's yaw reads back from its quaternion a
// few ulps off the quarter turn; only the exact cardinal form survives that.
TEST_F(GizmoViewClearanceTest, ACardinalYawMovesAHandleByWholeLatticeSteps) {
    const C_LocalTransform transform{vec3(-0.5f, -0.5f, -4.5f)};
    const IREntity::EntityId plain = makeAnchor(transform, 0.0f);
    const IREntity::EntityId cleared = makeAnchor(transform, kClearance);

    for (const float yaw : {0.0f, IRMath::kHalfPi, -IRMath::kHalfPi}) {
        IRPrefab::Camera::setYaw(yaw);
        runFrame();
        expectClearedInFrontOnTheSamePixels(plain, cleared, transform.translation_, yaw);

        const std::vector<PlacedHandle> plainHandles = placedHandles(plain);
        for (const PlacedHandle &handle : placedHandles(cleared)) {
            const PlacedHandle *twin = twinOf(plainHandles, handle);
            ASSERT_NE(twin, nullptr);
            const vec3 displacement = handle.world_ - twin->world_;
            EXPECT_LT(displacement.z, 0.0f) << "yaw " << yaw;
            EXPECT_EQ(displacement.z, IRMath::round(displacement.z)) << "yaw " << yaw;
            EXPECT_EQ(IRMath::abs(displacement.x), -displacement.z) << "yaw " << yaw;
            EXPECT_EQ(IRMath::abs(displacement.y), -displacement.z) << "yaw " << yaw;
        }
    }
}

// The anchor's world scale stretches a child handle's offset. With the X
// arrow pointing away from the camera at four times its authored reach, a
// displacement sized for a unit-scale anchor leaves the arrow's far end
// inside the sphere.
TEST_F(GizmoViewClearanceTest, AScaledAnchorKeepsItsHandlesOutsideTheSphere) {
    C_LocalTransform transform{vec3(-4.0f, 6.0f, 1.0f)};
    transform.scale_ = vec3(4.0f);
    const IREntity::EntityId plain = makeAnchor(transform, 0.0f);
    const IREntity::EntityId cleared = makeAnchor(transform, kClearance);

    // The sizing pass reads the anchor's world scale as PROPAGATE_TRANSFORM
    // last resolved it, so the first frame only seeds it.
    runFrame();
    runFrame();

    expectClearedInFrontOnTheSamePixels(plain, cleared, transform.translation_, 0.0f);
}

TEST_F(GizmoViewClearanceTest, AZeroClearanceHandleIsPlacedByItsParentChainAlone) {
    C_LocalTransform transform{vec3(3.0f, -2.0f, 5.0f)};
    transform.rotation_ = IRMath::quatAxisAngle(vec3(0.0f, 0.0f, 1.0f), 0.7f);
    const IREntity::EntityId anchor = makeAnchor(transform, 0.0f);

    IRPrefab::Camera::setYaw(0.6f);
    runFrame();

    int handleCount = 0;
    IREntity::forEachComponent<C_GizmoHandle>([&](IREntity::EntityId id, C_GizmoHandle &handle) {
        ++handleCount;
        EXPECT_FALSE(IREntity::getComponentOptional<C_Modifiers>(id).has_value());
        const vec3 expected =
            transform.translation_ +
            IRMath::rotateVectorByQuat(handle.referenceLocalPos_ / kZoom, transform.rotation_);
        const vec3 world = IREntity::getComponent<C_WorldTransform>(id).translation_;
        EXPECT_NEAR(world.x, expected.x, kEps);
        EXPECT_NEAR(world.y, expected.y, kEps);
        EXPECT_NEAR(world.z, expected.z, kEps);
        EXPECT_EQ(handle.anchorEntity_, anchor);
    });
    EXPECT_EQ(handleCount, 9);
}

} // namespace
