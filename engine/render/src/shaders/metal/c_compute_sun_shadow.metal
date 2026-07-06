#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
// FrameDataSun, the cascade PCF sampler, and the world-space
// worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's detached
// world-receive path (#1576 P4b-2).
#include "ir_sun_shadow_sample.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute with cascaded shadow maps.

constant int kEmptyDistanceEncoded = 65535;

// Round-to-cell staircase band + near self-step rejection (#2010) — in lockstep
// with the GLSL twin (c_compute_sun_shadow.glsl). See the GLSL twin for the full
// rationale.
constant float kSelfStepMinHeight = 0.5;
constant float kSelfStepMaxHeight = 1.5;
constant float kSelfStepDepthRange = 3.0;

// #2256: on the per-axis path this stage is dispatched indirectly over only each
// axis's OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass). compactedCells
// holds the occupied linear cell indices; cellDrawArgs carries visibleCount at
// [kDispatchArgsBaseUint + 3]. Unused on the single-canvas 2D path (byte-identical).
constant uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

// Is this single-canvas receiver on a round-to-cell staircase — the case where
// its near sun-shadow blocker is its OWN in-cell step (venetian banding) rather
// than a separate caster? Detected geometrically (#2010, the marker-free intent
// of #1718/#2089): a tilted-flat surface quantized into a voxel staircase has a
// SAME-face in-plane neighbour offset ~1 cell along the receiver normal (the
// round-to-cell step), whereas a flat cardinal face is coplanar (offset ~0) and
// a genuine concave crease is a DIFFERENT face. So a same-face neighbour at
// [kSelfStepMinHeight, kSelfStepMaxHeight] along the outward normal is the
// signature; 8 in-plane directions catch axis- and diagonal-aligned steps.
inline bool detectSelfStepStaircase(
    int2 pixel, int2 size, int slot, int rawDepth, int cardinalIndex,
    float3 centerPos3D,
    constant FrameDataVoxelToTrixel &frameData,
    texture2d<int, access::read> trixelDistances
) {
    int faceId = frameData.visibleFaceIds[slot];
    float3 worldOutward = float3(faceOutwardNormal6I(faceId));
    int3 t1;
    int3 t2;
    if (faceId == kFaceZNeg || faceId == kFaceZPos) {
        t1 = int3(1, 0, 0);
        t2 = int3(0, 1, 0);
    } else if (faceId == kFaceXNeg || faceId == kFaceXPos) {
        t1 = int3(0, 1, 0);
        t2 = int3(0, 0, 1);
    } else {
        t1 = int3(1, 0, 0);
        t2 = int3(0, 0, 1);
    }
    int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    int3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
    int3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
    int2 deltaT1 = pos3DtoPos2DIso(t1View) * scale;
    int2 deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    int2 dirs[8] = {
        deltaT1, -deltaT1, deltaT2, -deltaT2,
        deltaT1 + deltaT2, deltaT1 - deltaT2, -deltaT1 + deltaT2, -deltaT1 - deltaT2
    };

    for (int dir = 0; dir < 8; ++dir) {
        int2 samplePixel = pixel + dirs[dir];
        if (samplePixel.x < 0 || samplePixel.x >= size.x ||
            samplePixel.y < 0 || samplePixel.y >= size.y) continue;
        int neighbourEncoded = trixelDistances.read(uint2(samplePixel)).x;
        if (neighbourEncoded >= kEmptyDistanceEncoded) continue;
        if ((neighbourEncoded & 3) != slot) continue;   // SAME-face only
        float3 neighbourPos3D = trixelCanvasPixelToWorld3D(
            samplePixel, neighbourEncoded >> 2, frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions, cardinalIndex
        );
        float step = abs(dot(neighbourPos3D - centerPos3D, worldOutward));
        if (step > kSelfStepMinHeight && step < kSelfStepMaxHeight) return true;
    }
    return false;
}

kernel void c_compute_sun_shadow(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    // Per-axis path (#2256): decode the receiver pixel from this axis's compacted
    // occupied-cell list under a 1-D indirect dispatch; the single-canvas 2D path
    // keeps its full-grid xy invocation guard (byte-identical). Mirrors GLSL.
    int2 pixel;
    if (frameData.perAxisRoute != 0) {
        // #2256: 2-D-folded indirect dispatch — recover the flat group index
        // (matches c_per_axis_cell_finalize's capped grid + c_voxel_visibility_compact).
        const uint groupIndex = groupId.x + groupId.y * numGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + localIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = int2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = int2(globalId.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.shadowsEnabled == 0) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (frameData.perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int face = encoded & 3;
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally — recover world-pos via isoPixelToPos3D and read the
    // world-frame outward normal directly. The single canvas keeps its
    // cardinal-snap reconstruction + R_z(-rasterYaw) normal rotation. Mirrors GLSL.
    bool perAxis = frameData.perAxisRoute != 0;
    float3 pos3D;
    float3 normal;
    if (perAxis) {
        int faceId = frameData.visibleFaceIds[face];
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, faceId, size,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions
        );
        normal = faceOutwardNormal6(faceId);
    } else if (frameData.residualYaw != 0.0) {
        // Smooth-yaw receive (#1719). While rotating, voxels leave the single
        // canvas (per-axis scatter) and its remaining SDF/text content is
        // stored at the FULL visualYaw with view-frame depth (#1345/#1370) —
        // recover with the matching smooth inverse. The cardinal recovery
        // returns a residual-rotated world pos here, so receivers sampled the
        // sun map off the true surface (frozen / vanishing floor shadows).
        // residualYaw == 0 keeps the byte-identical cardinal path. Mirrors GLSL.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.visualYaw
        );
        normal = rotateYawZInv(faceOutwardNormal(face), frameData.visualYaw);
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
        // Rotate raster-frame face normal to world frame so normal bias and slope
        // bias are applied in the correct world-space direction at non-zero camera
        // yaw. No-op at yaw=0 (cardinalIndex=0). Matches the AO shader pattern.
        normal = rotateCardinalZInv(faceOutwardNormal(face), cardinalIndex);
    }

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.metal, #1576).
    float factor = worldSunShadowFactor(pos3D, normal, float(rawDepth), sunFrameData, sunDepthBuf);
    // Round-to-cell staircase self-step suppression (#2010). Only a SHADOWED
    // receiver (factor < 1.0) on a detected staircase needs the carve, so the
    // neighbour probe runs only then — lit pixels and flats stay byte-identical.
    // Scoped to the static-camera GRID single-canvas raster (residualYaw == 0).
    // The recompute reuses the same pos/normal with the near self-step rejection
    // lifted, so genuine FAR contact shadows survive. Mirrors GLSL.
    if (!perAxis && frameData.residualYaw == 0.0 && factor < 1.0 &&
        detectSelfStepStaircase(pixel, size, face, rawDepth, cardinalIndex, pos3D, frameData, trixelDistances)) {
        factor = worldSunShadowFactor(
            pos3D, normal, float(rawDepth), sunFrameData, sunDepthBuf, kSelfStepDepthRange
        );
    }
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
