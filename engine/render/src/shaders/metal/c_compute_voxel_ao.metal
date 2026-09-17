#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"

// Mirrors shaders/c_compute_voxel_ao.glsl.

constant int kEmptyDistanceEncoded = 65535;

constant float kAORadiusSquared = 4.0;
// Must stay in lockstep with c_compute_voxel_ao.glsl.
constant float kAOMinDistanceSquared = 1.0e-6;
// A monotone staircase returns to the receiver's own face one cell beyond
// a different-face step, ~1 voxel further out along the receiver normal; a
// coplanar same-face blip (d ~ 0) is not a staircase and keeps its AO.
// Chosen empirically against measured staircase captures, not derived from
// voxel geometry; any value strictly between a coplanar return (d ~ 0) and
// the next tread (d ~ 1) works.
constant float kAOStaircaseStepHeight = 0.5;

constant uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

// Mirrors `FrameDataSun` from ir_render_types.hpp. Only `aoEnabled` is
// consumed here; the layout must match so the shared UBO at binding 29
// can be read by every consumer (BAKE_SUN_SHADOW_MAP owns the upload).
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
    float sunSplatMaxTexels;  // unused here (sun-map bake only)
    float sunMaxShadowThrow;  // unused here (receiver-only)
};

kernel void c_compute_voxel_ao(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasAO [[texture(1)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    int2 size = int2(int(trixelDistances.get_width()), int(trixelDistances.get_height()));
    int2 pixel;
    if (frameData.perAxisRoute != 0) {
        // The compacted-cell dispatch is folded into a capped 2-D threadgroup
        // grid by c_per_axis_cell_finalize (groupsX capped, remainder in groupsY).
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
    // Per-axis canvas uses INT_MAX as empty sentinel; single-canvas uses 65535.
    const int kEmpty = (frameData.perAxisRoute != 0) ? 0x7FFFFFFF : kEmptyDistanceEncoded;
    if (encoded >= kEmpty) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.aoEnabled == 0) {
        canvasAO.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    // The rasterizer writes the visible-triplet slot (0/1/2); resolve the
    // world FaceId via `visibleFaceIds[slot]`. Single source of face metadata
    // shared with the raster.
    int slot = decodeSlot(encoded);
    // The riser-polarity flip selects the OPPOSITE same-axis face, so the
    // outward-normal step walks out of the true surface instead of into the
    // solid. The tangent pair is polarity-invariant.
    int flip = decodeFlipRoute(encoded, frameData.perAxisRoute);
    int faceId = frameData.visibleFaceIds[slot] ^ flip;
    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, and the flip carrier).
    int rawDepth = decodeDepthRoute(encoded, frameData.perAxisRoute);
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    // A per-axis canvas stores the world frame face-locally (perAxisRoute != 0),
    // recovered via isoPixelToPos3D; the single canvas uses the cardinal-snap
    // reconstruction.
    bool perAxis = frameData.perAxisRoute != 0;
    float3 pos3D = perAxis
        ? perAxisCellToWorld3DSubCell(
              pixel, encoded, faceId, size,
              frameData.frameCanvasOffset, frameData.voxelRenderOptions
          )
        : trixelCanvasPixelToWorld3D(
              pixel,
              rawDepth,
              frameData.trixelCanvasOffsetZ1,
              frameData.frameCanvasOffset,
              frameData.voxelRenderOptions,
              cardinalIndex
          );

    // World-frame outward normal + in-plane tangents for the camera-visible
    // face this pixel rendered. Tangents are rotated through R_z(-rasterYaw)
    // before iso projection so the neighbour-sample iso direction matches
    // where the rasterizer wrote the +tangent neighbour at this cardinal.
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
        // Y_NEG or Y_POS
        t1 = int3(1, 0, 0);
        t2 = int3(0, 0, 1);
    }

    int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    int2 deltaT1;
    int2 deltaT2;
    if (perAxis) {
        // Per-axis canvas is BASE-RESOLUTION: 1 cell = 1 world voxel.
        deltaT1 = int2(1, 0);
        deltaT2 = int2(0, 1);
    } else {
        int3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
        int3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
        deltaT1 = pos3DtoPos2DIso(t1View) * scale;
        deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    }

    float occlusion = 0.0;
    for (int dir = 0; dir < 4; ++dir) {
        int2 delta;
        if (dir == 0) delta = deltaT1;
        else if (dir == 1) delta = -deltaT1;
        else if (dir == 2) delta = deltaT2;
        else delta = -deltaT2;
        int2 samplePixel = pixel + delta;
        if (samplePixel.x < 0 || samplePixel.x >= size.x ||
            samplePixel.y < 0 || samplePixel.y >= size.y) continue;

        int neighbourEncoded = trixelDistances.read(uint2(samplePixel)).x;
        if (neighbourEncoded >= kEmpty) continue;

        int neighbourFaceId = frameData.visibleFaceIds[decodeSlot(neighbourEncoded)] ^
            decodeFlipRoute(neighbourEncoded, frameData.perAxisRoute);
        if (neighbourFaceId == faceId) continue;

        int neighbourRawDepth = decodeDepthRoute(neighbourEncoded, frameData.perAxisRoute);
        float3 neighbourPos3D;
        if (perAxis) {
            neighbourPos3D = perAxisCellToWorld3DSubCell(
                samplePixel, neighbourEncoded, neighbourFaceId, size,
                frameData.frameCanvasOffset, frameData.voxelRenderOptions
            );
        } else {
            neighbourPos3D = trixelCanvasPixelToWorld3D(
                samplePixel,
                neighbourRawDepth,
                frameData.trixelCanvasOffsetZ1,
                frameData.frameCanvasOffset,
                frameData.voxelRenderOptions,
                cardinalIndex
            );
        }

        float3 separation = neighbourPos3D - pos3D;
        float distanceSquared = dot(separation, separation);
        if (distanceSquared <= kAOMinDistanceSquared || distanceSquared >= kAORadiusSquared) continue;

        float receiverFacing = max(dot(separation, worldOutward), 0.0);
        float occluderFacing = max(
            dot(-separation, float3(faceOutwardNormal6I(neighbourFaceId))), 0.0
        );
        float facing = receiverFacing * occluderFacing;
        if (facing <= 0.0) continue;

        // Tilt-aware same-face resample (rationale in the GLSL mirror): a
        // monotone staircase returns to the receiver's own face one cell
        // beyond the step; a genuine crease does not. Single-canvas path only.
        if (!perAxis) {
            int2 beyondPixel = pixel + 2 * delta;
            if (beyondPixel.x >= 0 && beyondPixel.x < size.x &&
                beyondPixel.y >= 0 && beyondPixel.y < size.y) {
                int beyondEncoded = trixelDistances.read(uint2(beyondPixel)).x;
                if (beyondEncoded < kEmpty && decodeSlot(beyondEncoded) == slot &&
                    decodeFlipSingle(beyondEncoded) == flip) {
                    float3 beyondPos3D = trixelCanvasPixelToWorld3D(
                        beyondPixel,
                        decodeDepthSingle(beyondEncoded),
                        frameData.trixelCanvasOffsetZ1,
                        frameData.frameCanvasOffset,
                        frameData.voxelRenderOptions,
                        cardinalIndex
                    );
                    if (dot(beyondPos3D - pos3D, worldOutward) > kAOStaircaseStepHeight) continue;
                }
            }
        }

        float rangeWeight = 1.0 - distanceSquared / kAORadiusSquared;
        occlusion += facing / distanceSquared * rangeWeight * rangeWeight;
    }

    float ao = 1.0 - occlusion * 0.25;
    canvasAO.write(float4(ao, 0.0, 0.0, 0.0), uint2(pixel));
}
