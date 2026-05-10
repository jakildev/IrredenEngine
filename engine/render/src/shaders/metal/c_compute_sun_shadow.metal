#include "ir_iso_common.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute — reconstructs the voxel-space surface position for
// each rasterized pixel, projects into the sun-aligned depth map baked
// by BAKE_SUN_SHADOW_MAP, and writes a 0..1 brightness factor into
// canvasSunShadow.r.

constant int kEmptyDistanceEncoded = 65535;
constant float kShadowDarken = 0.45;

// Must match c_bake_sun_shadow_map.metal.
constant int kSunShadowMapDim = 1024;
constant float kSunDepthScale = 1024.0;
constant float kSunDepthOffset = 512.0;
// Normal-bias offset pushes the lookup point along the face's outward
// normal before projecting into sun-space, preventing self-shadow acne on
// cube tops and SDF spheres caused by adjacent-face pixels rounding to the
// same sun-texel. Tune via render-debug-loop on shape_debug.
constant float kNormalBiasVoxels = 0.5;
// Slope-scale bias covers the worst-case sunZ variation between iso pixels
// that share a sun-space texel — roughly texelSize/slope voxels. Below
// that threshold a flat surface self-shadows. Tune via render-debug-loop on shape_debug.
constant float kShadowBiasTexelScale = 2.0;
constant float kShadowBiasSlopeMin = 0.05;
constant float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;

inline float unpackSunDepth(uint packed) {
    return float(packed) / kSunDepthScale - kSunDepthOffset;
}

struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int aoEnabled;
    float4 sunBasisU;
    float4 sunBasisV;
    float2 sunBufferOriginUV;
    float2 sunBufferTexelSize;
};

kernel void c_compute_sun_shadow(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.shadowsEnabled == 0) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    int rawDepth = encoded >> 2;

    // sunDirection stays in world coordinates (camera-independent), so only
    // the surface position needs the R(-rasterYaw) compose. At
    // cardinalIndex==0 the path collapses to master so yaw=0 stays
    // byte-identical.
    float3 pos3D = trixelCanvasPixelToWorld3D(
        pixel,
        rawDepth,
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );

    int face = encoded & 3;
    float3 normal = faceOutwardNormal(face);

    // Screen-space lookup against the bake output. sunZ is negated
    // (smaller = closer to sun) so the bake's atomicMin stores the
    // nearest blocker per texel. The lookup position is offset along the
    // outward normal first so the projected sun-texel shifts away from the
    // true-surface writer, eliminating self-shadow acne without biasing the
    // baked depth map itself.
    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 uHat = sunFrameData.sunBasisU.xyz;
    float3 vHat = sunFrameData.sunBasisV.xyz;
    float3 biasedPos3D = pos3D + normal * kNormalBiasVoxels;
    float2 sunUV = float2(dot(biasedPos3D, uHat), dot(biasedPos3D, vHat));
    float sunZ = -dot(biasedPos3D, sunDir);

    // Bias depends only on per-fragment constants (face/normal/sunDir,
    // texelSize) — hoisted outside the PCF loop. T-132's slope-scale +
    // quantisation-noise formula stays unchanged; the PCF kernel just
    // applies it to each of the four taps.
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(
        sunFrameData.sunBufferTexelSize.x,
        sunFrameData.sunBufferTexelSize.y
    );
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    // 2×2 PCF: bilinearly weighted sample of four neighboring sun-space texels.
    // Fades shadow boundaries across one sun-texel instead of cliff-edging.
    // floor() pairs with the bake's round() convention (texel centers on
    // integers) so the four taps surround the fragment's continuous sunPxF.
    float2 sunPxF = (sunUV - sunFrameData.sunBufferOriginUV) /
                    sunFrameData.sunBufferTexelSize;
    int2 base = int2(floor(sunPxF));
    float2 frac = sunPxF - float2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int2 px = base + int2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[px.y * kSunShadowMapDim + px.x];
            if (stored == 0xFFFFFFFFu) continue;  // no caster → lit
            float nearestZ = unpackSunDepth(stored);
            float weight = mix(1.0f - frac.x, frac.x, float(dx))
                         * mix(1.0f - frac.y, frac.y, float(dy));
            if ((sunZ - nearestZ) > bias) shadowAccum += weight;
        }
    }
    float factor = mix(1.0f, kShadowDarken, shadowAccum);
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
