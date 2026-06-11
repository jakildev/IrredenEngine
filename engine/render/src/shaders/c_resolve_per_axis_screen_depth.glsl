#version 450 core

// Smooth camera Z-yaw — per-axis sun-shadow resolve, scatter pass (#1435).
//
// Re-projects one face-local per-axis voxel canvas into a SCREEN-SPACE
// front-most iso-depth scratch buffer laid out exactly like the main canvas
// distance texture (cardinal-snapped iso pixel, value = pos3DtoDistance<<2 |
// slot). Dispatched once per axis canvas; imageAtomicMin across the three
// resolves the front-most surface per screen pixel — the same per-screen-pixel
// flattening the main (SDF/text) canvas has, which the raw face-local store
// lacks. BAKE_SUN_SHADOW_MAP then reads the blitted texture through its
// EXISTING cardinal recovery (trixelCanvasPixelToWorld3D), so per-axis voxels
// cast sun shadows again without the cross-face self-occlusion that retired the
// face-local bake in #1380. See docs/design/per-axis-sun-shadow-resolve.md.
//
// The scratch target is an SSBO (not an image) because Metal has no portable
// image-atomic syntax — same reason c_voxel_to_trixel_stage_1.metal taps a
// distance scratch buffer. Cardinal byte-identity is structural: the per-axis
// canvases are only allocated at non-zero residual yaw, so this stage never
// runs at a cardinal.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Per-axis-only shader; canvas clears to INT_MAX per #1458 encoding.
const int kEmptyDistanceEncoded = 0x7FFFFFFF;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;       // MAIN canvas size (set by the resolve system)
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;
    uniform vec4 _faceDeformPadding[3];
    uniform ivec4 visibleFaceIds;
};

// Input: ONE per-axis voxel canvas (face-local in-plane store, R32I).
layout(r32i, binding = 0) readonly uniform iimage2D perAxisDistances;

// Output scratch: main-canvas-sized, front-most iso-depth via atomicMin.
// Aliases kBufferIndex_SunShadowDepthMap (slot 28) — the whole resolve stage
// runs strictly before BAKE rebinds slot 28 to the sun depth map.
layout(std430, binding = 28) restrict buffer PerAxisResolveScratch {
    int resolveScratch[];
};

void main() {
    const ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 perAxisSize = imageSize(perAxisDistances);
    if (cell.x >= perAxisSize.x || cell.y >= perAxisSize.y) {
        return;
    }

    const int rawDist = imageLoad(perAxisDistances, cell).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty per-axis cell
    }
    // Per-axis encoding (#1458): rawDepth in world units at bits [31:10].
    const int rawDepth = rawDist >> 10;
    const int slot = rawDist & 3;
    const int faceId = visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Recover the face-plane origin in canvas-native (subdivision) units — the
    // exact integer inverse perAxisCellToWorld3D / v_peraxis_scatter use (no
    // trig, no 2cos(yaw)+1 singularity). perAxisBase is reproduced from the
    // per-axis canvas size identically to the store (ir_per_axis_lighting.glsl).
    const ivec2 perAxisBase = trixelFrameOffset(
        trixelOriginOffsetZ1(perAxisSize), frameCanvasOffset, voxelRenderOptions
    );
    const ivec3 anchor = faceLocalAnchor(perAxisBase, perAxisSize);
    const ivec2 inPlane = cell - faceLocalBase(axis, anchor, perAxisSize);
    const ivec3 origin = faceOriginFromInPlane(faceId, inPlane, rawDepth);

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1's cardinal (perAxisRoute==0) store exactly:
    // rotate the world origin into the cardinal VIEW frame, add the same
    // lower-corner shift (scaled to subdivision units), key by un-yawed iso
    // depth, and place at the un-yawed iso pixel. The BAKE recovery
    // (trixelCanvasPixelToWorld3D with this rasterYaw) is the exact inverse, so
    // the recovered world-pos matches the per-axis RECEIVE (perAxisCellToWorld3D)
    // by construction — cast and receive agree.
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    // origin is in world units (#1458); scale up to subdivision units for the
    // main-canvas layout so BAKE's trixelCanvasPixelToWorld3D recovers correctly.
    ivec3 viewPos = origin;
    if (cardinalIndex != 0) {
        viewPos = rotateCardinalZ(origin, cardinalIndex);
        viewPos += cardinalLowerCornerShift(cardinalIndex);  // world units
    }
    viewPos *= scale;  // convert to subdivision units
    const int encoded = encodeDepthWithFace(pos3DtoDistance(viewPos), slot);

    const ivec2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSizePixels), frameCanvasOffset, voxelRenderOptions
    );
    const ivec2 mainPixel = mainBase + pos3DtoPos2DIso(viewPos);
    if (mainPixel.x < 0 || mainPixel.x >= canvasSizePixels.x ||
        mainPixel.y < 0 || mainPixel.y >= canvasSizePixels.y) {
        return;
    }

    // Front-most per screen pixel: smallest encoded distance wins (rawDepth
    // dominates the 2 slot bits), exactly like the main canvas atomicMin store.
    atomicMin(resolveScratch[mainPixel.y * canvasSizePixels.x + mainPixel.x], encoded);
}
