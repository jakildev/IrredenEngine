#version 450 core

// World-placed detached re-voxelize sun-shadow resolve, scatter pass.
//
// Re-projects ONE opt-in world-placed detached re-voxelize canvas (model-frame
// R32I distance texture) into a SCREEN-SPACE front-most iso-depth scratch
// buffer laid out exactly like the main canvas distance texture. Dispatched
// once per opt-in caster; atomicMin across the dispatches resolves the
// front-most surface per screen pixel. BAKE_SUN_SHADOW_MAP then bakes the
// blitted resolve texture through its cardinal recovery
// (trixelCanvasPixelToWorld3D).
// Invariant (docs/design/detached-revoxelize-world-light.md): the sun-shadow
// bake only ever reads main-canvas-layout depth sources; a foreign model-frame
// canvas texture is never a bake input (the direct read returns empty through
// Metal's image-atomic scratch indirection).
//
// The scratch target is an SSBO (not an image) because Metal has no portable
// image-atomic syntax.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

const int kEmptyDistanceEncoded = 65535;

// Destination layout only; private-canvas recovery uses DetachedShadowFrame.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;   // MAIN canvas origin (unused; bases size-derived)
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;       // MAIN canvas size
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;              // camera cardinal raster yaw (main projection)
    uniform float residualYaw;
    uniform float _yawPadding;
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    uniform ivec4 visibleFaceIds;
    uniform vec4 _voxelDepthAxisPadding;  // voxelDepthAxis_ in the full UBO
    // Unused tail retained for the shared frame's layout.
    uniform vec4 detachedWorldReceive;
};

// Input: ONE opt-in detached re-voxelize canvas (model-frame cardinal store, R32I).
layout(r32i, binding = 0) readonly uniform iimage2D detachedDistances;

layout(std140, binding = 16) uniform DetachedShadowFrame {
    vec4 worldOriginAndDensity;
    vec4 viewToWorld;
};

// Output scratch: main-canvas-sized, front-most iso-depth via atomicMin.
// Aliases kBufferIndex_SunShadowDepthMap (slot 28) — the cast block rebinds
// slot 28 to the sun depth map before its bake dispatch.
layout(std430, binding = 28) restrict buffer PerAxisResolveScratch {
    int resolveScratch[];
};

// The cardinal-layout micro-cell emit shared with
// c_resolve_per_axis_screen_depth. Included AFTER ir_iso_common.glsl and AFTER
// the resolveScratch declaration it writes through (the fragment's wrapper
// contract).
#include "ir_resolve_cardinal_emit.glsl"

void main() {
    const ivec2 cell = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 detachedSize = imageSize(detachedDistances);
    if (cell.x >= detachedSize.x || cell.y >= detachedSize.y) {
        return;
    }

    const int rawDist = imageLoad(detachedDistances, cell).x;
    if (rawDist >= kEmptyDistanceEncoded) {
        return; // empty detached cell
    }
    // Single-canvas encoding: the flip is re-emitted into the re-projected
    // encode so polarity survives the resolve bridge.
    const int rawDepth = decodeDepthSingle(rawDist);
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipSingle(rawDist);

    const ivec2 sourceOptions = ivec2(voxelRenderOptions.x, int(worldOriginAndDensity.w));
    const vec3 viewLocalPos = trixelCanvasPixelToWorld3D(
        cell, rawDepth, trixelOriginOffsetZ1(detachedSize), vec2(0.0), sourceOptions, 0
    );
    const vec3 worldPoint = rotateByQuat(viewLocalPos, viewToWorld) + worldOriginAndDensity.xyz;
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const ivec3 worldPos = roundHalfUp(worldPoint * float(scale));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1's cardinal store exactly (same output side as
    // the per-axis resolve scatter): rotate the world position into the
    // cardinal VIEW frame, key by un-yawed iso depth, and place at the un-yawed iso pixel. The
    // BAKE recovery (trixelCanvasPixelToWorld3D at this rasterYaw) is the
    // exact inverse.
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    ivec3 viewPos = worldPos;
    if (cardinalIndex != 0) {
        // Plain cardinal rotation with no lower-corner shift, mirroring the
        // stage-1 cardinal store and the BAKE recovery (trixelCanvasPixelToWorld3D).
        viewPos = rotateCardinalZ(worldPos, cardinalIndex);
    }

    const ivec2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSizePixels), frameCanvasOffset, voxelRenderOptions
    );

    const vec3 worldNormal = rotateByQuat(faceOutwardNormal6(slot << 1), viewToWorld);
    const vec3 viewNormal = abs(rotateCardinalZInv(worldNormal, (4 - cardinalIndex) & 3));
    const int viewAxis = viewNormal.x >= viewNormal.y && viewNormal.x >= viewNormal.z
        ? kXFace : (viewNormal.y >= viewNormal.z ? kYFace : kZFace);
    emitResolveCardinalDiamond(
        viewPos, viewAxis, slot, flip, mainBase, canvasSizePixels
    );
}
