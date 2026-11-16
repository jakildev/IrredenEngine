#include "ir_iso_common.metal"        // decode*, faceOutwardNormal6, unpack/packColor,
                                     // FrameDataVoxelToTrixel (with overflowScratchLayout)
#include "ir_sun_face_query_layout.metal"
#include "ir_sun_shadow_sample.metal" // FrameDataSun + worldSunShadowFactor()
#include "ir_per_axis_shadow.metal"
#include "ir_world_lighting.metal"
#include "ir_surface_light_volume.metal"
#include "ir_surface_material.metal"

// Mirrors shaders/c_light_overflow_faces.glsl. Dispatched inside
// LIGHTING_TO_TRIXEL after the per-axis CELL lighting, and ONLY while rotating
// — the cardinal path never dispatches this kernel. The relight is the same
// world sample as c_lighting_to_trixel plus bounded cross-axis contact AO, and
// it rewrites the entry's colorPacked in place. The surface route sets
// perAxisStoreFrame.w bit 0 to request AO-carrier-only output;
// perAxisRoute bit 1 is the diagnostic cross-axis disable.

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
    texture2d<int, access::read> perAxisDistancesX [[texture(10)]],
    texture2d<int, access::read> perAxisDistancesY [[texture(11)]],
    texture2d<int, access::read> perAxisDistancesZ [[texture(12)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    // The dispatch is a 2-D group grid (the GPU finalizer wraps past
    // 1024 groups), so flatten the group coordinates to the entry index.
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint gid = workGroupIndex * 64u + localId.x;
    const int4 layout = voxelFrameData.overflowScratchLayout;
    // Live entry count (ctrl instanceCount at ctrlBase + 1); dispatch grid is
    // rounded to whole workgroups, so threads past the count early-return.
    const uint entryCount = overflowScratch[uint(layout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }
    // Only the shadow and normals diagnostics recolor overflow faces.
    if (frameData.lightingEnabled == 0 || (frameData.debugOverlayMode != 0 && frameData.debugOverlayMode != 1 && frameData.debugOverlayMode != 3 && frameData.debugOverlayMode != 8)) {
        return;
    }

    const uint entryBase = uint(layout.z) + gid * kFaceSortRecordWords;
    const uint packedCell = overflowEntryCell(overflowScratch[entryBase + 0u]);
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int  rawDist = int(overflowScratch[entryBase + 2u]);
    const uint voxelAO = overflowScratch[entryBase + 3u];

    const float4 albedo = unpackColor(colorPacked);

    // Recover cardinal store cell → world FaceId + world pos, bit-for-bit the
    // same decode/recovery the scatter's overflow branch + per-axis cell lighting
    // use. Sub-cell recovery, not lattice-only — the sun/volume samples must
    // land on the drawn surface. Mirrors GLSL.
    const int2 cell = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipPerAxis(rawDist);
    const int faceId = voxelFrameData.visibleFaceIds[slot] ^ flip;
    const float3 worldNormal = faceOutwardNormal6(faceId);
    if (frameData.debugOverlayMode == 8) {
        overflowScratch[entryBase + 1u] = packColor(float4(encodeUnitVectorUnorm8(worldNormal), albedo.a));
        return;
    }
    const float3 pos3D = perAxisCellToWorld3DSubCell(
        cell, rawDist, faceId, voxelFrameData.perAxisStoreFrame
    );

    float3 tangentU;
    float3 tangentV;
    faceInPlaneUnitAxes(faceId >> 1, tangentU, tangentV);
    const int3 tangents[4] = {
        int3(tangentU), -int3(tangentU), int3(tangentV), -int3(tangentV)
    };
    float occlusion = 0.0f;
    const float3 receiverCenter = perAxisFaceCenter(pos3D, faceId);
    const int2 storeSize = int2(
        int(perAxisDistancesX.get_width()), int(perAxisDistancesX.get_height())
    );
    for (int contact = 0;
         contact < ((voxelFrameData.perAxisRoute & 2) == 0 ? 4 : 0);
         ++contact) {
        const int3 tangent = tangents[contact];
        const int candidateFaceId = perAxisContactFaceId(tangent);
        const float3 candidateOrigin =
            perAxisContactCandidateOrigin(pos3D, faceId, tangent);
        const int2 candidateCell = perAxisFaceOriginToStoreCell(
            candidateOrigin, voxelFrameData.perAxisStoreFrame
        );
        if (!isInsideCanvas(candidateCell, storeSize)) continue;
        int candidateEncoded;
        const int candidateAxis = candidateFaceId >> 1;
        if (candidateAxis == 0)
            candidateEncoded = perAxisDistancesX.read(uint2(candidateCell)).x;
        else if (candidateAxis == 1)
            candidateEncoded = perAxisDistancesY.read(uint2(candidateCell)).x;
        else
            candidateEncoded = perAxisDistancesZ.read(uint2(candidateCell)).x;
        if (candidateEncoded == 0x7FFFFFFF) continue;
        const int recoveredFaceId = voxelFrameData.visibleFaceIds[decodeSlot(candidateEncoded)] ^
            decodeFlipPerAxis(candidateEncoded);
        if (recoveredFaceId != candidateFaceId) continue;
        const float3 recoveredOrigin = perAxisCellToWorld3DSubCell(
            candidateCell, candidateEncoded, recoveredFaceId, voxelFrameData.perAxisStoreFrame
        );
        const float3 originError = recoveredOrigin - candidateOrigin;
        if (dot(originError, originError) > 0.004f) continue;
        const float3 separation = perAxisFaceCenter(recoveredOrigin, recoveredFaceId) -
                                  receiverCenter;
        const float distanceSquared = dot(separation, separation);
        if (distanceSquared <= 1.0e-6f || distanceSquared >= 4.0f) continue;
        const float facing = max(
            dot(separation, float3(faceOutwardNormal6I(faceId))), 0.0f
        ) * max(dot(-separation, float3(faceOutwardNormal6I(recoveredFaceId))), 0.0f);
        if (facing <= 0.0f) continue;
        const float rangeWeight = 1.0f - distanceSquared / 4.0f;
        occlusion += facing / distanceSquared * rangeWeight * rangeWeight;
    }
    const float ao = 1.0f - occlusion * 0.25f;
    overflowScratch[entryBase + 3u] = packOverflowVoxelAO(overflowVoxelIndex(voxelAO), ao);
    if (frameData.debugOverlayMode == 1) {
        overflowScratch[entryBase + 1u] = packColor(float4(float3(ao), albedo.a));
        return;
    }
    if ((voxelFrameData.perAxisStoreFrame.w & 1) != 0) {
        return;
    }
    const float shadow = sunFrameData.shadowsEnabled != 0
        ? perAxisSunShadowFactor(pos3D, faceId, sunFrameData, sunDepthBuf)
        : 1.0f;
    if (frameData.debugOverlayMode == 3) {
        overflowScratch[entryBase + 1u] = packColor(float4(surfaceShadowDebugColor(shadow), albedo.a));
        return;
    }
    const float lambert = max(0.0f, dot(worldNormal, sunFrameData.sunDirection.xyz));
    const float faceFactor =
        surfaceSunFactor(sunFrameData.sunAmbient, sunFrameData.sunIntensity, lambert, shadow);

    const float3 materialRgb = surfaceMaterialColor(albedo.rgb, ao, frameData.lutEnabled != 0, paletteLUT);
    float3 baseRgb = materialRgb * faceFactor;

    if (frameData.lightVolumeEnabled != 0) {
        constexpr sampler volumeSampler(filter::linear, address::clamp_to_edge);
        const float3 light = surfaceLightVolume(pos3D, lightVolumeParams.worldOriginVoxel,
            lightVolume, lightVolumeId, volumeSampler, lights);
        baseRgb = baseRgb + albedo.rgb * light;
    }

    if (frameData.hdrEnabled != 0) {
        if (frameData.skyIntensity > 0.0f) {
            baseRgb += surfaceSkyLight(worldNormal, frameData.skyColor.rgb, frameData.skyIntensity, ao);
        }
    }
    baseRgb = surfaceDisplayColor(baseRgb, frameData.exposure, frameData.hdrEnabled != 0);

    overflowScratch[entryBase + 1u] = packColor(float4(baseRgb, albedo.a));
}
