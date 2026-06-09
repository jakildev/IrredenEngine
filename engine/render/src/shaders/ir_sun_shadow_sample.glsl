// Shared sun-shadow sampling: the FrameDataSun UBO (binding 29), the baked
// sun-aligned depth-map SSBO (binding 28), and the cascaded PCF lookup. Used by
// BOTH c_compute_sun_shadow (the per-world-pixel screen-space pass) and
// c_lighting_to_trixel (the opt-in detached re-voxelize world-receive path,
// #1576 P4b-2 — re-runs the same lookup at a detached voxel's recovered world
// pos). Kept in a dedicated include — NOT in ir_iso_common.glsl — so only the
// sun-shadow consumers recompile and the SDF / voxel / scatter shaders keep
// their cardinal-yaw byte-identity (same rationale as ir_per_axis_lighting.glsl).

const int kSunShadowMapDim = 1024;
const int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
const float kSunDepthScale = 1024.0;
const float kSunDepthOffset = 512.0;
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
    uniform float _cascadePad0;
    uniform float _cascadePad1;
};

layout(std430, binding = 28) readonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

float unpackSunDepth(uint packedDepth) {
    return float(packedDepth) / kSunDepthScale - kSunDepthOffset;
}

float sampleCascadeShadow(
    vec2 sunUV, float sunZ, vec3 normal, vec3 sunDir,
    vec2 origin, vec2 texelSz, int bufferOffset
) {
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
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
            float weight = mix(1.0 - frac.x, frac.x, float(dx))
                         * mix(1.0 - frac.y, frac.y, float(dy));
            float depthDiff = sunZ - nearestZ;
            if (depthDiff > bias && depthDiff - bias < kMaxShadowDepthRange)
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
float worldSunShadowFactor(vec3 pos3D, vec3 normal, float isoDepth) {
    vec3 sunDir = sunDirection.xyz;
    vec3 uHat = sunBasisU.xyz;
    vec3 vHat = sunBasisV.xyz;
    vec3 biasedPos3D = pos3D + normal * kNormalBiasVoxels;
    vec2 sunUV = vec2(dot(biasedPos3D, uHat), dot(biasedPos3D, vHat));
    float sunZ = -dot(biasedPos3D, sunDir);

    float shadowAccum;
    if (cascadeCount <= 1) {
        shadowAccum = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir,
            sunBufferOriginUV, sunBufferTexelSize, 0
        );
    } else {
        float distToSplit = isoDepth - cascadeSplitDepth;
        if (distToSplit < -kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_0, cascadeTexelSize_0, 0
            );
        } else if (distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_0, cascadeTexelSize_0, 0
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }
    return mix(1.0, kShadowDarken, shadowAccum);
}
