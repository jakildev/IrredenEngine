#include "ir_iso_common.metal"
// Shared caster/receiver sun-space projection + depth pack.
#include "ir_sun_projection.metal"
#include <metal_atomic>

// Mirrors shaders/c_bake_sun_shadow_map.glsl.

constant int kEmptyDistanceEncoded = 65535;

struct FrameDataSun {
    float4 sunDirection;
    float sunIntensity;
    float sunAmbient;
    int shadowsEnabled;
    int aoEnabled;
    float4 sunBasisU;
    float4 sunBasisV;
    float2 sunBufferOriginUV;
    float2 sunBufferTexelSize;
    float2 cascadeOriginUV_0;
    float2 cascadeTexelSize_0;
    float2 cascadeOriginUV_1;
    float2 cascadeTexelSize_1;
    float cascadeSplitDepth;
    int cascadeCount;
    // Coverage-splat radius (sun texels), doubling as the kill switch —
    // 0 => the exact single-write path. Mirrors FrameDataSun in ir_render_types.hpp.
    float sunSplatMaxTexels;
    float sunMaxShadowThrow;  // unused here (receiver-only)
};

// The bounds check is a buffer-bounds guard, not a culling decision:
// sunCascadeKernelInterior (ir_sun_projection.metal) routes receivers
// near the map edge to the covering cascade whose wider AABB holds this
// caster's write, and every caster is projected into BOTH cascades, so this
// never drops a caster. Mirrors GLSL.
inline void writeSunTexel(
    device atomic_uint *sunDepthBuf, int cascadeOffset, int2 px, uint packedDepth
) {
    if (px.x < 0 || px.x >= kSunShadowMapDim ||
        px.y < 0 || px.y >= kSunShadowMapDim) {
        return;
    }
    atomic_fetch_min_explicit(
        &sunDepthBuf[cascadeOffset + px.y * kSunShadowMapDim + px.x],
        packedDepth,
        memory_order_relaxed
    );
}

// Coverage splat: radius 0 is the exact single write. atomic_fetch_min keeps a
// dense bake unchanged: where nearer real geometry already covers a box texel,
// the farther splat is a no-op, so the fill concentrates on the genuinely-empty
// hole texels a grazing / point-scattered caster footprint leaves (the
// moth-eaten cast-shadow holes). The uniform box (rather than a
// per-pixel oriented walk) is deliberate: the holes are 2D point-scatter, not a 1D
// silhouette line, so a directional walk under-covers
// (docs/design/sun-shadow-bake-coverage.md). Mirrors GLSL.
inline void bakeCascadeBox(
    device atomic_uint *sunDepthBuf, float3 sp,
    float2 origin, float2 texelSz, int cascadeOffset, int radius
) {
    int2 base = int2(floor((sp.xy - origin) / texelSz));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // Splat provenance: store the DISPLACEMENT VECTOR (dx, dy) of this box
            // texel from the caster's own (0,0) texel, so the receiver can
            // reconstruct the write's true origin (px - (dx,dy)) and reject a
            // same-plane self-occluder while keeping a genuine cast at the base
            // bias (ir_sun_shadow_sample same-plane test). radius 0 ⇒ only (0,0) ⇒
            // a direct write. Mirrors GLSL.
            writeSunTexel(sunDepthBuf, cascadeOffset, base + int2(dx, dy),
                          packSunDepth(sp.z, int2(dx, dy)));
        }
    }
}

kernel void c_bake_sun_shadow_map(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device atomic_uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }
    // Face/flip bits do not change a caster's plane position.
    int rawDepth = decodeDepthSingle(encoded);

    // Main SDF/text depth follows visual yaw. Per-axis and detached resolves
    // have cardinal-layout depth; their dispatches supply zero residual yaw.
    float3 pos3D;
    if (frameData.residualYaw != 0.0) {
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.visualYaw
        );
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
    }

    // Shared caster/receiver projection — the receiver lookup
    // (ir_sun_shadow_sample.metal worldSunShadowFactor) derives its sun UV +
    // depth from this same function, so cast and receive cannot drift.
    float3 sunProj = sunSpaceProject(
        pos3D,
        sunFrameData.sunBasisU.xyz,
        sunFrameData.sunBasisV.xyz,
        sunFrameData.sunDirection.xyz
    );

    // Only cardinal-layout depth uses coverage splats. The driver suppresses
    // them for the dense per-axis resolve, but keeps them for detached point
    // scatter (docs/design/sun-shadow-bake-coverage.md).
    int radius = 0;
    if (frameData.residualYaw == 0.0 &&
        sunFrameData.sunSplatMaxTexels > 0.0) {
        radius = int(sunFrameData.sunSplatMaxTexels);
    }

    bakeCascadeBox(
        sunDepthBuf, sunProj,
        sunFrameData.cascadeOriginUV_0, sunFrameData.cascadeTexelSize_0, 0, radius
    );
    bakeCascadeBox(
        sunDepthBuf, sunProj,
        sunFrameData.cascadeOriginUV_1, sunFrameData.cascadeTexelSize_1, kCascadeTexelCount, radius
    );
}
