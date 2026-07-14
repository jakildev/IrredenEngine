#include "ir_iso_common.metal"        // decode*, faceOutwardNormal6, unpack/packColor,
                                     // FrameDataVoxelToTrixel (with overflowScratchLayout)
#include "ir_per_axis_lighting.metal" // perAxisCellToWorld3D
#include "ir_sun_shadow_sample.metal" // FrameDataSun + worldSunShadowFactor()

// Mirrors shaders/c_light_overflow_faces.glsl — view-visibility overflow-face
// lighting (#2334, epic #2331 phase C2). Dispatched inside LIGHTING_TO_TRIXEL
// after the per-axis CELL lighting, this pass relights each C1 (#2333) overflow
// entry at its recovered WORLD position (sun cascade + light-volume + Lambert,
// AO = 1.0 — same world sample as c_lighting_to_trixel) and rewrites the entry's
// stored colorPacked in place, so the unchanged scatter draws LIT slivers while
// rotating. Runs ONLY while rotating; the cardinal fast path is byte-identical.

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

struct LightVolumeParams {
    int   _gridSize;
    int   _halfExtent;
    int   _lightCount;
    float _stepFalloff;
    int4  worldOriginVoxel;
};

// --- shared world-lighting primitives -----------------------------------------
// These MIRROR c_lighting_to_trixel.metal (kept in lockstep). Duplicated rather
// than extracted so the shipped lighting kernel stays byte-identical — a future
// simplify pass can hoist both into a shared ir_world_lighting.metal include.
constant float kLightVolumeSize = 128.0;
constant float kLightVolumeHalfExtent = 64.0;
constant int   kLightTypeSpot = 3;
constant float kConeEdgeSoftness = 1.15f;
constant float kDegToRad = 3.14159265358979323846f / 180.0f;

struct GPULightSource {
    float4 originAndType;
    float4 colorAndIntensity;
    float4 directionAndRadius;
    float4 coneAndSeedAlpha;
    float4 trueOriginVoxel;
};

float spotConeFactor(device const GPULightSource* lights, int lightIdx, float3 pos3D) {
    const GPULightSource L = lights[lightIdx];
    const float3 axis = normalize(L.directionAndRadius.xyz);
    const float3 toCell = pos3D - L.trueOriginVoxel.xyz;
    const float toCellLen = length(toCell);
    if (toCellLen < 1e-4f) {
        return 1.0f;
    }
    const float cosToCell = dot(toCell / toCellLen, axis);
    const float halfAngle = L.coneAndSeedAlpha.x * 0.5f * kDegToRad;
    const float cosInner = cos(halfAngle);
    const float cosOuter = cos(min(halfAngle * kConeEdgeSoftness, 90.0f * kDegToRad));
    return smoothstep(cosOuter, cosInner, cosToCell);
}

float3 ACESFilm(float3 x) {
    const float a = 2.51f;
    const float b = 0.03f;
    const float c = 2.43f;
    const float d = 0.59f;
    const float e = 0.14f;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0f, 1.0f);
}
// -----------------------------------------------------------------------------

