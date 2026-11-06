#include <gtest/gtest.h>

#include <irreden/ir_entity.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/entity/entity_manager.hpp>
#include <irreden/render/camera_controls.hpp>

namespace {

class CameraMouseRotateResetTest : public testing::Test {
  protected:
    CameraMouseRotateResetTest()
        : m_entity_manager{}
        , m_system_manager{} {}

    IREntity::EntityManager m_entity_manager;
    IRSystem::SystemManager m_system_manager;
};

TEST_F(CameraMouseRotateResetTest, PivotIndicatorDangleIsClearedAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(sysId);
    ASSERT_NE(params, nullptr);

    // Stand in for IRPrefab::CursorPivot::createIndicator()'s lazy spawn
    // without needing a full drag — any non-persistent entity dangles
    // identically.
    params->pivotIndicator_ = IREntity::createEntity();
    ASSERT_NE(params->pivotIndicator_, IREntity::kNullEntity);
    params->dragging_ = true;

    IREntity::resetGameplay();
    EXPECT_FALSE(params->dragging_);

    EXPECT_EQ(params->pivotIndicator_, IREntity::kNullEntity)
        << "pre-destroy hook must null the cached id so the next drag's "
           "lazy-respawn guard rebuilds the indicator instead of reaching "
           "through a dead id";
}

TEST_F(CameraMouseRotateResetTest, UnrelatedEntityDestructionLeavesPivotIndicatorIntact) {
    const auto sysId = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(sysId);
    ASSERT_NE(params, nullptr);

    const auto indicator = IREntity::createEntity();
    params->pivotIndicator_ = indicator;
    const auto other = IREntity::createEntity();

    // Direct EntityManager::destroyEntity, not the deferred IREntity:: free
    // function — the pre-destroy hook fires synchronously inside the
    // immediate destroy path, and this test needs that ordering guarantee.
    m_entity_manager.destroyEntity(other);

    EXPECT_EQ(params->pivotIndicator_, indicator)
        << "the hook must only null the cached id on an exact match, not on any destruction";
}

TEST_F(CameraMouseRotateResetTest, KeyDragIndicatorIsClearedAcrossResetGameplay) {
    const auto sysId = IRSystem::createSystem<IRSystem::CAMERA_KEY_DRAG_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(sysId);
    params->pivotIndicator_ = IREntity::createEntity();
    params->cursorPivot_ = true;
    params->dragging_ = true;
    IREntity::resetGameplay();
    EXPECT_EQ(params->pivotIndicator_, IREntity::kNullEntity);
    EXPECT_FALSE(params->cursorPivot_);
    EXPECT_FALSE(params->dragging_);
}

TEST_F(CameraMouseRotateResetTest, HiddenCenterMarkerCancelsDragAcrossReset) {
    const auto id = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(id);
    params->pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
    params->cursorPivot_ = false;
    params->dragging_ = true;
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_ShapeDescriptor>(params->pivotIndicator_).flags_ &
            IRRender::SHAPE_FLAG_VISIBLE,
        0u
    );
    IREntity::resetGameplay();
    EXPECT_EQ(params->pivotIndicator_, IREntity::kNullEntity);
    EXPECT_FALSE(params->dragging_);
}

TEST_F(CameraMouseRotateResetTest, MissingControlsMakeSettingsNoOp) {
    IRPrefab::Camera::setCursorPivotByDefault(true);
    IRPrefab::Camera::setCursorPivotOptions({.floorHeight_ = 14.5f});
    EXPECT_FALSE(IRPrefab::Camera::cursorPivotByDefault());
}

TEST_F(CameraMouseRotateResetTest, KeyOnlyControlsExposeDefaultAndOptions) {
    const auto id = IRSystem::createSystem<IRSystem::CAMERA_KEY_DRAG_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(id);
    EXPECT_TRUE(IRPrefab::Camera::cursorPivotByDefault());
    IRPrefab::Camera::setCursorPivotByDefault(false);
    EXPECT_FALSE(params->latchByDefault_);
    EXPECT_FALSE(IRPrefab::Camera::cursorPivotByDefault());
    IRPrefab::Camera::setCursorPivotOptions({.floorHeight_ = 14.5f});
    EXPECT_EQ(params->pivotOptions_.floorHeight_, 14.5f);
}

