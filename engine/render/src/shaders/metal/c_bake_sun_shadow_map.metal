#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
// Shared caster/receiver sun-space projection + depth pack (#2083).
#include "ir_sun_projection.metal"
#include <metal_atomic>

// Mirrors shaders/c_bake_sun_shadow_map.glsl. Projects each rasterized
// iso pixel into both cascade regions of the sun shadow depth buffer.

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
    // #2270 coverage-splat radius (sun texels), doubling as the kill switch —
    // 0 => the exact single-write path (saturated hosts byte-identical). See
    // FrameDataSun in ir_render_types.hpp.
    float sunSplatMaxTexels;
    float _cascadePad1;
};

// atomic_fetch_min the packed sun depth into one texel of a cascade, if in
// bounds. The bounds check is a buffer-bounds guard, not a culling decision
// (#2083): sunCascadeKernelInterior (ir_sun_projection.metal) routes receivers
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

// #2270 coverage splat. Writes the caster's own texel (the exact single write,
// byte-identical when radius == 0), then atomic_fetch_min's the SAME depth into
// a (2·radius+1)^2 box around it, filling the sun texels a grazing / point-
// scattered caster footprint leaves empty (the moth-eaten cast-shadow holes).
// atomic_fetch_min preserves saturated-host byte-identity: where nearer real
// geometry already covers a box texel, the farther splat is a no-op, so a
// dense-bake host sees no change and the fill concentrates on genuinely-empty
// hole texels. The uniform box (rather than a per-pixel oriented walk) is
// deliberate: the holes are 2D point-scatter, not a 1D silhouette line, so a
// directional walk under-covers (measured — see
// docs/design/sun-shadow-bake-coverage.md). Mirrors GLSL.
inline void bakeCascadeBox(
    device atomic_uint *sunDepthBuf, float3 sp,
    float2 origin, float2 texelSz, int cascadeOffset, int radius
) {
    int2 base = int2(floor((sp.xy - origin) / texelSz));
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            // Splat provenance (#2319): the Chebyshev displacement of this box
            // texel from the caster's own (dx=dy=0) texel, clamped to the 3-bit
            // pack field, so the receiver widens its near-rejection only as far
            // as this texel was displaced (a direct write stays at the base
            // bias). Free — the box loop already carries (dx, dy). radius 0 ⇒
            // only (0,0) ⇒ splatDist 0 ⇒ byte-identical. Mirrors GLSL.
            uint splatDist = uint(min(max(abs(dx), abs(dy)), 7));
            writeSunTexel(sunDepthBuf, cascadeOffset, base + int2(dx, dy),
                          packSunDepth(sp.z, splatDist));
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
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        return;
    }
    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (#1458 per-axis / single-canvas, flip carrier #2207). The bake is
    // position-only — the flip bit never changes a caster's plane position, so
    // it is decoded past, not consumed.
    int rawDepth = decodeDepthRoute(encoded, frameData.perAxisRoute);

    // Smooth camera Z-yaw (#1311): the per-axis voxel canvases bake into the same
    // shared sun depth map as the main canvas (SDF/text) so voxels and shapes
    // shadow each other under rotation. Per-axis stores the world frame
    // face-locally; the single canvas stores the cardinal-snapped iso pixel.
    float3 pos3D;
    if (frameData.perAxisRoute != 0) {
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, frameData.visibleFaceIds[decodeSlot(encoded)], size,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions
        );
    } else if (frameData.residualYaw != 0.0) {
        // Smooth-yaw cast (#1719). While rotating, the single canvas's
        // remaining SDF/text content is stored at the FULL visualYaw with
        // view-frame depth (#1345/#1370) — recover with the matching smooth
        // inverse so those casters bake at their true world positions. The
        // CARDINAL-layout resolve textures (per-axis #1435 + world-placed
        // P4b-3) bake with residualYaw zeroed by the C++ driver, so they keep
        // the cardinal recovery below. Mirrors GLSL.
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

    // Shared caster/receiver projection (#2083) — the receiver lookup
    // (ir_sun_shadow_sample.metal worldSunShadowFactor) derives its sun UV +
    // depth from this same function, so cast and receive cannot drift.
    float3 sunProj = sunSpaceProject(
        pos3D,
        sunFrameData.sunBasisU.xyz,
        sunFrameData.sunBasisV.xyz,
        sunFrameData.sunDirection.xyz
    );

    // #2270 coverage splat. The gate is a DECODE-PATH predicate, not a
    // camera-cardinality one: the raw smooth-yaw and per-axis face-local inputs
    // skip it, so it engages for the cardinal main-canvas bake AND the two
    // CARDINAL-layout resolve dispatches (per-axis #1435, world-placed P4b-3),
    // which spoof residualYaw == 0 with perAxisRoute == 0. The C++ driver
    // disambiguates via sunSplatMaxTexels: it zeros the radius for the PER-AXIS
    // resolve (patchSunSplatRadius) so invariant #1's per-axis / smooth-yaw
    // byte-identity is structural, but keeps it for the WORLD-PLACED resolve
    // (whose cast carries the same point-scatter defect — measured). The
    // atomic_fetch_min box preserves saturated-host byte-identity (farther splats
    // no-op where geometry is dense). Mirrors GLSL.
    int radius = 0;
    if (frameData.perAxisRoute == 0 && frameData.residualYaw == 0.0 &&
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
