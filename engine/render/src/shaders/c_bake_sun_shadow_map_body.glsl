
// Reconstructs pos3D for each rasterized iso pixel and atomicMin's its
// packed sun-space depth into both cascade regions of the sun shadow
// depth SSBO. Companion to c_compute_sun_shadow.glsl's screen-space
// lookup branch. Shared body: c_bake_sun_shadow_map.glsl and
// c_bake_sun_shadow_map_second_depth.glsl define IR_SUN_SECOND_DEPTH;
// c_bake_sun_shadow_map_cooperative.glsl defines IR_SUN_BAKE_COOPERATIVE and
// the smaller group size its threadgroup window reduces.

#ifndef IR_SUN_BAKE_GROUP_SIZE
#define IR_SUN_BAKE_GROUP_SIZE 16
#endif
#ifndef IR_SUN_BAKE_COOPERATIVE
#define IR_SUN_BAKE_COOPERATIVE 0
#endif
#if IR_SUN_BAKE_COOPERATIVE && IR_SUN_SECOND_DEPTH
#error "The second-depth diagnostic keeps per-pixel writes"
#endif

layout(local_size_x = IR_SUN_BAKE_GROUP_SIZE, local_size_y = IR_SUN_BAKE_GROUP_SIZE, local_size_z = 1) in;

#include "ir_iso_common.glsl"
layout(rg32ui, binding = 4) readonly uniform uimage2D fogShadowWindow;
#include "ir_fog_shadow_caster.glsl"
// Shared caster/receiver sun-space projection + depth pack.
#include "ir_sun_projection.glsl"
// The primary layer's slab extrema, and under IR_SUN_SECOND_DEPTH the indexed
// fallback layer the second-depth diagnostic borrows while no finite index is
// published.
#include "ir_sun_face_query_layout.glsl"

const int kEmptyDistanceEncoded = 65535;

layout(std430, binding = 28) restrict buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};
#include "ir_sun_depth_slab_write.glsl"
#if IR_SUN_BAKE_COOPERATIVE
#include "ir_sun_depth_slab_window.glsl"
#endif

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
    uniform vec4 sunCasterViewToWorld;  // unused here; places fogCeilingEnabled at 144
    uniform ivec4 fogCeilingEnabled;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rg32ui, binding = 3) readonly uniform uimage2D trixelEntityIds;