TEST_F(CameraMouseRotateResetTest, MouseOnlyControlsExposeDefaultAndOptions) {
    const auto id = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(id);
    EXPECT_TRUE(IRPrefab::Camera::cursorPivotByDefault());
    IRPrefab::Camera::setCursorPivotByDefault(false);
    EXPECT_FALSE(params->latchByDefault_);
    EXPECT_FALSE(IRPrefab::Camera::cursorPivotByDefault());
    IRPrefab::Camera::setCursorPivotOptions({.floorHeight_ = -6.5f});
    EXPECT_EQ(params->pivotOptions_.floorHeight_, -6.5f);
}

TEST_F(CameraMouseRotateResetTest, BothControlsReceivePolicyAndResolver) {
    const auto mouseId = IRSystem::createSystem<IRSystem::CAMERA_MOUSE_ROTATE>();
    const auto keyId = IRSystem::createSystem<IRSystem::CAMERA_KEY_DRAG_ROTATE>();
    auto *mouse =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_MOUSE_ROTATE>>(mouseId);
    auto *key =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(keyId);
    EXPECT_TRUE(mouse->latchByDefault_);
    EXPECT_TRUE(key->latchByDefault_);
    IRPrefab::Camera::setCursorPivotByDefault(false);
    EXPECT_FALSE(mouse->latchByDefault_);
    EXPECT_FALSE(key->latchByDefault_);
    IRPrefab::Camera::setCursorPivotByDefault(true);
    EXPECT_TRUE(mouse->latchByDefault_);
    EXPECT_TRUE(key->latchByDefault_);
    int calls = 0;
    const auto excluded = IREntity::createEntity();
    IRPrefab::Camera::setCursorPivotOptions({.resolveSurface_ = [&](IREntity::EntityId id) {
        EXPECT_EQ(id, excluded);
        ++calls;
        return std::optional<IRMath::vec3>(IRMath::vec3(7, 8, -9));
    }});
    EXPECT_EQ(
        IRPrefab::CursorPivot::resolveFocusWorld(excluded, mouse->pivotOptions_),
        IRMath::vec3(7, 8, -9)
    );
    EXPECT_EQ(
        IRPrefab::CursorPivot::resolveFocusWorld(excluded, key->pivotOptions_),
        IRMath::vec3(7, 8, -9)
    );
    EXPECT_EQ(calls, 2);
}

