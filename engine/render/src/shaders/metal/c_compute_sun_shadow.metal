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
constant float kShadowBiasTexelScale = 1.0;
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

    // Screen-space lookup against the bake output. sunZ is negated
    // (smaller = closer to sun) so the bake's atomicMin stores the
    // nearest blocker per texel.
    float3 sunDir = sunFrameData.sunDirection.xyz;
    float3 uHat = sunFrameData.sunBasisU.xyz;
    float3 vHat = sunFrameData.sunBasisV.xyz;
    float2 sunUV = float2(dot(pos3D, uHat), dot(pos3D, vHat));
    float sunZ = -dot(pos3D, sunDir);

    int2 sunPx = int2(round(
        (sunUV - sunFrameData.sunBufferOriginUV) /
        sunFrameData.sunBufferTexelSize
    ));
    bool shadowed = false;
    if (sunPx.x >= 0 && sunPx.x < kSunShadowMapDim &&
        sunPx.y >= 0 && sunPx.y < kSunShadowMapDim) {
        uint storedPacked = sunDepthBuf[sunPx.y * kSunShadowMapDim + sunPx.x];
        if (storedPacked != 0xFFFFFFFFu) {
            float nearestZ = unpackSunDepth(storedPacked);
            int face = encoded & 3;
            float3 normal = faceOutwardNormal(face);
            float slope = max(kShadowBiasSlopeMin, dot(normal, sunDir));
            float texelSize = max(
                sunFrameData.sunBufferTexelSize.x,
                sunFrameData.sunBufferTexelSize.y
            );
            float bias =
                texelSize * kShadowBiasTexelScale / slope + kShadowBiasQuantNoise;
            shadowed = (sunZ - nearestZ) > bias;
        }
    }
    float factor = shadowed ? kShadowDarken : 1.0;
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