// atomicMin the packed sun depth into one texel of a cascade, if in bounds.
// The bounds check is a buffer-bounds guard, not a culling decision: a
// caster outside THIS cascade's UV range is unreadable here by any receiver the
// sample side accepts — sunCascadeKernelInterior (ir_sun_projection.glsl) routes
// receivers near the map edge to the covering cascade, whose wider AABB holds
// this caster's write. Every caster is projected into BOTH cascades, so this
// early-out never drops a caster from the pipeline.
//
// IR_SUN_SECOND_DEPTH (the c_bake_sun_shadow_map_second_depth wrapper): the
// nearest slot's loser is kept in the indexed fallback layer (empty while no
// finite index is published) when its quantized depth differs from the
// winner's, so one surface's direct and splat words stay one depth. The
// receiver tests that layer independently. Two distinct distant depths still
// hide a lower caster: a bounded diagnostic, not a repair.
void writeSunTexel(int cascadeOffset, ivec2 px, uint packedDepth) {
    if (px.x < 0 || px.x >= kSunShadowMapDim ||
        px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    const int index = cascadeOffset + px.y * kSunShadowMapDim + px.x;
#if IR_SUN_SECOND_DEPTH
    const uint previous = atomicMin(sunDepthBuf[index], packedDepth);
    writeSunDepthSlab(index, packedDepth);
    if (previous == 0xFFFFFFFFu) {
        return;
    }
    const uint displaced = max(previous, packedDepth);
    if ((displaced >> 8) == (min(previous, packedDepth) >> 8)) {
        return;
    }
    atomicMin(sunDepthBuf[int(kSourceFaceFallbackOffset) + index], displaced);
#else
    writeSunPrimaryDepth(index, packedDepth);
#endif
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

#if IR_SUN_BAKE_COOPERATIVE
const uint kBakeLanes = uint(IR_SUN_BAKE_GROUP_SIZE * IR_SUN_BAKE_GROUP_SIZE);

// One cascade through the shared window: reset, footprint reduction, init,
// per-write routed splat, flush, a barrier between each. Every lane runs every
// phase and reaches every barrier; a lane without a caster only skips its own
// reduce and splat, and a tile without a caster has an empty footprint, so its
// init and flush loops run nothing. Lanes agree on the radius, which depends
// on frame uniforms alone.
void bakeCascadeCooperative(
    bool caster, vec3 sp, vec2 origin, vec2 texelSz, int cascadeOffset, int radius
) {
    const uint lane = gl_LocalInvocationIndex;
    sunWindowReset(lane);
    memoryBarrierShared();
    barrier();
    ivec2 base = ivec2(0);
    if (caster) {
        base = ivec2(floor((sp.xy - origin) / texelSz));
        sunWindowReduceBase(base);
    }
    memoryBarrierShared();
    barrier();
    const ivec2 anchor = sunWindowAnchor(radius);
    const ivec2 extent = sunWindowExtent(radius);
    sunWindowInit(lane, kBakeLanes, extent);
    memoryBarrierShared();
    barrier();
    if (caster) {
        for (int dy = -radius; dy <= radius; ++dy) {
            for (int dx = -radius; dx <= radius; ++dx) {
                writeSunTexelCooperative(cascadeOffset, base + ivec2(dx, dy), anchor,
                                         packSunDepth(sp.z, ivec2(dx, dy)));
            }
        }
    }
    memoryBarrierShared();
    barrier();
    sunWindowFlush(lane, kBakeLanes, cascadeOffset, anchor, extent);
    memoryBarrierShared();
    barrier();
}

void main() {
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(trixelDistances);
    bool caster = pixel.x < size.x && pixel.y < size.y;
    const int encoded = caster ? imageLoad(trixelDistances, pixel).x : kEmptyDistanceEncoded;
    caster = caster && encoded < kEmptyDistanceEncoded;
    vec3 sunProj = vec3(0.0);
    if (caster) {
        const int rawDepth = decodeDepthSingle(encoded);
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
        const bool fogBodyOrPrecut =
            fogCeilingEnabled.y != 0 || decodeFogBody(imageLoad(trixelEntityIds, pixel).xy);
        if (!(fogBodyOrPrecut) && fogCeilingEnabled.x != 0 && fogShadowVisionCircleCount > 0 &&
            !fogFieldCastsSunShadow(pos3D, false, fogCeilingEnabled.x, fogShadowCell(pos3D))) {
            caster = false;
        }
        sunProj = sunSpaceProject(pos3D, sunBasisU.xyz, sunBasisV.xyz, sunDirection.xyz);
    }
    int radius = 0;
    if (residualYaw == 0.0 && sunSplatMaxTexels > 0.0) {
        radius = int(sunSplatMaxTexels);
    }
    bakeCascadeCooperative(caster, sunProj, cascadeOriginUV_0, cascadeTexelSize_0, 0, radius);
    bakeCascadeCooperative(
        caster, sunProj, cascadeOriginUV_1, cascadeTexelSize_1, kCascadeTexelCount, radius
    );
}
#else
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

    const bool fogBodyOrPrecut =
        fogCeilingEnabled.y != 0 || decodeFogBody(imageLoad(trixelEntityIds, pixel).xy);
    if (!(fogBodyOrPrecut) && fogCeilingEnabled.x != 0 && fogShadowVisionCircleCount > 0 &&
        !fogFieldCastsSunShadow(pos3D, false, fogCeilingEnabled.x,
            fogShadowCell(pos3D))) return;

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
#endif
