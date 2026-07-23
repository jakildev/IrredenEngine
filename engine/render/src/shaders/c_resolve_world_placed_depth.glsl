#version 450 core

// World-placed detached re-voxelize sun-shadow resolve, scatter pass
// (#1576 P4b-3, Q2 mechanism a′).
//
// Re-projects ONE opt-in world-placed detached re-voxelize canvas (model-frame
// R32I distance texture) into a SCREEN-SPACE front-most iso-depth scratch
// buffer laid out exactly like the main canvas distance texture. Dispatched
// once per opt-in caster; atomicMin across the dispatches resolves the
// front-most surface per screen pixel. BAKE_SUN_SHADOW_MAP then bakes the
// blitted resolve texture through its EXISTING cardinal recovery
// (trixelCanvasPixelToWorld3D) — the faithful mirror of the per-axis
// resolve-bake precedent (c_resolve_per_axis_screen_depth.glsl / #1435).
// Invariant (docs/design/detached-revoxelize-world-light.md, Q2 REVISED):
// the sun-shadow bake only ever reads main-canvas-layout depth sources; a
// foreign model-frame canvas texture is never a bake input (the direct read
// returns empty through Metal's image-atomic scratch indirection).
//
// The scratch target is an SSBO (not an image) because Metal has no portable
// image-atomic syntax — same pattern as the per-axis resolve scatter.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

const int kEmptyDistanceEncoded = 65535;

// Authored with the MAIN canvas's frame; only `detachedWorldReceive` is
// patched per caster dispatch (.xyz = the caster's world cell origin,
// .w = 1.0). The caster's own canvas size comes from imageSize() — the
// re-voxelize canvas always rasters cardinal (rasterYaw == 0, camera pan
// gated off by isDetachedCanvas), so its store base is reproducible from
// its size + the shared frameCanvasOffset/voxelRenderOptions alone.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;   // MAIN canvas origin offset (unused; size-derived below)
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
    // .xyz = the caster entity's world cell origin (roundVec3HalfUp of its
    // world translation, world cells); .w = 1.0 on the caster dispatches.
    uniform vec4 detachedWorldReceive;
};

// Input: ONE opt-in detached re-voxelize canvas (model-frame cardinal store, R32I).
layout(r32i, binding = 0) readonly uniform iimage2D detachedDistances;

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
    // Single-canvas encoding (flip carrier #2207): the flip is re-emitted into
    // the re-projected encode below so polarity survives the resolve bridge.
    const int rawDepth = decodeDepthSingle(rawDist);
    const int slot = decodeSlot(rawDist);
    const int flip = decodeFlipSingle(rawDist);

    // Recover the pool-centered MODEL position in subdivision units. The
    // re-voxelize canvas rasters cardinal (rasterYaw == 0, perAxisRoute == 0),
    // so this is the integer-unit form of the trixelCanvasPixelToWorld3D
    // recovery the world-receive lighting path uses (c_lighting_to_trixel) —
    // cast and receive recover the same world position by construction.
    const ivec2 detachedBase = trixelFrameOffset(
        trixelOriginOffsetZ1(detachedSize), frameCanvasOffset, voxelRenderOptions
    );
    const ivec2 isoRel = cell - detachedBase;
    const vec3 modelPos = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));

    // Lift to WORLD: the world cell origin is in world cells; the model
    // recovery above is in subdivision units, so the offset scales by the
    // effective subdivision factor.
    const int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    const ivec3 worldPos =
        roundHalfUp(modelPos + detachedWorldReceive.xyz * float(scale));

    // Re-project into the MAIN-canvas cardinal distance layout, mirroring
    // c_voxel_to_trixel_stage_1's cardinal store exactly (same output side as
    // the per-axis resolve scatter): rotate the world position into the
    // cardinal VIEW frame, add the lower-corner shift (scaled to subdivision
    // units), key by un-yawed iso depth, place at the un-yawed iso pixel. The
    // BAKE recovery (trixelCanvasPixelToWorld3D at this rasterYaw) is the
    // exact inverse.
    const int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    ivec3 viewPos = worldPos;
    if (cardinalIndex != 0) {
        viewPos = rotateCardinalZ(worldPos, cardinalIndex);
        viewPos += cardinalLowerCornerShift(cardinalIndex) * scale;
    }

    const ivec2 mainBase = trixelFrameOffset(
        trixelOriginOffsetZ1(canvasSizePixels), frameCanvasOffset, voxelRenderOptions
    );

    // Emit the micro-cell's two-pixel diamond region (#1724), not just its
    // origin pixel: roundHalfUp collapses a region's input pixels onto one
    // recovered cell, so a single-pixel write left the resolve ~50% sparse —
    // pinhole casters whose world-placed shadows dithered. The detached store
    // is model-frame (slot = model face axis), so rotate the face into the
    // view frame the same way the position was to pick the region
    // (faceOffset_2x3 is polarity-blind: axis only).
    const ivec3 viewNormal =
        rotateCardinalZ(faceOutwardNormal6I(slot << 1), cardinalIndex);
    const int viewAxis =
        (viewNormal.x != 0) ? kXFace : ((viewNormal.y != 0) ? kYFace : kZFace);
    emitResolveCardinalDiamond(
        viewPos, viewAxis, slot, flip, mainBase, canvasSizePixels
    );
}
