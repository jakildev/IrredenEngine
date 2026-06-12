#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
// FrameDataSun, the cascade PCF sampler, and the world-space
// worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's detached
// world-receive path (#1576 P4b-2).
#include "ir_sun_shadow_sample.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl. Per-pixel directional sun
// shadow compute with cascaded shadow maps.

constant int kEmptyDistanceEncoded = 65535;

kernel void c_compute_sun_shadow(
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
    uint3 globalId [[thread_position_in_grid]]
) {
    int2 pixel = int2(globalId.xy);
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    if (pixel.x >= size.x || pixel.y >= size.y) return;

    int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as empty sentinel (#1458); single-canvas keeps 65535.
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
    if (sunFrameData.shadowsEnabled == 0) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }

    // Per-axis encoding (#1458): rawDepth in bits [31:10]; single-canvas: bits [31:2].
    int rawDepth = (frameData.perAxisRoute != 0) ? (encoded >> 10) : (encoded >> 2);
    int face = encoded & 3;
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    // Smooth camera Z-yaw (#1311): a per-axis canvas stores the world frame
    // face-locally — recover world-pos via faceOriginFromInPlane and read the
    // world-frame outward normal directly. The single canvas keeps its
    // cardinal-snap reconstruction + R_z(-rasterYaw) normal rotation. Mirrors GLSL.
    bool perAxis = frameData.perAxisRoute != 0;
    float3 pos3D;
    float3 normal;
    if (perAxis) {
        int faceId = frameData.visibleFaceIds[face];
        pos3D = perAxisCellToWorld3D(
            pixel, rawDepth, faceId, size,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions
        );
        normal = faceOutwardNormal6(faceId);
    } else if (frameData.residualYaw != 0.0) {
        // Smooth-yaw receive (#1719). While rotating, voxels leave the single
        // canvas (per-axis scatter) and its remaining SDF/text content is
        // stored at the FULL visualYaw with view-frame depth (#1345/#1370) —
        // recover with the matching smooth inverse. The cardinal recovery
        // returns a residual-rotated world pos here, so receivers sampled the
        // sun map off the true surface (frozen / vanishing floor shadows).
        // residualYaw == 0 keeps the byte-identical cardinal path. Mirrors GLSL.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.visualYaw
        );
        normal = rotateYawZInv(faceOutwardNormal(face), frameData.visualYaw);
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel,
            rawDepth,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.rasterYaw
        );
        // Rotate raster-frame face normal to world frame so normal bias and slope
        // bias are applied in the correct world-space direction at non-zero camera
        // yaw. No-op at yaw=0 (cardinalIndex=0). Matches the AO shader pattern.
        normal = rotateCardinalZInv(faceOutwardNormal(face), cardinalIndex);
    }

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.metal, #1576).
    float factor = worldSunShadowFactor(pos3D, normal, float(rawDepth), sunFrameData, sunDepthBuf);
    canvasSunShadow.write(float4(factor, 0.0, 0.0, 0.0), uint2(pixel));
}
