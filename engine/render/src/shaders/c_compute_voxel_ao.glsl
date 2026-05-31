#version 450 core

// Per-pixel ambient-occlusion compute. For each pixel on a rasterized
// surface — voxel OR shape, since both write encoded face+depth via
// `encodeDepthWithFace` — samples four face-tangent neighbour pixels in
// `trixelDistances` and counts each one as occluding when its decoded
// surface position sits in front of the receiver's face plane by ~1
// voxel along the face-outward normal.
//
// SDF shapes participate in crease AO automatically because their
// `trixelDistances` writes are visible to the sampling — no separate
// rasterization into a side buffer is needed.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

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
    uniform int _voxelDispatchPadding;
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

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
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
    int rawDepth = encoded >> 2;
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);
    vec3 pos3D = trixelCanvasPixelToWorld3D(
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
    ivec3 t1View = cardinalIndex == 0 ? t1 : rotateCardinalZ(t1, cardinalIndex);
    ivec3 t2View = cardinalIndex == 0 ? t2 : rotateCardinalZ(t2, cardinalIndex);
    ivec2 deltaT1 = pos3DtoPos2DIso(t1View) * scale;
    ivec2 deltaT2 = pos3DtoPos2DIso(t2View) * scale;

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
        if (neighbourEncoded >= kEmptyDistanceEncoded) continue;

        int neighbourRawDepth = neighbourEncoded >> 2;
        vec3 neighbourPos3D = trixelCanvasPixelToWorld3D(
            samplePixel, neighbourRawDepth, trixelCanvasOffsetZ1,
            frameCanvasOffset, voxelRenderOptions, cardinalIndex
        );

        float d = dot(neighbourPos3D - pos3D, worldOutward);
        if (d > kAOMinHeight && d < kAOMaxHeight) occl++;
    }

    // Each occluding edge-neighbour darkens by 10%; all four caps at 60%
    // brightness keeps crease darkening visually subtle.
    float ao = 1.0 - float(occl) * 0.10;
    imageStore(canvasAO, pixel, vec4(ao, 0.0, 0.0, 0.0));
}
