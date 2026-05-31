#include "ir_iso_common.metal"
#include <metal_atomic>

// Mirrors shaders/c_bake_sun_shadow_map.glsl. Projects each rasterized
// iso pixel into both cascade regions of the sun shadow depth buffer.

constant int kEmptyDistanceEncoded = 65535;
constant int kSunShadowMapDim = 1024;
constant int kCascadeTexelCount = kSunShadowMapDim * kSunShadowMapDim;
constant float kSunDepthScale = 1024.0;
constant float kSunDepthOffset = 512.0;

inline uint packSunDepth(float sunZ) {
    float biased = clamp(sunZ + kSunDepthOffset, 0.0, kSunDepthOffset * 2.0);
    return uint(biased * kSunDepthScale);
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

inline void bakeCascade(
    float2 sunUV, float sunZ, float2 origin, float2 texelSz,
    int cascadeOffset, device atomic_uint *sunDepthBuf
) {
    int2 sunPx = int2(floor((sunUV - origin) / texelSz));
    if (sunPx.x < 0 || sunPx.x >= kSunShadowMapDim ||
        sunPx.y < 0 || sunPx.y >= kSunShadowMapDim) {
        return;
    }
    uint packedDepth = packSunDepth(sunZ);
    atomic_fetch_min_explicit(
        &sunDepthBuf[cascadeOffset + sunPx.y * kSunShadowMapDim + sunPx.x],
        packedDepth,
        memory_order_relaxed
    );
}

kernel void c_bake_sun_shadow_map(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device atomic_uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
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

    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 uHat = sunFrameData.sunBasisU.xyz;
    float3 vHat = sunFrameData.sunBasisV.xyz;
    float2 sunUV = float2(dot(pos3D, uHat), dot(pos3D, vHat));
    float sunZ = -dot(pos3D, sunDir);

    bakeCascade(sunUV, sunZ, sunFrameData.cascadeOriginUV_0,
                sunFrameData.cascadeTexelSize_0, 0, sunDepthBuf);
    bakeCascade(sunUV, sunZ, sunFrameData.cascadeOriginUV_1,
                sunFrameData.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf);
}
