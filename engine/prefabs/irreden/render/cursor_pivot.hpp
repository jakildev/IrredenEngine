#ifndef IR_PREFAB_CURSOR_PIVOT_H
#define IR_PREFAB_CURSOR_PIVOT_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_input.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>

#include <irreden/common/components/component_local_transform.hpp>
#include <irreden/common/components/component_name.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_fog_exempt.hpp>
#include <irreden/render/components/component_screen_space_marker.hpp>
#include <irreden/render/camera.hpp>
#include <irreden/render/picking.hpp>
#include <irreden/voxel/components/component_shape_descriptor.hpp>

#include <functional>
#include <optional>

namespace IRPrefab::CursorPivot {

constexpr float kIndicatorRadius = 0.9f;
constexpr IRMath::Color kIndicatorColor{255, 208, 64, 255};

struct FocusOptions {
    // Called once at press on the main thread. A miss uses the exact floor ray.
    std::function<std::optional<IRMath::vec3>(IREntity::EntityId)> resolveSurface_;
    std::optional<float> floorHeight_;
};

namespace detail {

// The isometric cursor ray always has a nonzero world-Z component.
inline IRMath::vec3 pointAtHeight(IRMath::vec3 origin, IRMath::vec3 direction, float height) {
    return origin + direction * ((height - origin.z) / direction.z);
}

inline IRMath::vec2
preserveViewCamera(IRMath::vec2 rawCamera, IRMath::vec2 oldEffective, IRMath::vec2 newEffective) {
    return rawCamera + oldEffective - newEffective;
}

inline IRMath::vec3 indicatorWorldPosition(IRMath::vec3 focus, float yaw, float radius) {
    // Conservative voxel depth keys cover a whole cell, even for a surface hit.
    const float clearance = IRMath::max(1.0f, IRMath::ceil(radius));
    return focus - IRMath::isoPixelToPos3DYawed(IRMath::vec2(0.0f), 3.0f, yaw) * clearance;
}

} // namespace detail

inline IRMath::vec3 resolveFloorWorld(float height) {
    const auto origin = IRRender::mouseWorldPos3DAtIsoDepthExact(0.0f);
    const auto direction = IRRender::mouseWorldPos3DAtIsoDepthExact(3.0f) - origin;
    return detail::pointAtHeight(origin, direction, height);
}

// Compatibility entry point: iso-column pick with the default-pivot miss policy.
inline IRMath::vec3 resolveFocusWorld(IREntity::EntityId excludeEntity = IREntity::kNullEntity) {
    if (const auto hit = Picking::castVoxelRay(excludeEntity))
        return hit->worldHitPos_;
    return IRRender::getDefaultRotationPivotFocus();
}

inline IRMath::vec3
resolveFocusWorld(IREntity::EntityId excludeEntity, const FocusOptions &options) {
    if (options.resolveSurface_) {
        if (const auto hit = options.resolveSurface_(excludeEntity))
            return *hit;
    } else {
        const auto hit = Picking::castVoxelRay(
            Picking::RayCastOptions{
                .excludeEntity_ = excludeEntity,
                .cursorRay_ = Picking::CursorRay::SCREEN_PIXEL
            }
        );
        if (hit)
            return hit->worldHitPos_;
    }
    return resolveFloorWorld(options.floorHeight_.value_or(IRRender::getViewReferenceHeight()));
}

inline IRMath::vec3 resolveCenterWorld(float height) {
    const auto iso =
        IRRender::getRenderManager().getCanvasCenterIso() - IRRender::getEffectiveCameraIso();
    const float yaw = Camera::getYaw();
    return detail::pointAtHeight(
        IRMath::isoPixelToPos3DYawed(iso, 0.0f, yaw),
        IRMath::isoPixelToPos3DYawed(IRMath::vec2(0.0f), 3.0f, yaw),
        height
    );
}

// An explicit pivot has identity camera-translation derivative, including at
// yaw angles where the live-center pan inverse is singular.
inline void setFocusPreservingView(IRMath::vec3 focusWorld) {
    const auto oldEffective = IRRender::getEffectiveCameraIso();
    const auto rawCamera = IRRender::getCameraPosition2DIso();
    IRRender::setRotationPivotMode(IRRender::RotationPivotMode::CAMERA_CENTER);
    IRRender::setRotationPivotFocus(focusWorld);
    IRRender::setCameraPosition2DIso(
        detail::preserveViewCamera(rawCamera, oldEffective, IRRender::getEffectiveCameraIso())
    );
}

inline IREntity::EntityId createIndicator() {
    IRComponents::C_ShapeDescriptor marker{
        IRRender::ShapeType::SPHERE,
        IRMath::vec4(kIndicatorRadius, kIndicatorRadius, kIndicatorRadius, 0.0f),
        kIndicatorColor
    };
    marker.flags_ = IRRender::SHAPE_FLAG_XRAY_OCCLUDED;
    // Resolve the main canvas at render time, independent of the active pass.
    marker.canvasEntity_ = IREntity::kNullEntity;
    return IREntity::createEntity(
        IRComponents::C_LocalTransform{IRMath::vec3(0.0f)},
        marker,
        IRComponents::C_ScreenSpaceMarker{},
        IRComponents::C_FogExempt{},
        IRComponents::C_Name{"CursorPivotIndicator"}
    );
}

inline void showIndicator(
    IREntity::EntityId indicator, IRMath::vec3 worldPos, float radius = kIndicatorRadius
) {
    if (indicator == IREntity::kNullEntity)
        return;
    // Camera controls run after transform propagation. The marker is parentless.
    IREntity::getComponent<IRComponents::C_LocalTransform>(indicator).translation_ = worldPos;
    IREntity::getComponent<IRComponents::C_WorldTransform>(indicator).translation_ = worldPos;
    auto &marker = IREntity::getComponent<IRComponents::C_ShapeDescriptor>(indicator);
    marker.params_ = IRMath::vec4(radius, radius, radius, 0.0f);
    marker.flags_ |= IRRender::SHAPE_FLAG_VISIBLE;
    if (auto screen = IREntity::getComponentOptional<IRComponents::C_ScreenSpaceMarker>(indicator))
        (*screen)->visible_ = false;
}

inline void hideIndicator(IREntity::EntityId indicator) {
    if (indicator == IREntity::kNullEntity)
        return;
    IREntity::getComponent<IRComponents::C_ShapeDescriptor>(indicator).flags_ &=
        ~IRRender::SHAPE_FLAG_VISIBLE;
    if (auto marker = IREntity::getComponentOptional<IRComponents::C_ScreenSpaceMarker>(indicator))
        (*marker)->visible_ = false;
}

inline void showScreenIndicator(IREntity::EntityId indicator, IRMath::vec2 windowPoint) {
    hideIndicator(indicator);
    auto &marker = IREntity::getComponent<IRComponents::C_ScreenSpaceMarker>(indicator);
    marker.windowPoint_ = windowPoint;
    marker.radiusPoints_ = 6.0f;
    marker.color_ = kIndicatorColor;
    marker.visible_ = true;
}

// Owned by a gesture/system. Forward entity pre-destroy notifications through
// onEntityDestroyed; invoke begin/end on the main thread before shape rendering.
struct OrbitAnchor {
    IREntity::EntityId pivotIndicator_ = IREntity::kNullEntity;
    bool cursorPivot_ = false;
    IRMath::vec3 focusWorld_ = IRMath::vec3(0.0f);
    IRMath::vec2 indicatorWindowPoint_ = IRMath::vec2(0.0f);

