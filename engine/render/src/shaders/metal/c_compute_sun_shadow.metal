#include "ir_iso_common.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute with cascaded shadow maps.

constant int kEmptyDistanceEncoded = 65535;
constant float kShadowDarken = 0.45;

constant int kSunShadowMapDim = 1024;
constant int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
constant float kSunDepthScale = 1024.0;
constant float kSunDepthOffset = 512.0;
constant float kNormalBiasVoxels = 0.5;
constant float kShadowBiasTexelScale = 2.0;
constant float kShadowBiasSlopeMin = 0.05;
constant float kShadowBiasQuantNoise = 4.0 / kSunDepthScale;
// Reject shadows from occluders farther than 24 voxels in sun-Z.
// Prevents adjacent volumes from incorrectly casting onto faces they
// are beside rather than in front of.
constant float kMaxShadowDepthRange = 24.0;
constant float kCascadeBlendRange = 8.0;

inline float unpackSunDepth(uint packedDepth) {
    return float(packedDepth) / kSunDepthScale - kSunDepthOffset;
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
    float2 cascadeOriginUV_0;
    float2 cascadeTexelSize_0;
    float2 cascadeOriginUV_1;
    float2 cascadeTexelSize_1;
    float cascadeSplitDepth;
    int cascadeCount;
    float _cascadePad0;
    float _cascadePad1;
};

inline float sampleCascadeShadow(
    float2 sunUV, float sunZ, float3 normal, float3 sunDir,
    float2 origin, float2 texelSz, int bufferOffset,
    device const uint *sunDepthBuf
) {
    float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
    float texelSize = max(texelSz.x, texelSz.y);
    float bias = texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;

    float2 sunPxF = (sunUV - origin) / texelSz;
    int2 base = int2(floor(sunPxF));
    float2 frac = sunPxF - float2(base);
    float shadowAccum = 0.0;
    for (int dy = 0; dy < 2; ++dy) {
        for (int dx = 0; dx < 2; ++dx) {
            int2 px = base + int2(dx, dy);
            if (px.x < 0 || px.x >= kSunShadowMapDim ||
                px.y < 0 || px.y >= kSunShadowMapDim) continue;
            uint stored = sunDepthBuf[bufferOffset + px.y * kSunShadowMapDim + px.x];
            if (stored == 0xFFFFFFFFu) continue;
            float nearestZ = unpackSunDepth(stored);
            float weight = mix(1.0f - frac.x, frac.x, float(dx))
                         * mix(1.0f - frac.y, frac.y, float(dy));
            float depthDiff = sunZ - nearestZ;
            if (depthDiff > bias && depthDiff - bias < kMaxShadowDepthRange)
                shadowAccum += weight;
        }
    }
    return shadowAccum;
}

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

    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 uHat = sunFrameData.sunBasisU.xyz;
    float3 vHat = sunFrameData.sunBasisV.xyz;
    float3 biasedPos3D = pos3D + normal * kNormalBiasVoxels;
    float2 sunUV = float2(dot(biasedPos3D, uHat), dot(biasedPos3D, vHat));
    float sunZ = -dot(biasedPos3D, sunDir);

    float isoDepth = float(rawDepth);
    float shadowAccum;

    if (sunFrameData.cascadeCount <= 1) {
        shadowAccum = sampleCascadeShadow(
            sunUV, sunZ, normal, sunDir,
            sunFrameData.sunBufferOriginUV, sunFrameData.sunBufferTexelSize,
            0, sunDepthBuf
        );
    } else {
        float distToSplit = isoDepth - sunFrameData.cascadeSplitDepth;
        if (distToSplit < -kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                sunFrameData.cascadeOriginUV_0, sunFrameData.cascadeTexelSize_0,
                0, sunDepthBuf
            );
        } else if (distToSplit > kCascadeBlendRange) {
            shadowAccum = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                sunFrameData.cascadeOriginUV_1, sunFrameData.cascadeTexelSize_1,
                kCascadeTexelCount, sunDepthBuf
            );
        } else {
            float nearShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                sunFrameData.cascadeOriginUV_0, sunFrameData.cascadeTexelSize_0,
                0, sunDepthBuf
            );
            float farShadow = sampleCascadeShadow(
                sunUV, sunZ, normal, sunDir,
                sunFrameData.cascadeOriginUV_1, sunFrameData.cascadeTexelSize_1,
                kCascadeTexelCount, sunDepthBuf
            );
            float t = smoothstep(-kCascadeBlendRange, kCascadeBlendRange, distToSplit);
            shadowAccum = mix(nearShadow, farShadow, t);
        }
    }

    float factor = mix(1.0f, kShadowDarken, shadowAccum);
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