kernel void c_light_overflow_faces(
    constant FrameDataLightingToTrixel& frameData [[buffer(27)]],
    constant FrameDataVoxelToTrixel& voxelFrameData [[buffer(7)]],
    constant FrameDataSun& sunFrameData [[buffer(29)]],
    constant LightVolumeParams& lightVolumeParams [[buffer(23)]],
    device const uint* sunDepthBuf [[buffer(28)]],
    device const GPULightSource* lights [[buffer(4)]],
    // Overflow entry list + ctrl block (per-axis resolve scratch) bound at
    // kBufferIndex_OverflowLightingScratch — a slot dead during lighting; slot 28
    // holds the sun-depth map this pass samples. Read entries + rewrite color.
    device uint* overflowScratch [[buffer(8)]],
    texture2d<float, access::sample> paletteLUT [[texture(3)]],
    texture3d<float, access::sample> lightVolume [[texture(5)]],
    texture3d<float, access::read> lightVolumeId [[texture(7)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    // The dispatch is a 2-D group grid (voxelDispatchGridForCount wraps past
    // 1024 groups), so flatten the group coordinates to the entry index.
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint gid = workGroupIndex * 64u + localId.x;
    const int4 layout = voxelFrameData.overflowScratchLayout;
    // Live entry count (ctrl instanceCount at ctrlBase + 1); dispatch grid is
    // sized to the worst-case cap, so threads past the count early-return.
    const uint entryCount = overflowScratch[uint(layout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }
    // UNLIT / debug overlays leave overflow slivers as raw albedo (see GLSL twin).
    if (frameData.lightingEnabled == 0 || frameData.debugOverlayMode != 0) {
        return;
    }

    const uint entryBase = uint(layout.z) + gid * 3u;
    const uint packedCell = overflowScratch[entryBase + 0u];
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int  rawDist = int(overflowScratch[entryBase + 2u]);

    const float4 albedo = unpackColor(colorPacked);
    if (albedo.a < 0.1f) {
        return;   // cleared slot — never relight/repack (scatter degenerates it)
    }

    // Recover cardinal store cell → world FaceId + world pos, bit-for-bit the
    // same decode/recovery the scatter's overflow branch + per-axis cell lighting
    // use. The sub-cell frac offset is a scatter-geometry detail, irrelevant to
    // world-cell-resolution sun/volume sampling.
    const int2 cell = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int rawDepth = decodeDepthPerAxis(rawDist);
    const int faceId = voxelFrameData.visibleFaceIds[slot] ^ flip;
    const float3 worldNormal = faceOutwardNormal6(faceId);
    const float3 pos3D = perAxisCellToWorld3D(
        cell, rawDepth, faceId, voxelFrameData.canvasSizePixels,
        voxelFrameData.frameCanvasOffset, voxelFrameData.voxelRenderOptions
    );

    const float ao = 1.0f;
    const float shadow = sunFrameData.shadowsEnabled != 0
        ? worldSunShadowFactor(pos3D, worldNormal, float(rawDepth), sunFrameData, sunDepthBuf)
        : 1.0f;
    const float lambert = max(0.0f, dot(worldNormal, sunFrameData.sunDirection.xyz));
    const float faceFactor =
        (sunFrameData.sunAmbient + (1.0f - sunFrameData.sunAmbient) * lambert * shadow) *
        sunFrameData.sunIntensity;

    float3 baseRgb;
    if (frameData.lutEnabled == 0) {
        baseRgb = albedo.rgb * ao * faceFactor;
    } else {
        constexpr sampler s(filter::nearest, address::clamp_to_edge);
        const float luminance = dot(albedo.rgb, float3(0.299f, 0.587f, 0.114f));
        const float4 lut = paletteLUT.sample(s, float2(ao, luminance));
        baseRgb = albedo.rgb * lut.rgb * faceFactor;
    }

    if (frameData.lightVolumeEnabled != 0) {
        constexpr sampler volumeSampler(filter::nearest, address::clamp_to_edge);
        const float3 localPos = pos3D - float3(lightVolumeParams.worldOriginVoxel.xyz);
        const float3 sampleCoord =
            (localPos + float3(kLightVolumeHalfExtent) + float3(0.5)) / float3(kLightVolumeSize);
        const float4 lightSample = lightVolume.sample(volumeSampler, sampleCoord);
        float3 light = lightSample.rgb * lightSample.a;
        if (lightVolumeParams.worldOriginVoxel.w != 0) {
            const int3 idCell = int3(floor(localPos + float3(kLightVolumeHalfExtent) + float3(0.5)));
            if (all(idCell >= int3(0)) && all(idCell < int3(int(kLightVolumeSize)))) {
                const int winId = int(round(lightVolumeId.read(uint3(idCell)).r * 255.0f));
                if (winId > 0 && int(lights[winId - 1].originAndType.w) == kLightTypeSpot) {
                    light *= spotConeFactor(lights, winId - 1, pos3D);
                }
            }
        }
        baseRgb = baseRgb + albedo.rgb * light;
    }

    if (frameData.hdrEnabled != 0) {
        if (frameData.skyIntensity > 0.0f) {
            const float skyFactor = max(0.0f, worldNormal.z);
            baseRgb += frameData.skyColor.rgb * frameData.skyIntensity * skyFactor * ao;
        }
        baseRgb = ACESFilm(baseRgb * frameData.exposure);
    } else {
        baseRgb = clamp(baseRgb, 0.0f, 1.0f);
    }

    overflowScratch[entryBase + 1u] = packColor(float4(baseRgb, albedo.a));
}
