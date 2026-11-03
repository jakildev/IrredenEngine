#ifndef SURFACE_LIGHTING_ROUTE_H
#define SURFACE_LIGHTING_ROUTE_H

#include <irreden/ir_entity.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_system.hpp>
#include <irreden/render/per_axis_surface_lighting.hpp>
#include <irreden/render/trixel_glitch_math.hpp>
#include <irreden/render/components/component_canvas_ao_texture.hpp>
#include <irreden/render/components/component_canvas_light_volume.hpp>
#include <irreden/render/components/component_canvas_sun_shadow.hpp>
#include <irreden/render/components/component_triangle_canvas_textures.hpp>
#include <irreden/render/components/component_trixel_canvas_render_behavior.hpp>
#include <irreden/render/components/component_per_axis_trixel_canvases.hpp>
#include <irreden/voxel/components/component_voxel_pool.hpp>

namespace IRPrefab::PerAxisSurfaceLighting {

struct Route {
    bool cardinal_ = false;
    bool perAxis_ = false;
    bool cardinalGlitch_ = false;
    SurfaceFogRoute fog_ = SurfaceFogRoute::NONE;
};

namespace detail {
inline bool anyGlitchSubjectActive() {
    const auto subjectNodes = IREntity::queryArchetypeNodesSimple(
        IREntity::getArchetype<IRComponents::C_GlitchSubject>()
    );
    for (auto *node : subjectNodes) {
        for (const IRComponents::C_GlitchSubject &subject :
             IREntity::getComponentData<IRComponents::C_GlitchSubject>(node)) {
            if (IRPrefab::Glitch::isSubjectActive(subject)) {
                return true;
            }
        }
    }
    return false;
}

} // namespace detail

// Resolve from this RENDER pass's inputs, before sampled-shadow production and
// again before albedo publication. Readiness is still published by lighting's
// main-canvas tick; last frame's readiness must never suppress a producer.
inline Route resolveRoute(
    IREntity::EntityId perAxisCanvasEntity_,
    const IRComponents::C_TriangleCanvasTextures *mainCanvasTextures_,
    const IRComponents::C_CanvasAOTexture *mainCanvasAO_,
    const IRComponents::C_CanvasSunShadow *mainCanvasSunShadow_,
    const IRComponents::C_CanvasLightVolume *mainCanvasLightVolume_,
    const IRComponents::C_VoxelPool *mainVoxelPool_,
    const IRComponents::C_PerAxisTrixelCanvases *perAxisCanvases_,
    const IRComponents::C_CanvasFogOfWar *mainCanvasFog_
) {
    using namespace IRComponents;
    using namespace IRSystem;
    using IRRender::DebugOverlayMode;
    bool cardinalSurfaceLightingEligible_ = false;
    bool perAxisSurfaceLightingEligible_ = false;
    bool cardinalGlitchPresentationActive_ = false;
    SurfaceFogRoute surfaceFogRoute_ = SurfaceFogRoute::NONE;
    // Debug modes and incomplete pipelines retain the compute path;
    // presentation must never mistake them for albedo.
    bool worldSurfaceLightingEligible = false;
    if (mainCanvasTextures_ != nullptr && mainCanvasAO_ != nullptr &&
        mainCanvasSunShadow_ != nullptr && mainCanvasLightVolume_ != nullptr &&
        findSystem(COMPUTE_SUN_SHADOW) != kNullSystemId &&
        findSystem(TRIXEL_TO_FRAMEBUFFER) != kNullSystemId &&
        IRRender::getDebugOverlay() == DebugOverlayMode::NONE &&
        !IRRender::getDepthColorDebugMode()) {
        auto behavior =
            IREntity::getComponentOptional<C_TrixelCanvasRenderBehavior>(perAxisCanvasEntity_);
        worldSurfaceLightingEligible =
            behavior.has_value() && behavior.value()->useCameraPositionIso_;
    }
    const C_PerAxisSurfaceLighting *surfaceLighting =
        IREntity::singletonOrNull<C_PerAxisSurfaceLighting>();
    const bool surfaceLightingEnabled = surfaceLighting != nullptr && surfaceLighting->enabled_;
    const bool fogPassRegistered = findSystem(FOG_TO_TRIXEL) != kNullSystemId;
    const bool cardinalCandidate =
        worldSurfaceLightingEligible && mainVoxelPool_ != nullptr &&
        (perAxisCanvases_ == nullptr || !perAxisCanvases_->isAllocated());
    // The scan runs only where its answer can change a route.
    const bool glitchSubjectActive =
        worldSurfaceLightingEligible && findSystem(TRIXEL_GLITCH) != kNullSystemId &&
        (!fogPassRegistered || surfaceLightingEnabled) &&
        (cardinalCandidate || (fogPassRegistered && mainCanvasFog_ != nullptr)) &&
        detail::anyGlitchSubjectActive();
    surfaceFogRoute_ = IRPrefab::PerAxisSurfaceLighting::surfaceFogRoute(
        fogPassRegistered,
        mainCanvasFog_ != nullptr ? &mainCanvasFog_->observers_ : nullptr,
        surfaceLightingEnabled,
        glitchSubjectActive
    );
    if (surfaceFogRoute_ == IRPrefab::PerAxisSurfaceLighting::SurfaceFogRoute::FALLBACK) {
        worldSurfaceLightingEligible = false;
    }
    cardinalSurfaceLightingEligible_ = cardinalCandidate && worldSurfaceLightingEligible;
    cardinalGlitchPresentationActive_ = cardinalSurfaceLightingEligible_ && glitchSubjectActive;
    perAxisSurfaceLightingEligible_ = worldSurfaceLightingEligible && surfaceLightingEnabled &&
                                      perAxisCanvases_ != nullptr &&
                                      perAxisCanvases_->isAllocated();

    return {
        cardinalSurfaceLightingEligible_,
        perAxisSurfaceLightingEligible_,
        cardinalGlitchPresentationActive_,
        surfaceFogRoute_
    };
}

// The early shadow pass does not otherwise need these optional main-canvas
// resources. All references are consumed here, not retained across ECS changes.
inline Route resolveRoute(IREntity::EntityId canvas) {
    if (canvas == IREntity::kNullEntity)
        return {};
    using namespace IRComponents;
    const auto textures = IREntity::getComponentOptional<C_TriangleCanvasTextures>(canvas);
    const auto ao = IREntity::getComponentOptional<C_CanvasAOTexture>(canvas);
    const auto shadow = IREntity::getComponentOptional<C_CanvasSunShadow>(canvas);
    const auto volume = IREntity::getComponentOptional<C_CanvasLightVolume>(canvas);
    const auto pool = IREntity::getComponentOptional<C_VoxelPool>(canvas);
    const auto axes = IREntity::getComponentOptional<C_PerAxisTrixelCanvases>(canvas);
    const auto fog = IREntity::getComponentOptional<C_CanvasFogOfWar>(canvas);
    return resolveRoute(
        canvas,
        textures.value_or(nullptr),
        ao.value_or(nullptr),
        shadow.value_or(nullptr),
        volume.value_or(nullptr),
        pool.value_or(nullptr),
        axes.value_or(nullptr),
        fog.value_or(nullptr)
    );
}

} // namespace IRPrefab::PerAxisSurfaceLighting
#endif /* SURFACE_LIGHTING_ROUTE_H */
