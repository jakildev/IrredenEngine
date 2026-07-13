#ifndef SYSTEM_DEBUG_CULLING_MINIMAP_H
#define SYSTEM_DEBUG_CULLING_MINIMAP_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/common/components/component_world_transform.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/systems/system_bake_sun_shadow_map.hpp>
#include <irreden/render/systems/system_compute_light_volume.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/iso_spatial_hash.hpp>
#include <irreden/render/sun_shadow_constants.hpp>

#include <algorithm>
#include <vector>

using namespace IRComponents;
using namespace IRMath;

namespace IRSystem {

// Coordinate spaces used in the minimap and culling pipeline:
//
//   World 3D  (vec3)   Entity positions in voxel space. XY = ground, Z = up.
//
//   Iso 2D    (vec2)   Isometric projection of world 3D:
//                        iso.x = -world.x + world.y
//                        iso.y = -world.x - world.y + 2*world.z
//                      Used for canvas pixel addressing and 2D culling tests.
//
//   Canvas    (ivec2)  Pixel coordinates on the trixel canvas texture:
//                        canvasPixel = canvasOriginOffset + floor(cameraIso) + iso
//                      Canvas Y increases upward.
//
//   Screen    (vec2)   Pixel coordinates on the output framebuffer.
//                      The screen-vs-canvas Y relationship depends on the
//                      active renderer conventions (see IRPlatform::kGfx).
//                      Screen (0,0) is bottom-left, Y increases upward.
//
// The minimap plots entity iso positions mapped to screen coordinates.
// The iso-to-minimap transform uses screen-space Y, so it negates the
// relative Y component: minimap.y = center.y - rel.y.

namespace detail {

struct MinimapLayout {
    vec2 origin_;
    float width_;
    float height_;