    void begin(bool cursorPivot = true, const FocusOptions &options = {}) {
        cursorPivot_ = cursorPivot;
        indicatorWindowPoint_ = IRInput::getMousePositionScreen();
        focusWorld_ = cursorPivot ? resolveFocusWorld(pivotIndicator_, options)
                                  : resolveCenterWorld(IRRender::getViewReferenceHeight());
        setFocusPreservingView(focusWorld_);
        // The hidden center marker also supplies the gameplay-reset lifetime signal.
        if (pivotIndicator_ == IREntity::kNullEntity)
            pivotIndicator_ = createIndicator();
        updateIndicator();
    }

    void updateIndicator() {
        if (!cursorPivot_) {
            hideIndicator(pivotIndicator_);
            return;
        }
        if (IRSystem::findSystem(IRSystem::DEBUG_OVERLAY) != IRSystem::kNullSystemId) {
            showScreenIndicator(pivotIndicator_, indicatorWindowPoint_);
            return;
        }
        const float radius = kIndicatorRadius / IRMath::max(IRRender::getCameraZoom().x, 1e-3f);
        showIndicator(
            pivotIndicator_,
            detail::indicatorWorldPosition(focusWorld_, Camera::getYaw(), radius),
            radius
        );
    }

    void end() {
        hideIndicator(pivotIndicator_);
        cursorPivot_ = false;
        // Retaining the focus keeps release and subsequent panning view-neutral.
    }

    void onEntityDestroyed(IREntity::EntityId destroyed) {
        if (pivotIndicator_ == destroyed) {
            pivotIndicator_ = IREntity::kNullEntity;
            cursorPivot_ = false;
        }
    }
};

} // namespace IRPrefab::CursorPivot

#endif /* IR_PREFAB_CURSOR_PIVOT_H */
