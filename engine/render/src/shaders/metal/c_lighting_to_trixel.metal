#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"

// Mirrors shaders/c_lighting_to_trixel.glsl. Screen-space lighting
// application pass — modulates trixelColors.rgb by (AO × sun-shadow),
// with an optional LUT palette shading path keyed off lutEnabled and
// an optional flood-fill light-volume additive contribution keyed off
// lightVolumeEnabled. When hdrEnabled is set, computes in unclamped
// float precision, adds the sky-term contribution, applies exposure,
// and tonemaps via the ACES Filmic curve before writing back to the
// canvas.

struct FrameDataLightingToTrixel {
    int   lightingEnabled;
    int   lutEnabled;
    int   lightVolumeEnabled;
    float debugLightLevel;
    int   debugOverlayMode;
    int   hdrEnabled;
    float exposure;
    float skyIntensity;
    float4 skyColor;
};

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

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
constant float kLightVolumeSize = 128.0;
constant float kLightVolumeHalfExtent = 64.0;

// Layout must match the propagate/seed UBO layout so the shared buffer
// binding works. Lighting only reads `worldOriginVoxel`.
struct LightVolumeParams {
    int   _gridSize;
    int   _halfExtent;
    int   _lightCount;
    float _stepFalloff;
    int4  worldOriginVoxel;
};

// ACES Filmic tone mapping (Stephen Hill's fitted curve).
float3 ACESFilm(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}

kernel void c_lighting_to_trixel(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    constant FrameDataVoxelToTrixel& voxelFrameData [[buffer(7)]],
    constant FrameDataSun& sunFrameData [[buffer(29)]],
    constant LightVolumeParams& lightVolumeParams [[buffer(23)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasAO [[texture(2)]],
    texture2d<float, access::sample> paletteLUT [[texture(3)]],
    // Unit 4 — Metal flattens texture/image tables into a shared slot
    // space; cannot collide with paletteLUT(3) or lightVolume(5).
    texture2d<float, access::read> canvasSunShadow [[texture(4)]],
    texture3d<float, access::sample> lightVolume [[texture(5)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    if (frameData.lightingEnabled == 0) {
        return;
    }

    const int2 pixel = int2(globalId.xy);
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= 65535) {
        return;
    }

    const float  ao     = canvasAO.read(uint2(pixel)).r;
    const float  shadow = canvasSunShadow.read(uint2(pixel)).r;
    const float4 src    = trixelColors.read(uint2(pixel));

    if (frameData.debugOverlayMode != 0) {
        float3 debugColor = float3(0.0f);
        if (frameData.debugOverlayMode == 1) {
            debugColor = float3(1.0f - ao, ao, 0.0f);
        } else if (frameData.debugOverlayMode == 2) {
            const float level = ao * shadow;
            debugColor = float3(level, level, 1.0f);
        } else {
            debugColor = shadow >= 0.999f ? float3(0.0f) : float3(1.0f, 0.0f, 1.0f);
        }
        trixelColors.write(float4(debugColor, src.a), uint2(pixel));
        return;
    }

    const int rawDepth = encoded >> 2;
    // Decode the visible-triplet slot (0/1/2) and resolve to the world
    // FaceId via `visibleFaceIds[slot]` (#1278). Six-face outward normal
    // is in the world frame; sun direction is world frame; Lambert is a
    // plain dot product without rotation. Mirrors GLSL.
    const int slot = encoded & 3;
    const int faceId = voxelFrameData.visibleFaceIds[slot];
    int cardinalIndex = rasterYawCardinalIndex(voxelFrameData.rasterYaw);
    float3 worldNormal = faceOutwardNormal6(faceId);
    const float lambert = max(0.0f, dot(worldNormal, sunFrameData.sunDirection.xyz));
    const float faceFactor =
        mix(sunFrameData.sunAmbient, 1.0f, lambert) * sunFrameData.sunIntensity;

    float3 baseRgb;
    if (frameData.lutEnabled == 0) {
        baseRgb = src.rgb * ao * shadow * faceFactor;
    } else {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        const float  luminance = dot(src.rgb, float3(0.299f, 0.587f, 0.114f));
        const float4 lut       = paletteLUT.sample(s, float2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * shadow * faceFactor;
    }

    if (frameData.lightVolumeEnabled != 0) {
        // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
        // face-locally; the single canvas uses the cardinal-snap reconstruction.
        // The shared world light volume is sampled the same way for both. Mirrors GLSL.
        float3 pos3D = voxelFrameData.perAxisRoute != 0
            ? perAxisCellToWorld3D(
                  pixel, rawDepth, faceId, size,
                  voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions
              )
            : trixelCanvasPixelToWorld3D(
                  pixel,
                  rawDepth,
                  voxelFrameData.trixelCanvasOffsetZ1,
                  voxelFrameData.frameCanvasOffset,
                  voxelFrameData.voxelRenderOptions,
                  voxelFrameData.rasterYaw
              );

        constexpr sampler volumeSampler(
            filter::nearest, address::clamp_to_edge
        );
        const float3 localPos =
            pos3D - float3(lightVolumeParams.worldOriginVoxel.xyz);
        const float3 sampleCoord =
            (localPos + float3(kLightVolumeHalfExtent) + float3(0.5)) /
            float3(kLightVolumeSize);
        const float4 lightSample = lightVolume.sample(volumeSampler, sampleCoord);
        const float3 light = lightSample.rgb * lightSample.a;
        baseRgb = baseRgb + src.rgb * light;
    }

    if (frameData.hdrEnabled != 0) {
        if (frameData.skyIntensity > 0.0f) {
            float skyFactor = max(0.0f, worldNormal.z);
            baseRgb += frameData.skyColor.rgb * frameData.skyIntensity * skyFactor * ao;
        }
        baseRgb = ACESFilm(baseRgb * frameData.exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0f, 1.0f);
    }

    trixelColors.write(float4(baseRgb, src.a), uint2(pixel));
}