    vec2 center() const {
        return origin_ + vec2(width_, height_) * 0.5f;
    }
    IsoBounds2D screenBounds() const {
        return {origin_, origin_ + vec2(width_, height_)};
    }
};

inline MinimapLayout
computeLayout(ivec2 viewport, float screenWidthFraction, float aspectRatio, float padding) {
    float width = viewport.x * screenWidthFraction;
    float height = width / aspectRatio;
    vec2 origin = vec2((viewport.x - width) * 0.5f, padding);
    return {origin, width, height};
}

struct EntityRecord {
    vec2 isoPosition_;
    bool isVisible_;
};

struct PendingEntity {
    IREntity::EntityId entityId_;
    vec2 isoPosition_;
};

inline float computeAutoFitExtent(
    const std::vector<EntityRecord> &entityRecords, vec2 viewCenterIso, vec2 cullExtent
) {
    float autoExtent = IRMath::max(cullExtent.x, cullExtent.y) * 0.5f;
    for (const auto &record : entityRecords) {
        vec2 distanceFromCenter = IRMath::abs(record.isoPosition_ - viewCenterIso);
        autoExtent =
            IRMath::max(autoExtent, IRMath::max(distanceFromCenter.x, distanceFromCenter.y));
    }
    return autoExtent * 1.2f;
}

inline vec2
isoToMinimapScreen(vec2 isoPosition, vec2 viewCenterIso, float scale, vec2 minimapCenter) {
    vec2 relativeOffset = (isoPosition - viewCenterIso) * scale;
    return minimapCenter + vec2(relativeOffset.x, -relativeOffset.y);
}

inline void drawViewportOutline(
    IsoBounds2D isoViewport, vec2 viewCenterIso, float scale, vec2 minimapCenter, vec4 borderColor
) {
    vec2 mappedMin = isoToMinimapScreen(isoViewport.min_, viewCenterIso, scale, minimapCenter);
    vec2 mappedMax = isoToMinimapScreen(isoViewport.max_, viewCenterIso, scale, minimapCenter);
    IsoBounds2D screenRect = IsoBounds2D::fromCorners(mappedMin, mappedMax);
    IRDebug::drawRectScreen(screenRect.min_, screenRect.max_, vec4(0.0f), borderColor);
}

inline void drawEntityDots(
    const std::vector<EntityRecord> &entityRecords,
    vec2 viewCenterIso,
    float scale,
    vec2 minimapCenter,
    IsoBounds2D minimapScreenBounds
) {
    constexpr float kDotRadius = 2.0f;
    const vec4 kVisibleColor(0.0f, 1.0f, 0.0f, 0.9f);
    const vec4 kCulledColor(1.0f, 0.0f, 0.0f, 0.7f);

    for (const auto &record : entityRecords) {
        vec2 dotPosition =
            isoToMinimapScreen(record.isoPosition_, viewCenterIso, scale, minimapCenter);
        if (!minimapScreenBounds.contains(dotPosition)) {
            continue;
        }
        vec4 dotColor = record.isVisible_ ? kVisibleColor : kCulledColor;
        IRDebug::drawDotScreen(dotPosition, kDotRadius, dotColor);
    }
}

inline void drawFrozenIndicator(vec2 minimapOrigin, float minimapWidth, float minimapHeight) {
    vec2 lineStart = minimapOrigin + vec2(minimapWidth - 30, minimapHeight - 8);
    vec2 lineEnd = minimapOrigin + vec2(minimapWidth - 8, minimapHeight - 8);
    IRDebug::drawLineScreen(lineStart, lineEnd, 1.0f, 0.3f, 0.3f, 0.9f);
}

// Light domain (#2316, V2). Color encodes V1's per-light gather outcome
// (system_compute_light_volume.hpp): full seed, boundary-clamped (residual
// alpha), or skipped entirely.
inline vec4 lightGatherStateColor(IRSystem::LightGatherState state, float residual) {
    switch (state) {
    case IRSystem::LightGatherState::SEEDED_FULL:
        return vec4(0.0f, 1.0f, 0.0f, 0.9f);
    case IRSystem::LightGatherState::BOUNDARY_DISCOUNTED:
        return vec4(1.0f, 1.0f, 0.0f, IRMath::clamp(residual, 0.15f, 0.9f));
    case IRSystem::LightGatherState::SKIPPED:
        return vec4(1.0f, 0.0f, 0.0f, 0.7f);
    }
    return vec4(1.0f, 0.0f, 1.0f, 0.9f); // unreachable — magenta flags a new enum value
}

// One foreign getComponentOptional per gathered light, once per frame
// (endTick) — the light-gather batch is already bounded to
// kLightVolumeMaxSources, so this is the sanctioned "batched foreign lookup"
// shape (cpp-ecs.md), not the per-entity-tick footgun.
inline void drawLightDots(
    const std::vector<IRSystem::LightGatherRecord> &records,
    vec2 viewCenterIso,
    float scale,
    vec2 minimapCenter,
    IsoBounds2D minimapScreenBounds
) {
    constexpr float kDotRadius = 2.5f;
    for (const auto &record : records) {
        auto xform = IREntity::getComponentOptional<C_WorldTransform>(record.entity_);
        if (!xform.has_value()) {
            continue;
        }
        vec2 isoPosition = IRMath::pos3DtoPos2DIso(xform.value()->translation_);
        vec2 dotPosition = isoToMinimapScreen(isoPosition, viewCenterIso, scale, minimapCenter);
        if (!minimapScreenBounds.contains(dotPosition)) {
            continue;
        }
        IRDebug::drawDotScreen(
            dotPosition,
            kDotRadius,
            lightGatherStateColor(record.state_, record.residual_)
        );
    }
}

// Caster domain (#2316, V2). Squares mark world-placed re-voxelize casters
// gathered by BAKE_SUN_SHADOW_MAP (system_bake_sun_shadow_map.hpp);
// membership is tested against the same shadow-feeder AABB the bake itself
// widens toward the sun (sun_shadow_constants.hpp), so a caster reading as a
// member here is exactly one the bake would still pick up off-screen.
inline void drawCasterSquares(
    const std::vector<IRSystem::WorldPlacedCaster> &casters,
    const IsoBounds2D &feederViewport,
    vec2 viewCenterIso,
    float scale,
    vec2 minimapCenter,
    IsoBounds2D minimapScreenBounds
) {
    constexpr float kHalfSize = 3.0f;
    const vec4 kMemberColor(0.2f, 0.8f, 1.0f, 0.85f);
    const vec4 kOutsideColor(1.0f, 0.5f, 0.0f, 0.85f);

    for (const auto &caster : casters) {
        vec2 isoPosition = IRMath::pos3DtoPos2DIso(caster.worldCellOffset_);
        vec2 screenPos = isoToMinimapScreen(isoPosition, viewCenterIso, scale, minimapCenter);
        if (!minimapScreenBounds.contains(screenPos)) {
            continue;
        }
        vec4 color = feederViewport.contains(isoPosition) ? kMemberColor : kOutsideColor;
        IRDebug::drawRectScreen(
            screenPos - vec2(kHalfSize),
            screenPos + vec2(kHalfSize),
            color,
            vec4(0.0f, 0.0f, 0.0f, 0.9f)
        );
    }
}

} // namespace detail

template <> struct System<DEBUG_CULLING_MINIMAP> {
    // Layout configuration — settable via create(Params).
    float screenWidthFraction_ = 0.15f;
    float aspectRatio_ = 12.0f / 7.0f;
    float padding_ = 10.0f;

    // Light + caster domain sources (#2316, V2) — settable via create(Params).
    // Optional: kNullEntity means "not wired", so a demo with no lighting
    // pipeline (e.g. the default demo) just gets the shape domain, no
    // spurious empty-window rectangle.
    IRSystem::SystemId lightVolumeSystemId_ = IREntity::kNullEntity;
    IRSystem::SystemId bakeSunShadowSystemId_ = IREntity::kNullEntity;

    // Per-frame scratch buffers (reused across ticks).
    std::vector<detail::EntityRecord> entityRecords_;
    IRRender::IsoSpatialHash spatialHash_{32};
    std::vector<detail::PendingEntity> pendingEntities_;

    // External config type for the two-argument create() overload.
    struct Params {
        float screenWidthFraction_ = 0.15f;
        float aspectRatio_ = 12.0f / 7.0f;
        float padding_ = 10.0f;
        IRSystem::SystemId lightVolumeSystemId_ = IREntity::kNullEntity;
        IRSystem::SystemId bakeSunShadowSystemId_ = IREntity::kNullEntity;
    };

    void tick(
        IREntity::EntityId entityId, const C_ShapeDescriptor &shape, const C_WorldTransform &xform
    ) {
        if (!IRRender::isCullingMinimapEnabled()) {
            return;
        }
        vec2 isoPosition = IRMath::pos3DtoPos2DIso(xform.translation_);
        vec2 isoHalfExtent = IRMath::shapeIsoHalfExtent(vec3(shape.params_));

        spatialHash_.insert(entityId, isoPosition - isoHalfExtent, isoPosition + isoHalfExtent);
        pendingEntities_.push_back({entityId, isoPosition});
    }

    void beginTick() {
        spatialHash_.clear();
        pendingEntities_.clear();
    }

    void endTick() {
        if (!IRRender::isCullingMinimapEnabled()) {
            return;
        }
        const auto &cullState = IRRender::getCullViewport();
        bool isCullingFrozen = cullState.frozen_;
        vec2 liveCameraIso = IRRender::getCameraPosition2DIso();
        vec2 liveCameraZoom = IRRender::getCameraZoom();
        ivec2 viewportSize = IRRender::getViewport();

        auto cullViewport = cullState.isoViewport();
        auto visibleEntityIds = spatialHash_.query(cullViewport.min_, cullViewport.max_);

        entityRecords_.clear();
        for (const auto &pending : pendingEntities_) {
            bool isVisible =
                std::find(visibleEntityIds.begin(), visibleEntityIds.end(), pending.entityId_) !=
                visibleEntityIds.end();
            entityRecords_.push_back({pending.isoPosition_, isVisible});
        }

        auto layout =
            detail::computeLayout(viewportSize, screenWidthFraction_, aspectRatio_, padding_);

        IRDebug::drawRectScreen(
            layout.origin_,
            layout.origin_ + vec2(layout.width_, layout.height_),
            vec4(0.0f, 0.0f, 0.0f, 0.4f),
            vec4(0.5f, 0.5f, 0.5f, 0.8f)
        );

        vec2 viewCenterIso = cullViewport.center();
        vec2 cullExtent = cullViewport.extent();

        float autoFitExtent =
            detail::computeAutoFitExtent(entityRecords_, viewCenterIso, cullExtent);

        float isoToMinimapScale = layout.width_ / (autoFitExtent * 2.0f);
        vec2 minimapCenter = layout.center();

        detail::drawViewportOutline(
            cullViewport,
            viewCenterIso,
            isoToMinimapScale,
            minimapCenter,
            vec4(1.0f, 1.0f, 1.0f, 0.9f)
        );

        if (isCullingFrozen) {
            ivec2 liveCanvasSize = ivec2(0);
            {
                IREntity::EntityId mainCanvasEntity = IRRender::getActiveCanvasEntity();
                auto texturesOptional =
                    IREntity::getComponentOptional<C_TriangleCanvasTextures>(mainCanvasEntity);
                if (texturesOptional.has_value()) {
                    liveCanvasSize = texturesOptional.value()->size_;
                }
            }
            auto liveViewport = IRMath::visibleIsoViewport(
                liveCameraIso,
                IRMath::trixelOriginOffsetZ1(liveCanvasSize),
                liveCanvasSize,
                liveCameraZoom
            );
            detail::drawViewportOutline(
                liveViewport,
                viewCenterIso,
                isoToMinimapScale,
                minimapCenter,
                vec4(0.2f, 0.8f, 1.0f, 0.7f)
            );
        }

        detail::drawEntityDots(
            entityRecords_,
            viewCenterIso,
            isoToMinimapScale,
            minimapCenter,
            layout.screenBounds()
        );

        if (isCullingFrozen) {
            detail::drawFrozenIndicator(layout.origin_, layout.width_, layout.height_);
        }

        // Light domain (#2316, V2): gather-state-colored dots + the pinned
        // light-volume window rectangle. Skipped when this instance wasn't
        // wired to a COMPUTE_LIGHT_VOLUME system.
        if (lightVolumeSystemId_ != IREntity::kNullEntity) {
            const auto &lightRecords = IRSystem::lightGatherRecords(lightVolumeSystemId_);
            detail::drawLightDots(
                lightRecords,
                viewCenterIso,
                isoToMinimapScale,
                minimapCenter,
                layout.screenBounds()
            );

            // Iso-space footprint of the WORLD anchor±kLightVolumeHalfExtent
            // cube, projected at z=0 — cameraAnchorVoxel() always pins
            // anchor.z to 0 (camera_anchor.hpp), and the window's Z extent
            // has no meaning on a top-down minimap. A z-degenerate box (min.z
            // == max.z == 0) collapses isoAABBOfWorldAABBUnderYaw's 8-corner
            // enumeration to the same 4 unique XY corners this needs.
            const ivec3 anchor = IRRender::getLightAnchorFreeze().anchor_;
            const float halfExtent = static_cast<float>(kLightVolumeHalfExtent);
            const vec3 windowLo(anchor.x - halfExtent, anchor.y - halfExtent, 0.0f);
            const vec3 windowHi(anchor.x + halfExtent, anchor.y + halfExtent, 0.0f);
            IsoBounds2D windowBounds = IRMath::isoAABBOfWorldAABBUnderYaw(windowLo, windowHi, 0.0f);
            detail::drawViewportOutline(
                windowBounds,
                viewCenterIso,
                isoToMinimapScale,
                minimapCenter,
                vec4(1.0f, 1.0f, 0.0f, 0.6f)
            );
        }

        // Caster domain (#2316, V2): world-placed casters as squares +
        // the shadow-feeder AABB rectangle they're tested against. Skipped
        // when this instance wasn't wired to a BAKE_SUN_SHADOW_MAP system.
        if (bakeSunShadowSystemId_ != IREntity::kNullEntity) {
            const auto shadowFeederParams = IRPrefab::SunShadow::frameShadowFeederParams();
            const IsoBounds2D feederViewport =
                IRPrefab::SunShadow::shadowFeederCullViewport(0, shadowFeederParams);
            const auto &casters = IRSystem::worldPlacedCasters(bakeSunShadowSystemId_);
            detail::drawCasterSquares(
                casters,
                feederViewport,
                viewCenterIso,
                isoToMinimapScale,
                minimapCenter,
                layout.screenBounds()
            );
            detail::drawViewportOutline(
                feederViewport,
                viewCenterIso,
                isoToMinimapScale,
                minimapCenter,
                vec4(1.0f, 0.5f, 0.0f, 0.5f)
            );
        }
    }

    static SystemId create() {
        return create(Params{});
    }

    static SystemId create(const Params &initialParams) {
        SystemId systemId =
            registerSystem<DEBUG_CULLING_MINIMAP, C_ShapeDescriptor, C_WorldTransform>(
                "DebugCullingMinimap"
            );
        auto *p = getSystemParams<System<DEBUG_CULLING_MINIMAP>>(systemId);
        p->screenWidthFraction_ = initialParams.screenWidthFraction_;
        p->aspectRatio_ = initialParams.aspectRatio_;
        p->padding_ = initialParams.padding_;
        p->lightVolumeSystemId_ = initialParams.lightVolumeSystemId_;
        p->bakeSunShadowSystemId_ = initialParams.bakeSunShadowSystemId_;
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_CULLING_MINIMAP_H */
