#version 450 core

// Per-pixel ambient-occlusion compute. For each pixel on a rasterized
// surface — voxel OR shape, since both write encoded face+depth via
// `encodeDepthWithFace` — samples four face-tangent neighbour pixels in
// `trixelDistances` and counts each one as occluding when its decoded
// surface position sits in front of the receiver's face plane by ~1
// voxel along the face-outward normal AND belongs to a DIFFERENT visible
// face. The different-face gate is what keeps a rotated voxel surface from
// self-darkening: a real crease / contact is always two distinct faces
// meeting, whereas a same-face neighbour ~1 voxel in front is just the
// round-to-cell stair-step of a tilted-flat surface (the alternating-band
// speckle that reads as "missing sections" on rotating solids). Flat
// cardinal faces are coplanar (d ~ 0) so the gate never changes them.
//
// Tilt-aware same-face resample (#1718): a re-voxelize / REBUILD_GRID
// rotating solid is rebuilt as real voxels, so its tilted-flat surface
// becomes a true voxel staircase whose tread (+Z) and riser (±X/±Y) ARE
// different faces — the different-face gate alone would then count every
// 1-cell step as a crease and re-introduce the banding. The resample
// (search "Tilt-aware" below) looks one cell beyond a different-face
// step: a monotone staircase returns to the receiver's own face there
// (suppress), whereas a genuine concave crease (the L-prism notch) meets
// a perpendicular wall (keep). Per-pixel, so legitimate creases keep AO.
//
// SDF shapes participate in crease AO automatically because their
// `trixelDistances` writes are visible to the sampling — no separate
// rasterization into a side buffer is needed.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"

// Same threshold LIGHTING_TO_TRIXEL uses for "empty pixel" — encoded
// distances >= 65535 mean the clear value was never overwritten.
const int kEmptyDistanceEncoded = 65535;

// Crease-band along the face-outward normal. A neighbour pixel is treated
// as occluding when its decoded `pos3D'` is between `kAOMinHeight` and
// `kAOMaxHeight` voxels in front of the receiver's face plane.
//   Lower bound rejects coplanar continuations (flat surface, d ≈ 0).
//   Upper bound rejects deep voids (cliffs, d ≫ 1) so they don't darken.
// The canonical edge-occluder voxel sits at d = `kAOOccluderHeight`; the
// band is centred there with a ±`kAOBandHalfWidth` voxel tolerance, extended
// below by `kAOSubVoxelTolerance` so SDF surfaces whose decoded depth lands
// between the receiver and the canonical voxel still register.
// Must stay in lockstep with the matching constants in
// c_compute_voxel_ao.metal.
const float kAOOccluderHeight = 1.0;
const float kAOBandHalfWidth = 0.5;
const float kAOSubVoxelTolerance = 0.375;
const float kAOMinHeight = kAOOccluderHeight - kAOBandHalfWidth - kAOSubVoxelTolerance;
const float kAOMaxHeight = kAOOccluderHeight + kAOBandHalfWidth;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single-canvas raster; nonzero
    // = lighting a per-axis canvas (#1311), so reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5) — see c_voxel_to_trixel_stage_1.glsl + #1278.
    // AO maps the decoded depth slot → world FaceId via this lookup so the
    // outward-normal step uses the rotation-aware six-face normal (not the
    // cardinal-0 lower-coord assumption that broke at non-zero cardinal).
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
    uniform float _cascadePad0;
    uniform float _cascadePad1;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasAO;

