#version 450 core

// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
// Shared caster/receiver sun-space projection + depth pack.
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
    // Shared frame ABI; this pass consumes only single-canvas depth.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    uniform ivec4 visibleFaceIds;
    uniform vec4 _voxelDepthAxisPadding;
    uniform vec4 _detachedWorldReceivePadding;
    uniform ivec4 _visibleIsoBoundsPadding;
    uniform ivec4 _resolveFeederPadding;
    uniform ivec4 _overflowScratchLayoutPadding;
    uniform ivec4 _overflowSortStepPadding;
    uniform vec4 _detachedViewToWorldPadding;
    uniform ivec4 perAxisStoreFrame;
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
    // Coverage-splat radius (sun texels), doubling as the kill switch —
    // 0 ⇒ the exact single-write path. Mirrors FrameDataSun in ir_render_types.hpp;
    // see docs/design/sun-shadow-bake-coverage.md.
    uniform float sunSplatMaxTexels;
    uniform float sunMaxShadowThrow;  // unused here (receiver-only)
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;

// atomicMin the packed sun depth into one texel of a cascade, if in bounds.
// The bounds check is a buffer-bounds guard, not a culling decision: a
// caster outside THIS cascade's UV range is unreadable here by any receiver the
// sample side accepts — sunCascadeKernelInterior (ir_sun_projection.glsl) routes
// receivers near the map edge to the covering cascade, whose wider AABB holds
// this caster's write. Every caster is projected into BOTH cascades, so this
// early-out never drops a caster from the pipeline.
void writeSunTexel(int cascadeOffset, ivec2 px, uint packedDepth) {
    if (px.x < 0 || px.x >= kSunShadowMapDim ||
        px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    atomicMin(sunDepthBuf[cascadeOffset + px.y * kSunShadowMapDim + px.x], packedDepth);
}

// Coverage splat. Writes the caster's own texel (the exact single write when
// radius == 0), then atomicMin's the SAME depth into a (2·radius+1)² box around
// it, filling the sun texels a grazing / point-scattered caster footprint leaves
// empty (the moth-eaten cast-shadow holes). atomicMin is what keeps a dense bake
// unchanged: where nearer real geometry already covers a box texel, the farther
// splat is a no-op — so a host whose bake is already dense sees no change, and
// the fill concentrates on the genuinely-empty hole texels. The uniform box
// (rather than a per-pixel oriented walk) is deliberate: the holes are 2D
// point-scatter, not a 1D silhouette line, so a directional walk under-covers
// (docs/design/sun-shadow-bake-coverage.md).
void bakeCascadeBox(vec3 sp, vec2 origin, vec2 texelSz, int cascadeOffset, int radius) {
    ivec2 base = ivec2(floor((sp.xy - origin) / texelSz));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // Splat provenance: store the DISPLACEMENT VECTOR (dx, dy) of this box
            // texel from the caster's own (0,0) texel, so the receiver can
            // reconstruct the write's true origin (px - (dx,dy)) and reject a
            // same-plane self-occluder while keeping a genuine cast at the base
            // bias (ir_sun_shadow_sample same-plane test). radius 0 ⇒ only (0,0) ⇒
            // a direct write.
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
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }
    // Face/flip bits do not change a caster's plane position.
    int rawDepth = decodeDepthSingle(encoded);

    // Main SDF/text depth follows visual yaw. Per-axis and detached resolves
    // have cardinal-layout depth; their dispatches supply zero residual yaw.
    vec3 pos3D;
    if (residualYaw != 0.0) {
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
    }

    // Shared caster/receiver projection — the receiver lookup
    // (ir_sun_shadow_sample.glsl worldSunShadowFactor) derives its sun UV +
    // depth from this same function, so cast and receive cannot drift.
    vec3 sunProj = sunSpaceProject(
        pos3D, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz
    );

    // Only cardinal-layout depth uses coverage splats. The driver suppresses
    // them for the dense per-axis resolve, but keeps them for detached point
    // scatter (docs/design/sun-shadow-bake-coverage.md).
    int radius = 0;
    if (residualYaw == 0.0 && sunSplatMaxTexels > 0.0) {
        radius = int(sunSplatMaxTexels);
    }

    bakeCascadeBox(sunProj, cascadeOriginUV_0, cascadeTexelSize_0, 0, radius);
    bakeCascadeBox(sunProj, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, radius);
}
