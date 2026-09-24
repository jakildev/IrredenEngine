#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
#if IR_SHAPE_RECEIVER
#include "ir_sdf_common.metal"
#include "ir_shape_data.metal"
#include "ir_shape_receiver.metal"
#include "ir_selected_shape_receiver.metal"
#include "ir_receiver_face.metal"
#endif
// FrameDataSun, the cascade PCF sampler, and the world-space
// worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's detached
// world-receive path.
#include "ir_sun_shadow_sample.metal"

// Mirrors shaders/c_compute_sun_shadow.glsl.

constant int kEmptyDistanceEncoded = 65535;

// On the per-axis path this stage is dispatched indirectly over only each axis's
// OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass). compactedCells
// holds the occupied linear cell indices; cellDrawArgs carries visibleCount at
// [kDispatchArgsBaseUint + 3]. Unused on the single-canvas 2D path.
constant uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
constant uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

kernel void IR_SUN_SHADOW_KERNEL_NAME(
#if IR_SHAPE_RECEIVER
    constant ShapeProjectionData &receiverFrame [[buffer(23)]],
    device const ShapeDescriptor *receiverShapes [[buffer(20)]],
    device const uint *receiverOwners [[buffer(22)]],
    device const ShapeTileDescriptor *receiverTiles [[buffer(30)]],
#endif
    constant FrameDataVoxelToTrixel &frameData [[buffer(7)]],
    constant FrameDataSun &sunFrameData [[buffer(29)]],
    device const uint *sunDepthBuf [[buffer(28)]],
    texture2d<int, access::read> trixelDistances [[texture(0)]],
    texture2d<float, access::write> canvasSunShadow [[texture(1)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    int2 size = int2(
        int(trixelDistances.get_width()),
        int(trixelDistances.get_height())
    );
    int2 pixel;
    if (frameData.perAxisRoute != 0) {
        // The compacted-cell dispatch is folded into a capped 2-D threadgroup
        // grid by c_per_axis_cell_finalize (groupsX capped, remainder in groupsY).
        const uint groupIndex = groupId.x + groupId.y * numGroups.x;
        const uint idx = groupIndex * kPerAxisCellComputeTile + localIndex;
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        const uint linearCell = compactedCells[idx];
        pixel = int2(int(linearCell) % size.x, int(linearCell) / size.x);
    } else {
        pixel = int2(globalId.xy);
        if (pixel.x >= size.x || pixel.y >= size.y) {
            return;
        }
    }

    int encoded = trixelDistances.read(uint2(pixel)).x;
    // Per-axis canvas uses INT_MAX as the empty sentinel; single-canvas uses 65535.
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
#if !IR_SHAPE_RECEIVER
    if (sunFrameData.shadowsEnabled == 0) {
        canvasSunShadow.write(float4(1.0, 0.0, 0.0, 0.0), uint2(pixel));
        return;
    }
#endif

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, flip carrier).
    int rawDepth = decodeDepthRoute(encoded, frameData.perAxisRoute);
    int face = decodeSlot(encoded);
    int flip = decodeFlipRoute(encoded, frameData.perAxisRoute);
    int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);

    // Smooth camera Z-yaw: a per-axis canvas stores the world frame
    // face-locally — recover world-pos via isoPixelToPos3D and read the
    // world-frame outward normal directly. The single canvas uses its
    // cardinal-snap reconstruction + R_z(-rasterYaw) normal rotation. Mirrors GLSL.
    bool perAxis = frameData.perAxisRoute != 0;
    float3 pos3D;
    float3 normal;
    if (perAxis) {
        int faceId = frameData.visibleFaceIds[face];
        // Sub-cell recovery — the receiver must sample the sun map at the
        // drawn surface, not the lattice cell origin. Mirrors GLSL.
        pos3D = perAxisCellToWorld3DSubCell(
            pixel, encoded, faceId, size,
            frameData.frameCanvasOffset, frameData.voxelRenderOptions
        );
        normal = faceOutwardNormal6(faceId);
    } else if (frameData.residualYaw != 0.0) {
        // Smooth-yaw receive. While rotating, voxels leave the single canvas
        // (per-axis scatter) and its remaining SDF/text content is stored at the
        // FULL visualYaw with view-frame depth — recover with the matching smooth
        // inverse. The cardinal recovery would return a residual-rotated world pos
        // here and sample the sun map off the true surface. Mirrors GLSL.
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
    // Riser-polarity flip: a flipped face's true outward normal is the
    // NEGATION of the slot-derived one — without it the normal bias pushes the
    // shadow sample INTO the caster and the riser reads fully sun-shadowed.
    // Negation commutes with the frame rotations above, so one flip covers all
    // three recovery branches; flip == 0 everywhere on non-rotated content.
    if (flip != 0) {
        normal = -normal;
    }

    float receiverFace = 0.0;
#if IR_SHAPE_RECEIVER
    if (!perAxis && selectedShapeBoxReceiver(pixel, size.x, float2(pixel),
            receiverFrame, receiverShapes, receiverOwners, receiverTiles,
            pos3D, normal)) {
        receiverFace = encodeReceiverFace(normal);
    }

#endif

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.metal).
    float factor = sunFrameData.shadowsEnabled == 0 ? 1.0 : worldSunShadowFactor(pos3D, normal, float(rawDepth), sunFrameData, sunDepthBuf);
    canvasSunShadow.write(float4(factor, 0.0, 0.0, receiverFace), uint2(pixel));
}