// Per-axis compacted occupied-cell list + per-axis indirect-args region (#2256).
// On the per-axis path the dispatch is 1-D over the compacted cells; the shader
// recovers each cell's canvas pixel from the linear index below. Bound per axis
// via bindRange (offsets into the three axis regions).
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
        // #2256: indirect dispatch over the compacted occupied-cell list, folded
        // into a capped 2-D workgroup grid by c_per_axis_cell_finalize; recover
        // the flat group index the same way c_voxel_visibility_compact does.
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
    const int kEmpty = (perAxisRoute != 0) ? 0x7FFFFFFF : kEmptyDistanceEncoded;
    if (encoded >= kEmpty) {
        imageStore(canvasAO, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (aoEnabled == 0) {
        imageStore(canvasAO, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    // Decode the visible-triplet slot (0/1/2) the rasterizer wrote (#1278).
    // Slot → world FaceId via `visibleFaceIds[slot]` — single source of
    // face metadata shared with the raster, so AO's "step out of the
    // surface" arithmetic uses the actually-visible face's outward normal
    // and tangents at every cardinal (not the cardinal-0 lower-coord
    // assumption the pre-#1278 path baked in).
    int slot = encoded & 3;
    int faceId = visibleFaceIds[slot];
    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally (perAxisRoute != 0), so recover world-pos via
    // isoPixelToPos3D; the single canvas stores the cardinal-snapped iso
    // pixel, recovered via trixelCanvasPixelToWorld3D. Per-axis canvases are
    // only allocated while rotating, so the single-canvas path stays byte-
    // identical at the cardinal fast path.
    bool perAxis = perAxisRoute != 0;
    vec3 pos3D = perAxis
        ? perAxisCellToWorld3D(pixel, rawDepth, faceId, size, frameCanvasOffset, voxelRenderOptions)
        : trixelCanvasPixelToWorld3D(
              pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, cardinalIndex
          );

    // World-frame outward normal + in-plane tangents for the camera-visible
    // face this pixel rendered. The tangent step is rotated through
    // R_z(-rasterYaw) before iso projection so the neighbour-sample
    // direction lands on the canvas pixel that actually holds the
    // +tangent neighbour at this cardinal (PR #1275 prep).
    vec3 worldOutward = vec3(faceOutwardNormal6I(faceId));
    ivec3 t1, t2;
    // Pick the two in-plane tangents per face axis. The pair direction
    // doesn't matter (AO samples ±t1, ±t2); the per-axis split must
    // match across X / Y / Z faces of both polarities.
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

    // Subdivision modes scale canvas pixels by
    // `effectiveTrixelSubdivisionScale` so the iso offset for a one-
    // voxel step grows accordingly.
    int scale = effectiveTrixelSubdivisionScale(voxelRenderOptions);
    ivec2 deltaT1;
    ivec2 deltaT2;
    if (perAxis) {
        // Per-axis canvas is BASE-RESOLUTION (#1458): 1 cell = 1 world voxel.
        // A +/-1 cell step along each canvas axis is the +/-1 in-plane neighbour.
        deltaT1 = ivec2(1, 0);
        deltaT2 = ivec2(0, 1);
    } else {
        ivec3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
        ivec3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
        deltaT1 = pos3DtoPos2DIso(t1View) * scale;
        deltaT2 = pos3DtoPos2DIso(t2View) * scale;
    }

    int occl = 0;
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

        int neighbourRawDepth = (perAxisRoute != 0) ? (neighbourEncoded >> 10) : (neighbourEncoded >> 2);
        vec3 neighbourPos3D;
        if (perAxis) {
            int neighbourFaceId = visibleFaceIds[neighbourEncoded & 3];
            neighbourPos3D = perAxisCellToWorld3D(
                samplePixel, neighbourRawDepth, neighbourFaceId, size,
                frameCanvasOffset, voxelRenderOptions
            );
        } else {
            neighbourPos3D = trixelCanvasPixelToWorld3D(
                samplePixel, neighbourRawDepth, trixelCanvasOffsetZ1,
                frameCanvasOffset, voxelRenderOptions, cardinalIndex
            );
        }

        // A DIFFERENT visible face ~1 voxel in front is the classic crease /
        // contact occluder; a SAME-face neighbour at d ~ 1 is the round-to-cell
        // stair-step of a rotated (tilted-flat) surface (the "missing sections"
        // speckle) and is excluded by the slot test. Flat cardinal faces are
        // coplanar (d ~ 0, below kAOMinHeight) so they never reach this gate and
        // the cardinal fast path stays byte-identical.
        float d = dot(neighbourPos3D - pos3D, worldOutward);
        if ((neighbourEncoded & 3) == slot || d <= kAOMinHeight || d >= kAOMaxHeight) continue;

        // Tilt-aware same-face resample (#1718). A re-voxelized / REBUILD_GRID
        // rotating solid turns a tilted-flat surface into a true voxel staircase
        // whose tread (+Z) and riser (±X/±Y) ARE different faces, so every 1-cell
        // step reads as a different-face crease at d ~ 1 — the venetian banding.
        // The riser of a quantized flat and a genuine concave crease (the L-prism
        // notch) are locally identical here — same face normals, same relative
        // position — so the only screen-space signal is whether the surface
        // RETURNS to the receiver's own face one cell beyond the step: a monotone
        // staircase continues as the next tread (same slot, still in front),
        // whereas a real crease meets a multi-cell perpendicular wall that does
        // not. Single-canvas path only — a per-axis canvas holds a single face so
        // its stair-step AO already drops out, and the GRID solids this targets
        // raster cardinal (perAxisRoute == 0).
        if (!perAxis) {
            ivec2 beyondPixel = pixel + 2 * delta;
            if (beyondPixel.x >= 0 && beyondPixel.x < size.x &&
                beyondPixel.y >= 0 && beyondPixel.y < size.y) {
                int beyondEncoded = imageLoad(trixelDistances, beyondPixel).x;
                if (beyondEncoded < kEmpty && (beyondEncoded & 3) == slot) {
                    vec3 beyondPos3D = trixelCanvasPixelToWorld3D(
                        beyondPixel, beyondEncoded >> 2, trixelCanvasOffsetZ1,
                        frameCanvasOffset, voxelRenderOptions, cardinalIndex
                    );
                    // The next tread steps ~1 voxel further out along the
                    // receiver normal; a coplanar same-face blip (d ~ 0) is not a
                    // staircase and must keep its AO.
                    if (dot(beyondPos3D - pos3D, worldOutward) > kAOMinHeight) continue;
                }
            }
        }
        occl++;
    }

    // Each occluding edge-neighbour darkens by 10%; all four caps at 60%
    // brightness keeps crease darkening visually subtle.
    float ao = 1.0 - float(occl) * 0.10;
    imageStore(canvasAO, pixel, vec4(ao, 0.0, 0.0, 0.0));
}
