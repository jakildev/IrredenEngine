#ifndef SYSTEM_DEBUG_CULLING_MINIMAP_H
#define SYSTEM_DEBUG_CULLING_MINIMAP_H

#include <irreden/ir_system.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_math.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/voxel/components/component_shape_descriptor.hpp>
#include <irreden/common/components/component_position_global_3d.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/systems/system_debug_overlay.hpp>
#include <irreden/render/cull_viewport_state.hpp>
#include <irreden/render/iso_spatial_hash.hpp>

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

    vec2 center() const { return origin_ + vec2(width_, height_) * 0.5f; }
    IsoBounds2D screenBounds() const { return {origin_, origin_ + vec2(width_, height_)}; }
};

inline MinimapLayout computeLayout(ivec2 viewport, float screenWidthFraction, float aspectRatio, float padding) {
    float width = viewport.x * screenWidthFraction;
    float height = width / aspectRatio;
    vec2 origin = vec2((viewport.x - width) * 0.5f, padding);
    return {origin, width, height};
}

struct EntityRecord {
    vec2 isoPosition_;
    bool isVisible_;
};

inline float computeAutoFitExtent(
    const std::vector<EntityRecord> &entityRecords,
    vec2 viewCenterIso,
    vec2 cullExtent
) {
    float autoExtent = IRMath::max(cullExtent.x, cullExtent.y) * 0.5f;
    for (const auto &record : entityRecords) {
        vec2 distanceFromCenter = glm::abs(record.isoPosition_ - viewCenterIso);
        autoExtent = IRMath::max(autoExtent, IRMath::max(distanceFromCenter.x, distanceFromCenter.y));
    }
    return autoExtent * 1.2f;
}

inline vec2 isoToMinimapScreen(vec2 isoPosition, vec2 viewCenterIso, float scale, vec2 minimapCenter) {
    vec2 relativeOffset = (isoPosition - viewCenterIso) * scale;
    return minimapCenter + vec2(relativeOffset.x, -relativeOffset.y);
}

inline void drawViewportOutline(
    IsoBounds2D isoViewport,
    vec2 viewCenterIso,
    float scale,
    vec2 minimapCenter,
    vec4 borderColor
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
        vec2 dotPosition = isoToMinimapScreen(record.isoPosition_, viewCenterIso, scale, minimapCenter);
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

} // namespace detail

template <> struct System<DEBUG_CULLING_MINIMAP> {
    struct Params {
        float screenWidthFraction_ = 0.15f;
        float aspectRatio_ = 12.0f / 7.0f;
        float padding_ = 10.0f;
    };

    static SystemId create() { return create(Params{}); }

    static SystemId create(const Params &initialParams) {
        auto paramsOwner = std::make_unique<Params>(initialParams);
        Params *params = paramsOwner.get();

        static std::vector<detail::EntityRecord> entityRecords;

        static IRRender::IsoSpatialHash spatialHash(32);

        struct PendingEntity {
            IREntity::EntityId entityId_;
            vec2 isoPosition_;
        };
        static std::vector<PendingEntity> pendingEntities;

        SystemId systemId = createSystem<C_ShapeDescriptor, C_PositionGlobal3D>(
            "DebugCullingMinimap",
            [](IREntity::EntityId &entityId,
               const C_ShapeDescriptor &shape,
               const C_PositionGlobal3D &position) {
                vec2 isoPosition = IRMath::pos3DtoPos2DIso(position.pos_);
                vec2 isoHalfExtent = IRMath::shapeIsoHalfExtent(vec3(shape.params_));

                spatialHash.insert(entityId, isoPosition - isoHalfExtent, isoPosition + isoHalfExtent);
                pendingEntities.push_back({entityId, isoPosition});
            },
            []() {
                spatialHash.clear();
                pendingEntities.clear();
            },
            [params]() {
                const auto &cullState = IRRender::getCullViewport();
                bool isCullingFrozen = cullState.frozen_;
                vec2 liveCameraIso = IRRender::getCameraPosition2DIso();
                vec2 liveCameraZoom = IRRender::getCameraZoom();
                ivec2 viewportSize = IRRender::getViewport();

                auto cullViewport = cullState.isoViewport();
                auto visibleEntityIds = spatialHash.query(cullViewport.min_, cullViewport.max_);

                entityRecords.clear();
                for (const auto &pending : pendingEntities) {
                    bool isVisible =
                        std::find(visibleEntityIds.begin(), visibleEntityIds.end(), pending.entityId_)
                        != visibleEntityIds.end();
                    entityRecords.push_back({pending.isoPosition_, isVisible});
                }

                auto layout = detail::computeLayout(
                    viewportSize, params->screenWidthFraction_,
                    params->aspectRatio_, params->padding_
                );

                IRDebug::drawRectScreen(
                    layout.origin_,
                    layout.origin_ + vec2(layout.width_, layout.height_),
                    vec4(0.0f, 0.0f, 0.0f, 0.4f),
                    vec4(0.5f, 0.5f, 0.5f, 0.8f)
                );

                vec2 viewCenterIso = cullViewport.center();
                vec2 cullExtent = cullViewport.extent();

                float autoFitExtent = detail::computeAutoFitExtent(
                    entityRecords, viewCenterIso, cullExtent
                );

                float isoToMinimapScale = layout.width_ / (autoFitExtent * 2.0f);
                vec2 minimapCenter = layout.center();

                detail::drawViewportOutline(
                    cullViewport, viewCenterIso, isoToMinimapScale,
                    minimapCenter, vec4(1.0f, 1.0f, 1.0f, 0.9f)
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
                        liveCanvasSize, liveCameraZoom
                    );
                    detail::drawViewportOutline(
                        liveViewport, viewCenterIso, isoToMinimapScale,
                        minimapCenter, vec4(0.2f, 0.8f, 1.0f, 0.7f)
                    );
                }

                detail::drawEntityDots(
                    entityRecords, viewCenterIso, isoToMinimapScale,
                    minimapCenter, layout.screenBounds()
                );

                if (isCullingFrozen) {
                    detail::drawFrozenIndicator(layout.origin_, layout.width_, layout.height_);
                }
            }
        );

        setSystemParams(systemId, std::move(paramsOwner));
        return systemId;
    }
};

} // namespace IRSystem

#endif /* SYSTEM_DEBUG_CULLING_MINIMAP_H */