TEST_F(CameraMouseRotateResetTest, ReleaseHidesMarkerAndKeepsLatchedPoint) {
    IRPrefab::CursorPivot::OrbitAnchor anchor;
    anchor.pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
    anchor.cursorPivot_ = true;
    anchor.focusWorld_ = IRMath::vec3(7, -4, 12);
    IRPrefab::CursorPivot::showIndicator(anchor.pivotIndicator_, anchor.focusWorld_);
    auto &marker = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(anchor.pivotIndicator_);
    ASSERT_NE(marker.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    anchor.end();
    EXPECT_EQ(marker.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    EXPECT_EQ(anchor.focusWorld_, IRMath::vec3(7, -4, 12));
    EXPECT_FALSE(anchor.cursorPivot_);
}

TEST_F(CameraMouseRotateResetTest, IndicatorAcceptsPerGestureRadiusAndPreservesDefault) {
    const auto indicator = IRPrefab::CursorPivot::createIndicator();
    const IRMath::vec3 focus(2, -3, -4);
    IRPrefab::CursorPivot::showIndicator(indicator, focus, 0.1125f);
    auto &marker = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(indicator);
    EXPECT_EQ(marker.params_, IRMath::vec4(0.1125f, 0.1125f, 0.1125f, 0.0f));
    EXPECT_NE(marker.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_WorldTransform>(indicator).translation_,
        focus
    );
    IRPrefab::CursorPivot::showIndicator(indicator, focus);
    const float radius = IRPrefab::CursorPivot::kIndicatorRadius;
    EXPECT_EQ(marker.params_, IRMath::vec4(radius, radius, radius, 0.0f));
}

TEST_F(CameraMouseRotateResetTest, ScreenMarkerReleaseHidesBothRepresentations) {
    IRPrefab::CursorPivot::OrbitAnchor anchor;
    anchor.pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
    anchor.focusWorld_ = IRMath::vec3(7, -4, 12);
    const IRMath::vec2 press(381, 219);
    IRPrefab::CursorPivot::showIndicator(anchor.pivotIndicator_, anchor.focusWorld_);
    IRPrefab::CursorPivot::showScreenIndicator(anchor.pivotIndicator_, press);
    auto &screen =
        IREntity::getComponent<IRComponents::C_ScreenSpaceMarker>(anchor.pivotIndicator_);
    auto &shape = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(anchor.pivotIndicator_);
    EXPECT_TRUE(screen.visible_);
    EXPECT_EQ(screen.windowPoint_, press);
    EXPECT_FLOAT_EQ(screen.radiusPoints_, 6.0f);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    EXPECT_EQ(anchor.focusWorld_, IRMath::vec3(7, -4, 12));
    anchor.end();
    EXPECT_FALSE(screen.visible_);
    EXPECT_EQ(shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
    IRPrefab::CursorPivot::showScreenIndicator(anchor.pivotIndicator_, press);
    IRPrefab::CursorPivot::showIndicator(anchor.pivotIndicator_, anchor.focusWorld_);
    EXPECT_FALSE(screen.visible_);
    EXPECT_NE(shape.flags_ & IRRender::SHAPE_FLAG_VISIBLE, 0u);
}

TEST_F(CameraMouseRotateResetTest, ScreenMarkerIsDestroyedAcrossGameplayReset) {
    const auto id = IRSystem::createSystem<IRSystem::CAMERA_KEY_DRAG_ROTATE>();
    auto *params =
        m_system_manager.getSystemParams<IRSystem::System<IRSystem::CAMERA_KEY_DRAG_ROTATE>>(id);
    params->pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
    const auto indicator = params->pivotIndicator_;
    params->dragging_ = true;
    IRPrefab::CursorPivot::showScreenIndicator(indicator, IRMath::vec2(20, 30));
    IREntity::resetGameplay();
    EXPECT_FALSE(IREntity::entityExists(indicator));
    EXPECT_EQ(params->pivotIndicator_, IREntity::kNullEntity);
    EXPECT_FALSE(params->dragging_);
}

TEST(CursorPivotMath, ReanchorKeepsAllWorldPointsFixedAtEveryYaw) {
    using namespace IRMath;
    const vec2 raw(-45.5f, 27.25f);
    const vec2 offset(11.25f, -6.5f);
    const vec3 oldFocus(13, -4, -7), nextFocus(-9, 12, 3);
    for (const float degrees : {0.0f, 30.0f, 67.0f, 90.0f, 120.0f, 180.0f, 240.0f, 270.0f}) {
        const float yaw = degrees * kPi / 180.0f;
        for (const bool originMode : {false, true}) {
            const vec2 before =
                originMode ? raw : cameraYawPivotOffset(raw + offset, oldFocus, yaw);
            const vec2 switched = cameraYawPivotOffset(raw + offset, nextFocus, yaw);
            const vec2 compensated =
                IRPrefab::CursorPivot::detail::preserveViewCamera(raw, before, switched);
            const vec2 after = cameraYawPivotOffset(compensated + offset, nextFocus, yaw);
            EXPECT_NEAR(before.x, after.x, 0.0001f) << degrees;
            EXPECT_NEAR(before.y, after.y, 0.0001f) << degrees;
            for (const vec3 point : {oldFocus, nextFocus, vec3(90, -11, -18)}) {
                const vec2 projection = pos3DtoPos2DIsoYawed(point, yaw);
                EXPECT_NEAR((projection + before).x, (projection + after).x, 0.0001f);
                EXPECT_NEAR((projection + before).y, (projection + after).y, 0.0001f);
            }
            const float nextYaw = yaw + 0.17f;
            const vec2 pinned = pos3DtoPos2DIsoYawed(nextFocus, nextYaw) +
                                cameraYawPivotOffset(compensated + offset, nextFocus, nextYaw);
            const vec2 press = pos3DtoPos2DIsoYawed(nextFocus, yaw) + before;
            EXPECT_NEAR(pinned.x, press.x, 0.0001f);
            EXPECT_NEAR(pinned.y, press.y, 0.0001f);
        }
    }
}

TEST(CursorPivotMath, MarkerClearancePreservesProjectionAndActualFocusAtEveryYaw) {
    using namespace IRMath;
    const vec3 focus(7.25f, -4.5f, -12.0f);
    for (const float degrees : {0.0f, 30.0f, 67.0f, 90.0f, 120.0f, 180.0f, 240.0f, 270.0f}) {
        const float yaw = degrees * kPi / 180.0f;
        for (const float radius : {0.1125f, 0.225f, 0.9f, 7.2f}) {
            const auto marker =
                IRPrefab::CursorPivot::detail::indicatorWorldPosition(focus, yaw, radius);
            const auto projectedFocus = pos3DtoPos2DIsoYawed(focus, yaw);
            const auto projectedMarker = pos3DtoPos2DIsoYawed(marker, yaw);
            EXPECT_NEAR(projectedMarker.x, projectedFocus.x, 0.0001f);
            EXPECT_NEAR(projectedMarker.y, projectedFocus.y, 0.0001f);
            const auto depthRay = isoPixelToPos3DYawed(vec2(0.0f), 3.0f, yaw);
            EXPECT_GE(dot(focus - marker, depthRay) / dot(depthRay, depthRay), radius - 0.0001f);
            EXPECT_GE(length(focus - marker), 1.7f);
        }
    }
}

TEST_F(CameraMouseRotateResetTest, MarkerClearanceDoesNotReplaceOrbitFocus) {
    IRPrefab::CursorPivot::OrbitAnchor anchor;
    anchor.pivotIndicator_ = IRPrefab::CursorPivot::createIndicator();
    anchor.focusWorld_ = IRMath::vec3(7, -4, 12);
    const auto markerPosition =
        IRPrefab::CursorPivot::detail::indicatorWorldPosition(anchor.focusWorld_, 1.17f, 0.225f);
    IRPrefab::CursorPivot::showIndicator(anchor.pivotIndicator_, markerPosition, 0.225f);
    EXPECT_EQ(anchor.focusWorld_, IRMath::vec3(7, -4, 12));
    EXPECT_EQ(
        IREntity::getComponent<IRComponents::C_WorldTransform>(anchor.pivotIndicator_).translation_,
        markerPosition
    );
    EXPECT_NE(markerPosition, anchor.focusWorld_);
}

TEST(CursorPivotMath, FloorFallbackRetainsClickedRayAtContinuousAndCardinalYaw) {
    using namespace IRMath;
    const vec2 cursorIso(12.25f, -39.5f);
    for (const float degrees : {0.0f, 30.0f, 67.0f, 90.0f, 180.0f, 270.0f}) {
        const float yaw = degrees * kPi / 180.0f;
        const vec3 origin = isoPixelToPos3DYawed(cursorIso, 0, yaw);
        const vec3 direction = isoPixelToPos3DYawed(vec2(0), 3, yaw);
        for (const float height : {-13.5f, 0.0f, 27.5f}) {
            const vec3 floor =
                IRPrefab::CursorPivot::detail::pointAtHeight(origin, direction, height);
            EXPECT_NEAR(floor.z, height, 0.0001f);
            const vec2 projected = pos3DtoPos2DIsoYawed(floor, yaw);
            EXPECT_NEAR(projected.x, cursorIso.x, 0.0001f);
            EXPECT_NEAR(projected.y, cursorIso.y, 0.0001f);
        }
    }
}

} // namespace
