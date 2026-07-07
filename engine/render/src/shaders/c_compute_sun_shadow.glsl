#version 450 core

// Per-pixel directional sun shadow compute with cascaded shadow maps.
// For each rasterized surface pixel, reconstructs the voxel-space position,
// selects the appropriate cascade based on iso depth, projects into the
// cascade's sun-aligned depth map, and compares against the nearest stored
// blocker. Blends between cascades at the split boundary.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), the cascade PCF sampler, and the
// world-space worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's
// detached world-receive path (#1576 P4b-2).
#include "ir_sun_shadow_sample.glsl"

const int kEmptyDistanceEncoded = 65535;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = a
    // per-axis canvas bake (#1311), reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path (#1278/#1311).
    uniform ivec4 visibleFaceIds;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;

// #2256: on the per-axis path this stage is dispatched indirectly over only each
// axis's OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass) instead of
// sweeping the full grid. compactedCells holds the occupied linear cell indices;
// cellDrawArgs carries the visibleCount at [kDispatchArgsBaseUint + 3]. Unused on
// the single-canvas 2D path (perAxisRoute == 0), which stays byte-identical.
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

// Round-to-cell staircase same-face step band (#2010). A tilted-flat surface
// quantized into a voxel staircase has a SAME-face in-plane neighbour offset
// ~1 cell along the receiver's outward normal (the round-to-cell step). A flat
// cardinal face is coplanar (offset ~0, below kSelfStepMinHeight) and a genuine
// concave crease meets a DIFFERENT face — so a same-face neighbour whose decoded
// pos3D sits in [kSelfStepMinHeight, kSelfStepMaxHeight] along the normal is the
// unambiguous staircase signal. Capped at kSelfStepMaxHeight so a >1-cell depth
// discontinuity (two separate same-face surfaces, an occlusion boundary) is not
// mistaken for a step.
const float kSelfStepMinHeight = 0.5;
const float kSelfStepMaxHeight = 1.5;
// Near sun-Z rejection applied on a detected staircase receiver (#2010). The
// in-cell step projects to a small sun-Z offset; kept generous (the detection
// already restricts the carve to staircase pixels) so the self-step blocker is
// reliably skipped, while the kMaxShadowDepthRange window still rejects genuine
// far casters. Sized against the --debug-overlay shadow magenta residual over
// the GRID spin cubes.
const float kSelfStepDepthRange = 3.0;

// Is this single-canvas receiver on a round-to-cell staircase — the case where
// its near sun-shadow blocker is its OWN in-cell step (venetian banding) rather
// than a separate caster? Detected geometrically (#2010, the marker-free intent
// of #1718/#2089): probe 8 in-plane neighbours (axis + diagonal) and report a
// staircase if any SAME-face neighbour is ~1 cell offset along the receiver
// normal. The verbatim #2089 different-face/beyond resample is tuned for the AO
// receiver (a tread whose occluder is a riser ~1 cell in front); the sun-shadow
// receiver is the riser/tread of the staircase itself, for which the same-face
// round-to-cell step is the reliable signal. Cardinal flats (offset ~0) never
// match, so a static cardinal scene is byte-identical.
bool detectSelfStepStaircase(ivec2 pixel, ivec2 size, int slot, int rawDepth, int cardinalIndex, vec3 centerPos3D) {
    int faceId = visibleFaceIds[slot];
    vec3 worldOutward = vec3(faceOutwardNormal6I(faceId));
    // In-plane tangent pair for the receiver's face axis.
    ivec3 t1, t2;
    if (faceId == kFaceZNeg || faceId == kFaceZPos) {
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 1, 0);
    } else if (faceId == kFaceXNeg || faceId == kFaceXPos) {
        t1 = ivec3(0, 1, 0);
        t2 = ivec3(0, 0, 1);
    } else {
        t1 = ivec3(1, 0, 0);
        t2 = ivec3(0, 0, 1);
    }
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    ivec3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
    ivec3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
    ivec2 deltaT1 = pos3DtoPos2DIso(t1View) * scale;
    ivec2 deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    ivec2 dirs[8] = ivec2[8](
        deltaT1, -deltaT1, deltaT2, -deltaT2,
        deltaT1 + deltaT2, deltaT1 - deltaT2, -deltaT1 + deltaT2, -deltaT1 - deltaT2
    );

    for (int dir = 0; dir < 8; ++dir) {
        ivec2 samplePixel = pixel + dirs[dir];
        if (samplePixel.x < 0 || samplePixel.x >= size.x ||
            samplePixel.y < 0 || samplePixel.y >= size.y) continue;
        int neighbourEncoded = imageLoad(trixelDistances, samplePixel).x;
        if (neighbourEncoded >= kEmptyDistanceEncoded) continue;
        if ((neighbourEncoded & 3) != slot) continue;   // SAME-face only
        vec3 neighbourPos3D = trixelCanvasPixelToWorld3D(
            samplePixel, neighbourEncoded >> 2, trixelCanvasOffsetZ1,
            frameCanvasOffset, voxelRenderOptions, cardinalIndex
        );
        float step = abs(dot(neighbourPos3D - centerPos3D, worldOutward));
        if (step > kSelfStepMinHeight && step < kSelfStepMaxHeight) return true;
    }
    return false;
}

