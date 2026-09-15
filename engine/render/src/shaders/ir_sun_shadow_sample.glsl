// Shared sun-shadow sampling: the FrameDataSun UBO (binding 29), the baked
// sun-aligned depth-map SSBO (binding 28), and the cascaded PCF lookup. Used by
// BOTH c_compute_sun_shadow (the per-world-pixel screen-space pass) and
// c_lighting_to_trixel (the opt-in detached re-voxelize world-receive path, which
// re-runs the same lookup at a detached voxel's recovered world pos). Deliberately
// NOT in ir_iso_common.glsl, so only the sun-shadow consumers recompile and the
// SDF / voxel / scatter shaders keep their cardinal-yaw byte-identity.
//
// Self-includes ir_sun_projection.glsl — the map-dim constants, the shared
// sunSpaceProject basis, unpackSunDepth, and sunCascadeKernelInterior — so the
// caster bake and this receiver lookup share one source. A wrapper's earlier
// include of ir_sun_projection.glsl makes this a suppressed duplicate.
#include "ir_sun_projection.glsl"

const float kNormalBiasVoxels = 0.5;
const float kShadowBiasTexelScale = 2.0;
const float kShadowBiasSlopeMin = 0.05;
const float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
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
    uniform float sunSplatMaxTexels;  // unused here (sun-map bake only)
    // Maximum shadow-throw window (sun-Z voxels). BAKE_SUN_SHADOW_MAP sets it
    // from kSunShadowMaxDistance — the SAME distance the feeder / bake AABB
    // sweep uses — so a baked caster is receivable at its full throw and the
    // two cannot drift.
    uniform float sunMaxShadowThrow;
};

layout(std430, binding = 28) readonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

float sampleCascadeShadow(
    vec2 sunUV, float sunZ, vec3 normal, vec3 sunDir, vec3 uHat, vec3 vHat,
    vec2 origin, vec2 texelSz, int bufferOffset, float maxShadowThrow
) {
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
    // Receiver tolerance accounts for slope, map resolution and depth quantization.
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    // Receiver-plane depth gradient in sun-UV, used only by the splat-tap
    // same-plane test. Derivation: with (uHat, vHat, sunDir) orthonormal
    // and depth z = -dot(P, sunDir), a displacement dP within the receiver plane
    // (dot(dP, normal) = 0) gives dz/du = dot(uHat, normal)/dot(sunDir, normal)
    // and likewise for v; dot(sunDir, normal) is exactly `slope`. So the plane's
    // depth at sun-UV coordinate q is sunZ + dot(gradUV, q - sunUV). (Sign is +:
    // for a coplanar occluder at the write's origin this must reproduce that
    // occluder's own depth, so the splat tap's h is ~0 and it stays lit. The
    // base bias absorbs the sub-texel origin-quantization error, a texel*|grad|
    // term.)
    vec2 gradUV = vec2(dot(normal, uHat), dot(normal, vHat)) / slope;

    // Map samples lie at texel centers; integer coordinates address those samples.
    vec2 sunPxF = (sunUV - origin) / texelSz - 0.5;
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
            float weight = mix(1.0 - frac.x, frac.x, float(dx))
                         * mix(1.0 - frac.y, frac.y, float(dy));
            // Far shadow-throw window — raw sun-Z gap to the stored occluder, on
            // BOTH tap regimes. maxShadowThrow == the feeder / bake sweep
            // (kSunShadowMaxDistance) so a baked caster is receivable at its full
            // throw.
            float depthDiff = sunZ - nearestZ;
            if (depthDiff - bias >= maxShadowThrow) continue;

            if (sunWriteIsDirect(stored)) {
                // Direct writes compare caster depth without splat-plane recovery.
                if (depthDiff > bias) shadowAccum += weight;
            } else {
                // Coverage-SPLAT neighbour: the winning depth was written from a
                // caster `offset` texels away. Reconstruct that origin and test
                // whether the occluder lies in the RECEIVER's OWN plane. A same-face
                // self-hit extrapolates to ~nearestZ (h ~ 0 -> lit, at ANY splat
                // distance and face tilt). A genuine cast sits far above the plane
                // (h ~ caster height >> bias -> shadow at the base tolerance, no
                // widening, whatever the caster-floor gap).
                ivec2 offset = unpackSunSplatOffset(stored);
                ivec2 originTexel = px - offset;
                vec2 originUV = origin + (vec2(originTexel) + 0.5) * texelSz;
                float expectedZ = sunZ + dot(gradUV, originUV - sunUV);
                float h = expectedZ - nearestZ;
                if (h > bias) shadowAccum += weight;
            }
        }
    }
    return shadowAccum;
}

// Direct-sun visibility at a world-space surface: 1.0 lit, 0.0 occluded.
// Ambient lighting is composed separately. isoDepth selects/blends cascades.
float worldSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth) {
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    // Shared caster/receiver projection — the bake derives every
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
            sunUV, sunZ, normal, sunDir, uHat, vHat,
            sunBufferOriginUV, sunBufferTexelSize, 0, sunMaxShadowThrow
        );
    } else {
        float distToSplit = isoDepth - cascadeSplitDepth;
        // Covering-cascade fallback: the near cascade is valid for
        // this receiver only where its PCF kernel sits interior to the map —
        // its AABB was built from a depth-capped corner set, so a receiver
        // near the map edge (screen corners, the split blend band past the
        // cap) can straddle a region where taps fall out of bounds and the
        // matching casters were bounds-dropped by the bake. Selecting the
        // covering far cascade there trades texel resolution for a complete
        // kernel instead of silently reading the missing region as "lit"
        // (partial face dropout). The gate is per-receiver-UV, uniform across
        // a voxel's faces, so a straddling voxel's faces select consistently.
        bool nearInterior =
            sunCascadeKernelInterior(sunUV, cascadeOriginUV_0, cascadeTexelSize_0);
        if (nearInterior && distToSplit < -kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                cascadeOriginUV_0, cascadeTexelSize_0, 0, sunMaxShadowThrow
            );
        } else if (!nearInterior || distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, sunMaxShadowThrow
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                cascadeOriginUV_0, cascadeTexelSize_0, 0, sunMaxShadowThrow
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir, uHat, vHat,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, sunMaxShadowThrow
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }
    return 1.0 - shadowAccum;
}
