#version 450 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
// Shared caster/receiver sun-space projection + depth pack (#2083).
#include "ir_sun_projection.glsl"

const int kEmptyDistanceEncoded = 65535;

layout(std430, binding = 28) restrict buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = baking
    // a per-axis voxel canvas into the shared sun map (#1311).
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path (#1311).
    uniform ivec4 visibleFaceIds;
};

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
    // #2270 coverage-splat radius (sun texels), doubling as the kill switch —
    // 0 ⇒ the exact single-write path (saturated hosts byte-identical). See
    // FrameDataSun in ir_render_types.hpp and docs/design/sun-shadow-bake-coverage.md.
    uniform float sunSplatMaxTexels;
    uniform float sunMaxShadowThrow;  // #2320; unused here (receiver-only)
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;

// atomicMin the packed sun depth into one texel of a cascade, if in bounds.
// The bounds check is a buffer-bounds guard, not a culling decision (#2083): a
// caster outside THIS cascade's UV range is unreadable here by any receiver the
// sample side accepts — sunCascadeKernelInterior (ir_sun_projection.glsl) routes
// receivers near the map edge to the covering cascade, whose wider AABB holds
// this caster's write. Every caster is projected into BOTH cascades below, so
// this early-out never drops a caster from the pipeline.
void writeSunTexel(int cascadeOffset, ivec2 px, uint packedDepth) {
    if (px.x < 0 || px.x >= kSunShadowMapDim ||
        px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    atomicMin(sunDepthBuf[cascadeOffset + px.y * kSunShadowMapDim + px.x], packedDepth);
}

// #2270 coverage splat. Writes the caster's own texel (the exact single write,
// byte-identical when radius == 0), then atomicMin's the SAME depth into a
// (2·radius+1)² box around it, filling the sun texels a grazing / point-
// scattered caster footprint leaves empty (the moth-eaten cast-shadow holes).
// atomicMin is what preserves saturated-host byte-identity: where nearer real
// geometry already covers a box texel, the farther splat is a no-op — so a host
// whose bake is already dense sees no change, and the fill concentrates on the
// genuinely-empty hole texels. The uniform box (rather than a per-pixel
// oriented walk) is deliberate: the holes are 2D point-scatter, not a 1D
// silhouette line, so a directional walk under-covers (measured — see
// docs/design/sun-shadow-bake-coverage.md).
void bakeCascadeBox(vec3 sp, vec2 origin, vec2 texelSz, int cascadeOffset, int radius) {
    ivec2 base = ivec2(floor((sp.xy - origin) / texelSz));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // Splat provenance (#2319): store the DISPLACEMENT VECTOR (dx, dy) of
            // this box texel from the caster's own (0,0) texel, so the receiver
            // can reconstruct the write's true origin (px - (dx,dy)) and reject a
            // same-plane self-occluder while keeping a genuine cast at the base
            // bias (ir_sun_shadow_sample same-plane test). Free — the box loop
            // already carries (dx, dy). radius 0 ⇒ only (0,0) ⇒ a direct write ⇒
            // byte-identical to the pre-splat single write.
            writeSunTexel(cascadeOffset, base + ivec2(dx, dy),
                          packSunDepth(sp.z, ivec2(dx, dy)));
        }
    }
}

void main() {
    ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    ivec2 size = imageSize(trixelDistances);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = imageLoad(trixelDistances, pixel).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        return;
    }
    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (#1458 per-axis / single-canvas, flip carrier #2207). The bake is
    // position-only — the flip bit never changes a caster's plane position, so
    // it is decoded past, not consumed.
    int rawDepth = decodeDepthRoute(encoded, perAxisRoute);

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. A per-axis canvas stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    vec3 pos3D;
    if (perAxisRoute != 0) {
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, visibleFaceIds[decodeSlot(encoded)], size,
            frameCanvasOffset, voxelRenderOptions
        );
    } else if (residualYaw != 0.0) {
        // Smooth-yaw cast (#1719). While rotating, the single canvas's
        // remaining SDF/text content is stored at the FULL visualYaw with
        // view-frame depth (#1345/#1370) — recover with the matching smooth
        // inverse so those casters bake at their true world positions. The
        // CARDINAL-layout resolve textures (per-axis #1435 + world-placed
        // P4b-3) bake with residualYaw zeroed by the C++ driver, so they keep
        // the cardinal recovery below.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
    }

    // Shared caster/receiver projection (#2083) — the receiver lookup
    // (ir_sun_shadow_sample.glsl worldSunShadowFactor) derives its sun UV +
    // depth from this same function, so cast and receive cannot drift.
    vec3 sunProj = sunSpaceProject(
        pos3D, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz
    );

    // #2270 coverage splat. The gate below is a DECODE-PATH predicate, not a
    // camera-cardinality one: the raw smooth-yaw single-canvas content
    // (residualYaw != 0) and the per-axis face-local store (perAxisRoute != 0)
    // skip it, so it engages for the cardinal main-canvas bake AND the two
    // CARDINAL-layout resolve dispatches (per-axis #1435, world-placed P4b-3),
    // which spoof residualYaw == 0 with perAxisRoute == 0 to reuse the cardinal
    // recovery. The C++ driver disambiguates via sunSplatMaxTexels: it zeros the
    // radius for the PER-AXIS resolve (patchSunSplatRadius) so invariant #1's
    // per-axis / smooth-yaw byte-identity is structural, but keeps it for the
    // WORLD-PLACED resolve (whose cast carries the same point-scatter defect the
    // splat must fill — measured). The atomicMin box preserves saturated-host
    // byte-identity (farther splats no-op where geometry is dense).
    int radius = 0;
    if (perAxisRoute == 0 && residualYaw == 0.0 && sunSplatMaxTexels > 0.0) {
        radius = int(sunSplatMaxTexels);
    }

    bakeCascadeBox(sunProj, cascadeOriginUV_0, cascadeTexelSize_0, 0, radius);
    bakeCascadeBox(sunProj, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, radius);
}
