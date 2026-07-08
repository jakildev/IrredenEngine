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
    float _cascadePad0;
    float _cascadePad1;
};

inline void bakeCascade(
    float2 sunUV, float sunZ, float2 origin, float2 texelSz,
    int cascadeOffset, device atomic_uint *sunDepthBuf
) {
    int2 sunPx = int2(floor((sunUV - origin) / texelSz));
    // Buffer-bounds guard, not a culling decision (#2083): a caster outside
    // THIS cascade's UV range is unreadable here by any receiver the sample
    // side accepts — sunCascadeKernelInterior (ir_sun_projection.metal) routes
    // receivers near the map edge to the covering cascade, whose wider AABB
    // holds this caster's write. Every caster is projected into BOTH cascades,
    // so this early-out never drops a caster from the pipeline.
    if (sunPx.x < 0 || sunPx.x >= kSunShadowMapDim ||
        sunPx.y < 0 || sunPx.y >= kSunShadowMapDim) {
        return;
    }
    uint packedDepth = packSunDepth(sunZ);
    atomic_fetch_min_explicit(
        &sunDepthBuf[cascadeOffset + sunPx.y * kSunShadowMapDim + sunPx.x],
        packedDepth,
        memory_order_relaxed
    );
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

    bakeCascade(sunProj.xy, sunProj.z, sunFrameData.cascadeOriginUV_0,
                sunFrameData.cascadeTexelSize_0, 0, sunDepthBuf);
    bakeCascade(sunProj.xy, sunProj.z, sunFrameData.cascadeOriginUV_1,
                sunFrameData.cascadeTexelSize_1, kCascadeTexelCount, sunDepthBuf);
}
