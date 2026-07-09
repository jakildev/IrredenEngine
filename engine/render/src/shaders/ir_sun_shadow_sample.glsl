// Shared sun-shadow sampling: the FrameDataSun UBO (binding 29), the baked
// sun-aligned depth-map SSBO (binding 28), and the cascaded PCF lookup. Used by
// BOTH c_compute_sun_shadow (the per-world-pixel screen-space pass) and
// c_lighting_to_trixel (the opt-in detached re-voxelize world-receive path,
// #1576 P4b-2 — re-runs the same lookup at a detached voxel's recovered world
// pos). Kept in a dedicated include — NOT in ir_iso_common.glsl — so only the
// sun-shadow consumers recompile and the SDF / voxel / scatter shaders keep
// their cardinal-yaw byte-identity (same rationale as ir_per_axis_lighting.glsl).
//
// Requires ir_sun_projection.glsl included FIRST by the top-level shader (the
// GLSL include resolver is non-recursive): the map-dim constants, the shared
// sunSpaceProject basis, unpackSunDepth, and sunCascadeKernelInterior all
// live there so the caster bake and this receiver lookup share one source
// (#2083).

const float kShadowDarken = 0.45;
const float kNormalBiasVoxels = 0.5;
const float kShadowBiasTexelScale = 2.0;
const float kShadowBiasSlopeMin = 0.05;
const float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
// Reject shadows from occluders farther than 24 voxels in sun-Z.
// Prevents adjacent volumes from incorrectly casting onto faces they
// are beside rather than in front of.
const float kMaxShadowDepthRange = 24.0;
const float kCascadeBlendRange = 8.0;

layout(std140, binding = 29) uniform FrameDataSun {
    uniform vec4 sunDirection;
    uniform float sunIntensity;
    uniform float sunAmbient;
    uniform int shadowsEnabled;
    uniform int aoEnabled;
    uniform vec4 sunBasisU;
    uniform vec4 sunBasisV;
    uniform vec2 sunBufferOriginUV;
    uniform vec2 sunBufferTexelSize;
    uniform vec2 cascadeOriginUV_0;
    uniform vec2 cascadeTexelSize_0;
    uniform vec2 cascadeOriginUV_1;
    uniform vec2 cascadeTexelSize_1;
    uniform float cascadeSplitDepth;
    uniform int cascadeCount;
    uniform float sunSplatMaxTexels;  // #2270; unused here (sun-map bake only)
    uniform float _cascadePad1;
};

layout(std430, binding = 28) readonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

float sampleCascadeShadow(
    vec2 sunUV, float sunZ, vec3 normal, vec3 sunDir,
    vec2 origin, vec2 texelSz, int bufferOffset, float selfStepDepthRange
) {
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
    // Base receiver bias — the trustworthy near-rejection for a DIRECT sun-map
    // write (splatDist 0). The far window (kMaxShadowDepthRange) always uses
    // this base `bias`; the NEAR rejection is recomputed per tap below.
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    vec2 sunPxF = (sunUV - origin) / texelSz;
    ivec2 base = ivec2(floor(sunPxF));
    vec2 frac = sunPxF - vec2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            ivec2 px = base + ivec2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[bufferOffset + px.y * kSunShadowMapDim + px.x];
            if (stored == 0xFFFFFFFFu) continue;
            float nearestZ = unpackSunDepth(stored);
            // Per-tap near-rejection (#2319 splat provenance + #2010 staircase
            // carve). The #2270 coverage splat may have written this texel from
            // a caster up to `splatDist` sun texels away, so its stored depth is
            // trustworthy only to within that displacement — widen the near
            // reject by splatDist texel-scales. A tilted sun-facing face's
            // SAME-face self-occluder is a nearby splat neighbour (small depth
            // gap → skipped); a genuine DIFFERENT-face cast occluder has a large
            // caster-to-receiver depth gap → still shadows. A direct write
            // (splatDist 0) keeps the base scale, so every non-splatted receiver
            // path is byte-identical. selfStepDepthRange (#2010, 0 except on a
            // detected round-to-cell staircase riser) lifts the near reject as
            // before. The kMaxShadowDepthRange window below keeps the base
            // `bias`, so genuine far contact shadows still register.
            float splatDist = float(unpackSunSplatDist(stored));
            float tapBias = texelSize * (kShadowBiasTexelScale + splatDist) / slope
                          + kShadowBiasQuantNoise;
            float nearReject = max(tapBias, selfStepDepthRange);
            float weight = mix(1.0 - frac.x, frac.x, float(dx))
                         * mix(1.0 - frac.y, frac.y, float(dy));
            float depthDiff = sunZ - nearestZ;
            if (depthDiff > nearReject && depthDiff - bias < kMaxShadowDepthRange)
                shadowAccum += weight;
        }
    }
    return shadowAccum;
}

