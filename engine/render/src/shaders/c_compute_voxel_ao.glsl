#version 450 core

// Four face-tangent depth samples approximate local ambient visibility.
// Contributions require mutually facing surfaces and decay with separation.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"

// Same threshold LIGHTING_TO_TRIXEL uses for "empty pixel" — encoded
// distances >= 65535 mean the clear value was never overwritten.
const int kEmptyDistanceEncoded = 65535;

const float kAORadiusSquared = 4.0;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster; nonzero
    // = lighting a per-axis canvas, so reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5), the table the stage-1 raster encodes slots
    // against. AO maps the decoded depth slot → world FaceId via this lookup so
    // the outward-normal step uses the rotation-aware six-face normal.
    uniform ivec4 visibleFaceIds;
};

// Sun lighting state. Only `aoEnabled` is read by this shader; the block
// is kept in lockstep with `FrameDataSun` in ir_render_types.hpp so the
// shared UBO at binding 29 matches std140 layout for every consumer
// (BAKE_SUN_SHADOW_MAP owns the upload each frame).
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
    uniform float sunSplatMaxTexels;  // unused here (sun-map bake only)
    uniform float sunMaxShadowThrow;  // unused here (receiver-only)
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasAO;

// Per-axis compacted occupied-cell list + per-axis indirect-args region.
// On the per-axis path the dispatch runs over the compacted cells and each
// cell's canvas pixel is recovered from its linear index. Bound per axis via
// bindRange (offsets into the three axis regions).
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

void main() {
    const ivec2 size = imageSize(trixelDistances);
    ivec2 pixel;
    if (perAxisRoute != 0) {
        // Indirect dispatch over the compacted occupied-cell list, folded into
        // a capped 2-D workgroup grid by c_per_axis_cell_finalize; recover the
        // flat group index the same way c_voxel_visibility_compact does.
        const uint groupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + gl_LocalInvocationIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = ivec2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = ivec2(gl_GlobalInvocationID.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    int encoded = imageLoad(trixelDistances, pixel).x;
    // Per-axis canvas uses INT_MAX as empty sentinel; single-canvas uses 65535.
    const int kEmpty = (perAxisRoute != 0) ? 0x7FFFFFFF : kEmptyDistanceEncoded;
    if (encoded >= kEmpty) {
        imageStore(canvasAO, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (aoEnabled == 0) {
        imageStore(canvasAO, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    // The rasterizer writes the visible-triplet slot (0/1/2). Slot → world
    // FaceId via `visibleFaceIds[slot]` — single source of face metadata
    // shared with the raster, so AO's "step out of the surface" arithmetic
    // uses the actually-visible face's outward normal and tangents at every
    // cardinal.
    int slot = decodeSlot(encoded);
    // The riser-polarity flip selects the OPPOSITE same-axis face, so the
    // outward-normal step walks out of the true surface instead of into the
    // solid. The tangent pair is polarity-invariant (both branches cover NEG
    // and POS of each axis).
    int flip = decodeFlipRoute(encoded, perAxisRoute);
    int faceId = visibleFaceIds[slot] ^ flip;
    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, and the flip carrier).
    int rawDepth = decodeDepthRoute(encoded, perAxisRoute);
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    // A per-axis canvas stores the world frame face-locally (perAxisRoute != 0),
    // so recover world-pos via isoPixelToPos3D; the single canvas stores the
    // cardinal-snapped iso pixel, recovered via trixelCanvasPixelToWorld3D.
    bool perAxis = perAxisRoute != 0;
    vec3 pos3D = perAxis
        ? perAxisCellToWorld3DSubCell(pixel, encoded, faceId, size, frameCanvasOffset, voxelRenderOptions)
        : trixelCanvasPixelToWorld3D(
              pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, cardinalIndex
          );

    // World-frame outward normal + in-plane tangents for the camera-visible
    // face this pixel rendered. The tangent step is rotated through
    // R_z(-rasterYaw) before iso projection so the neighbour-sample
    // direction lands on the canvas pixel that actually holds the
    // +tangent neighbour at this cardinal.
    vec3 worldOutward = vec3(faceOutwardNormal6I(faceId));
    ivec3 t1, t2;
    // The tangent sign doesn't matter (AO samples ±t1, ±t2), so both
    // polarities of a face axis share one pair.
    if (faceId == kFaceZNeg || faceId == kFaceZPos) {
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 1, 0);
    } else if (faceId == kFaceXNeg || faceId == kFaceXPos) {
        t1 = ivec3(0, 1, 0);
        t2 = ivec3(0, 0, 1);
    } else {
        // Y_NEG or Y_POS
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 0, 1);
    }

    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    ivec2 deltaT1;
    ivec2 deltaT2;
    if (perAxis) {
        // Per-axis canvas is BASE-RESOLUTION: 1 cell = 1 world voxel.
        // A +/-1 cell step along each canvas axis is the +/-1 in-plane neighbour.
        deltaT1 = ivec2(1, 0);
        deltaT2 = ivec2(0, 1);
    } else {
        ivec3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
        ivec3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
        deltaT1 = pos3DtoPos2DIso(t1View) * scale;
        deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    }

    float occlusion = 0.0;
    for (int dir = 0; dir < 4; ++dir) {
        ivec2 delta;
        if (dir == 0) delta = deltaT1;
        else if (dir == 1) delta = -deltaT1;
        else if (dir == 2) delta = deltaT2;
        else delta = -deltaT2;
        ivec2 samplePixel = pixel + delta;
        if (samplePixel.x < 0 || samplePixel.x >= size.x ||
            samplePixel.y < 0 || samplePixel.y >= size.y) continue;

        int neighbourEncoded = imageLoad(trixelDistances, samplePixel).x;
        if (neighbourEncoded >= kEmpty) continue;

        int neighbourFaceId = visibleFaceIds[decodeSlot(neighbourEncoded)] ^
            decodeFlipRoute(neighbourEncoded, perAxisRoute);
        if (neighbourFaceId == faceId) continue;

        int neighbourRawDepth = decodeDepthRoute(neighbourEncoded, perAxisRoute);
        vec3 neighbourPos3D;
        if (perAxis) {
            neighbourPos3D = perAxisCellToWorld3DSubCell(
                samplePixel, neighbourEncoded, neighbourFaceId, size,
                frameCanvasOffset, voxelRenderOptions
            );
        } else {
            neighbourPos3D = trixelCanvasPixelToWorld3D(
                samplePixel, neighbourRawDepth, trixelCanvasOffsetZ1,
                frameCanvasOffset, voxelRenderOptions, cardinalIndex
            );
        }

        vec3 separation = neighbourPos3D - pos3D;
        float distanceSquared = dot(separation, separation);
        if (distanceSquared <= 1.0e-6 || distanceSquared >= kAORadiusSquared) continue;

        float receiverFacing = max(dot(separation, worldOutward), 0.0);
        float occluderFacing = max(
            dot(-separation, vec3(faceOutwardNormal6I(neighbourFaceId))), 0.0
        );
        float rangeWeight = 1.0 - distanceSquared / kAORadiusSquared;
        occlusion += receiverFacing * occluderFacing / distanceSquared *
            rangeWeight * rangeWeight;
    }

    float ao = 1.0 - occlusion * 0.25;
    imageStore(canvasAO, pixel, vec4(ao, 0.0, 0.0, 0.0));
}