void main() {
    // Per-axis path (#2256): decode the receiver pixel from this axis's compacted
    // occupied-cell list under a 1-D indirect dispatch; the single-canvas 2D path
    // keeps its full-grid xy invocation guard (byte-identical).
    const ivec2 size = imageSize(trixelDistances);
    ivec2 pixel;
    if (perAxisRoute != 0) {
        // #2256: 2-D-folded indirect dispatch — recover the flat group index
        // (matches c_per_axis_cell_finalize's capped grid + c_voxel_visibility_compact).
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
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (shadowsEnabled == 0) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int face = encoded & 3;
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally, so recover world-pos via isoPixelToPos3D and read the
    // world-frame outward normal directly (no cardinal rotation — the store
    // already wrote world coords). The single canvas keeps its cardinal-snap
    // reconstruction + R_z(-rasterYaw) normal rotation, byte-identical at the
    // cardinal fast path (per-axis canvases are only allocated while rotating).
    bool perAxis = perAxisRoute != 0;
    vec3 pos3D;
    vec3 normal;
    if (perAxis) {
        int faceId = visibleFaceIds[face];
        pos3D = perAxisCellToWorld3D(pixel, rawDepth, faceId, size, frameCanvasOffset, voxelRenderOptions);
        normal = faceOutwardNormal6(faceId);
    } else if (residualYaw != 0.0) {
        // Smooth-yaw receive (#1719). While rotating, voxels leave the single
        // canvas (per-axis scatter) and its remaining SDF/text content is
        // stored at the FULL visualYaw with view-frame depth (#1345/#1370) —
        // recover with the matching smooth inverse. The cardinal recovery
        // returns a residual-rotated world pos here, so receivers sampled the
        // sun map off the true surface: the floor shadow froze against the
        // screen, mis-scaled against the deforming floor, and slid off the
        // caster footprint entirely as |residual| grew. residualYaw == 0
        // keeps the byte-identical cardinal path below.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
        normal = rotateYawZInv(faceOutwardNormal(face), visualYaw);
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        // Rotate raster-frame face normal to world frame so normal bias and slope
        // bias are applied in the correct world-space direction at non-zero camera
        // yaw. No-op at yaw=0 (cardinalIndex=0). Matches the AO shader pattern.
        normal = rotateCardinalZInv(faceOutwardNormal(face), cardinalIndex);
    }

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.glsl, #1576).
    float factor = worldSunShadowFactor(pos3D, normal, float(rawDepth));
    // Round-to-cell staircase self-step suppression (#2010). A rebuilt-rotating
    // GRID solid self-shadows its own quantized staircase risers as venetian
    // banding. Only a SHADOWED receiver (factor < 1.0) on a detected staircase
    // step needs the carve, so the neighbour probe (mirroring the AO twin) runs
    // only then — lit pixels and flat surfaces skip it and stay byte-identical.
    // residualYaw == 0 scopes it to the static-camera GRID single-canvas raster
    // (a rotating camera scatters the solid onto per-axis canvases handled
    // upstream); the recompute reuses the same pos/normal with the near self-step
    // rejection lifted, so genuine far contact shadows are untouched.
    if (!perAxis && residualYaw == 0.0 && factor < 1.0 &&
        detectSelfStepStaircase(pixel, size, face, rawDepth, cardinalIndex, pos3D)) {
        factor = worldSunShadowFactor(pos3D, normal, float(rawDepth), kSelfStepDepthRange);
    }
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}