// Per-surface sun-shadow brightness factor for a WORLD-space surface point +
// world normal, selecting the cascade by iso depth and blending across the
// split. `isoDepth` is the surface's world iso depth (x+y+z under the (1,1,1)
// axis). Returns 1.0 (fully lit) … kShadowDarken (fully shadowed). This is the
// per-pixel cascade body of c_compute_sun_shadow's main() lifted verbatim so
// both that pass and the detached world-receive path (#1576) share one source.
// `selfStepDepthRange` (#2010) is the near-rejection lift for a round-to-cell
// staircase riser (0 = no lift = pre-#2010 behaviour); threaded to every
// cascade sample so it applies regardless of which cascade the receiver lands in.
float worldSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth, float selfStepDepthRange) {
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    // Shared caster/receiver projection (#2083) — the bake derives every
    // caster's sun UV + depth from this same function, so cast and receive
    // cannot drift.
    vec3 sunProj = sunSpaceProject(
        pos3D + normal * kNormalBiasVoxels, uHat, vHat, sunDir
    );
    vec2 sunUV = sunProj.xy;
    float sunZ = sunProj.z;

    float shadowAccum;
    if (cascadeCount <= 1) {
        shadowAccum = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir,
            sunBufferOriginUV, sunBufferTexelSize, 0, selfStepDepthRange
        );
    } else {
        float distToSplit = isoDepth - cascadeSplitDepth;
        // Covering-cascade fallback (#2083): the near cascade is valid for
        // this receiver only where its PCF kernel sits interior to the map —
        // its AABB was built from a depth-capped corner set, so a receiver
        // near the map edge (screen corners, the split blend band past the
        // cap) can straddle a region where taps fall out of bounds and the
        // matching casters were bounds-dropped by the bake. Selecting the
        // covering far cascade there trades texel resolution for a complete
        // kernel instead of silently reading the missing region as "lit"
        // (partial face dropout). The gate is per-receiver-UV, uniform across
        // a voxel's faces, so a straddling voxel's faces select consistently.
        // Interior receivers take exactly the pre-#2083 branches.
        bool nearInterior =
            sunCascadeKernelInterior(sunUV, cascadeOriginUV_0, cascadeTexelSize_0);
        if (nearInterior && distToSplit < -kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_0, cascadeTexelSize_0, 0, selfStepDepthRange
            );
        } else if (!nearInterior || distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, selfStepDepthRange
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_0, cascadeTexelSize_0, 0, selfStepDepthRange
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, selfStepDepthRange
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }
    return mix(1.0, kShadowDarken, shadowAccum);
}

// Default 3-arg form — full self-occlusion (no staircase carve). Used by the
// detached world-receive path (c_lighting_to_trixel, #1576) and any caller that
// has no per-receiver staircase signal; byte-identical to pre-#2010 master.
float worldSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth) {
    return worldSunShadowFactor(pos3D, normal, isoDepth, 0.0);
}
