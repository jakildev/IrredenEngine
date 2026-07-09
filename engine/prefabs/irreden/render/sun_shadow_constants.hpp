#ifndef IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H
#define IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H

#include <irreden/ir_math.hpp>
#include <irreden/ir_render.hpp>
#include <irreden/ir_entity.hpp>

#include <irreden/render/components/component_light_source.hpp>
#include <irreden/render/cull_viewport_state.hpp>

#include <limits>

namespace IRPrefab::SunShadow {

// Maximum shadow throw distance in voxels. Drives both the sun-depth-map
// AABB sweep in BAKE_SUN_SHADOW_MAP and the cull-region widening in the
// iso rasterizers (VOXEL_TO_TRIXEL_*, SHAPES_TO_TRIXEL); they must agree
// or off-screen casters within shadow range either fail to write the
// shadow source (rasterizer too narrow) or fail to project it onto a
// visible pixel (bake too narrow).
constexpr float kSunShadowMaxDistance = 64.0f;

// Resolves the active sun direction. Honors a `C_LightSource{type=DIRECTIONAL}`
// entity's `direction_` if present (matches BAKE_SUN_SHADOW_MAP's resolution
// in detail::resolveSun), otherwise falls back to `IRRender::getSunDirection()`.
// Returned vector is unit-length when the override is non-zero; the global
// path is unit-length by `setSunDirection` invariant.
inline IRMath::vec3 resolveDirection() {
    IRMath::vec3 dir = IRRender::getSunDirection();
    const auto include = IREntity::getArchetype<IRComponents::C_LightSource>();
    auto nodes = IREntity::queryArchetypeNodesSimple(include);
    for (auto *node : nodes) {
        auto &lights = IREntity::getComponentData<IRComponents::C_LightSource>(node);
        for (int i = 0; i < node->length_; ++i) {
            const IRComponents::C_LightSource &light = lights[i];
            if (light.type_ != IRComponents::LightType::DIRECTIONAL) {
                continue;
            }
            const float length = IRMath::length(light.direction_);
            if (length > 0.0f) {
                dir = light.direction_ / length;
            }
        }
    }
    return dir;
}

// Per-frame resolved sun direction. Call from a system's beginTick (once per
// frame), store the result in SystemParams, and read the cached value in the
// per-entity tick. Identical to resolveDirection() today; reserved for future
// caching by a RESOLVE_SUN_DIRECTION head-of-pipeline system.
inline IRMath::vec3 getFrameSunDirection() {
    return resolveDirection();
}

// Per-frame shadow-feeder inputs: the active sun direction and the iso-AABB
// sweep length, each gated on whether sun shadows are enabled (disabled →
// zero, the no-widen case for IRMath::shadowFeederIsoBounds). The
// C_LightSource archetype scan behind getFrameSunDirection() runs only when
// shadows are on, so resolve this ONCE per frame (a system's beginTick) and
// reuse the result; don't re-resolve per iterating entity/canvas.
struct ShadowFeederParams {
    IRMath::vec3 sunDir_ = IRMath::vec3(0.0f);
    float sweepDistance_ = 0.0f;
};

inline ShadowFeederParams frameShadowFeederParams() {
    if (!IRRender::getSunShadowsEnabled()) {
        return {};
    }
    return {getFrameSunDirection(), kSunShadowMaxDistance};
}

// The shared frozen-or-live cull viewport at @p margin, widened to the
// shadow-feeder AABB so off-screen casters within shadow range still feed the
// sun-depth bake (see IRMath::shadowFeederIsoBounds). Pass the once-per-frame
// @p params from frameShadowFeederParams(); a caller needing several margins
// (the voxel rasterizer's chunk + GPU bounds) reuses a single params resolve.
// Centralizes the cull-viewport → shadow-feeder derivation shared by
// VOXEL_TO_TRIXEL_STAGE_1, SHAPES_TO_TRIXEL, and REBUILD_GRID_VOXELS.
inline IRMath::IsoBounds2D shadowFeederCullViewport(int margin, const ShadowFeederParams &params) {
    return IRMath::shadowFeederIsoBounds(
        IRRender::getCullViewport().isoViewport(margin),
        params.sunDir_,
        params.sweepDistance_
    );
}

inline IRMath::IsoBounds2D shadowFeederCullViewport(
    int margin, const ShadowFeederParams &params, const IRRender::CullViewportState &cull
) {
    return IRMath::shadowFeederIsoBounds(
        cull.isoViewport(margin),
        params.sunDir_,
        params.sweepDistance_
    );
}

// Sun-UV bounding box of the iso-frustum depth slab [@p depthMin, @p depthMax]
// over @p isoBounds, with every corner ALSO offset by @p sweep (world-frame,
// = -sunDir * sweepDistance) so off-screen casters within shadow range are
// enclosed — the box BAKE_SUN_SHADOW_MAP fits its depth map to before the
// texel-grid snap. Each corner is lifted from the rasterYaw-rotated canvas frame
// into world frame (rotateCardinalZInv) before projecting onto the sun basis, so
// the sweep (a world-frame vector) shares the corners' frame; no-op at
// rasterYaw == 0. @p uHat / @p vHat / @p sunDir are the sun basis from
// buildOrthonormalBasis; @p cardinalIndex is the rasterYaw cardinal snap.
// Projects via IRMath::sunSpaceProject — the same projection the bake +
// receiver shaders use (#2083) — so the AABB brackets exactly what they will
// project. Lives in the shared sun-shadow header so a sun-space feeder-density
// consumer can reuse the bake's exact derivation rather than re-deriving it.
inline IRMath::IsoBounds2D sunBakeFrustumUVBounds(
    const IRMath::IsoBounds2D &isoBounds,
    float depthMin,
    float depthMax,
    const IRMath::vec3 &uHat,
    const IRMath::vec3 &vHat,
    const IRMath::vec3 &sunDir,
    IRMath::CardinalIndex cardinalIndex,
    const IRMath::vec3 &sweep
) {
    IRMath::vec2 uvMin(std::numeric_limits<float>::max());
    IRMath::vec2 uvMax(std::numeric_limits<float>::lowest());
    for (float depth : {depthMin, depthMax}) {
        for (int y : {isoBounds.min_.y, isoBounds.max_.y}) {
            for (int x : {isoBounds.min_.x, isoBounds.max_.x}) {
                const IRMath::vec3 corner =
                    IRMath::rotateCardinalZInv(IRMath::isoPixelToPos3D(x, y, depth), cardinalIndex);
                for (const IRMath::vec3 &offset : {IRMath::vec3(0.0f), sweep}) {
                    const IRMath::vec3 p = corner + offset;
                    const IRMath::vec2 uv(IRMath::sunSpaceProject(p, uHat, vHat, sunDir));
                    uvMin = IRMath::min(uvMin, uv);
                    uvMax = IRMath::max(uvMax, uv);
                }
            }
        }
    }
    return IRMath::IsoBounds2D{uvMin, uvMax};
}

// Sun-bake density constants for the #2258 Step-B shadow-feeder dispatch cap.
// These MUST match system_bake_sun_shadow_map.hpp (kSunShadowMapDim /
// kSunShadowCascadeCount / kCascadeSplitRatio) and the bake's local iso-depth
// clip range — the cap is only meaningful if it reproduces the bake's texel
// density. Kept here (not in the bake header) so the feeder-density consumer
// in VOXEL_TO_TRIXEL_STAGE_1 can reuse the exact derivation without pulling the
// bake system header; a drift here shows as shadow holes in the render-debug
// loop, which is the acceptance gate.
constexpr int kFeederSunShadowMapDim = 1024;     // == kSunShadowMapDim
constexpr int kFeederSunShadowCascadeCount = 2;  // == kSunShadowCascadeCount
constexpr float kFeederCascadeSplitRatio = 0.4f; // == kCascadeSplitRatio
constexpr float kFeederIsoDepthMin = -256.0f;    // == the bake's local kIsoDepthMin
constexpr float kFeederIsoDepthMax = 256.0f;     // == the bake's local kIsoDepthMax
// The ONE render-debug-loop-tunable knob (#2258 Step B): the per-face-edge
// feeder sample count as a multiple of the bake's texel density. Widen above
// 1.0 only if validation shows shadow holes at an off-screen-caster boundary.
constexpr float kFeederSubSafetyFactor = 1.0f;

// Per-face-edge micro-grid cap for the #2258 Step-B shadow-feeder dispatch. An
// off-screen shadow caster only feeds the sun-shadow bake, so its stage-1
// trixel depth can raster a coarser strided micro-grid than the on-screen
// effSub² — as long as it still lands ≥ 1 sample per sun-map texel it covers
// (else the bake sees gaps ⇒ shadow holes). The cap reproduces the bake's
// per-cascade sun-space UV extent (sunBakeFrustumUVBounds) and returns the
// FINEST cascade's texels-per-world-unit, clamped to [1, effSub]. Because the
// bake frustum is dominated by the fixed ±256 iso-depth range + sweep (not the
// zoom-dependent viewport), the cap is ~zoom-independent: at high zoom
// effSub ≫ cap (the reduction Step B captures), at low zoom cap == effSub
// (byte-identical). @p sunDir is the cached frame sun direction (zero when
// shadows are off ⇒ no feeders are appended, so the returned effSub is moot but
// safe). Call once per canvas in VOXEL_TO_TRIXEL_STAGE_1's per-canvas tick.
inline int
feederSubCap(const IRMath::vec3 &sunDir, IRMath::CardinalIndex cardinalIndex, int effSub) {
    const int cappedEffSub = IRMath::max(effSub, 1);
    const float sunLen = IRMath::length(sunDir);
    if (sunLen <= 0.0f) {
        return cappedEffSub; // shadows off — feeder pass is empty, cap is moot
    }
    const IRMath::vec3 dir = sunDir / sunLen;
    IRMath::vec3 uHat, vHat;
    IRMath::buildOrthonormalBasis(dir, uHat, vHat);

    // Same iso viewport + sweep the bake fits its depth map to (margin 0).
    const auto &cull = IRRender::getCullViewport();
    const IRMath::IsoBounds2D isoBounds = cull.isoViewportForCanvas(cull.canvasSize_, 0);
    const IRMath::vec3 sweep = -dir * kSunShadowMaxDistance;

    const float splitDepth =
        kFeederIsoDepthMin + (kFeederIsoDepthMax - kFeederIsoDepthMin) * kFeederCascadeSplitRatio;
    const float cascadeMaxDepth[kFeederSunShadowCascadeCount] = {splitDepth, kFeederIsoDepthMax};

    int cap = 1;
    for (int ci = 0; ci < kFeederSunShadowCascadeCount; ++ci) {
        const IRMath::IsoBounds2D uv = sunBakeFrustumUVBounds(
            isoBounds,
            kFeederIsoDepthMin,
            cascadeMaxDepth[ci],
            uHat,
            vHat,
            dir,
            cardinalIndex,
            sweep
        );
        const float extent = IRMath::max(uv.max_.x - uv.min_.x, uv.max_.y - uv.min_.y);
        if (extent > 0.0f) {
            const float texelsPerWorldUnit = static_cast<float>(kFeederSunShadowMapDim) / extent;
            cap = IRMath::max(
                cap,
                static_cast<int>(IRMath::ceil(texelsPerWorldUnit * kFeederSubSafetyFactor))
            );
        }
    }
    return IRMath::clamp(cap, 1, cappedEffSub);
}

} // namespace IRPrefab::SunShadow

#endif /* IRREDEN_RENDER_SUN_SHADOW_CONSTANTS_H */
