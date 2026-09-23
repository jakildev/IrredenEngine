
// Per-pixel directional sun shadow compute with cascaded shadow maps.
// For each rasterized surface pixel, reconstructs the voxel-space position,
// selects the appropriate cascade based on iso depth, projects into the
// cascade's sun-aligned depth map, and compares against the nearest stored
// blocker. Blends between cascades at the split boundary.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
#if IR_SHAPE_RECEIVER
#include "ir_sdf_common.glsl"
#include "ir_shape_data.glsl"
#include "ir_shape_receiver.glsl"
#endif
// Shared caster/receiver sun-space projection.
#include "ir_sun_projection.glsl"
// FrameDataSun UBO (29), sun-depth SSBO (28), the cascade PCF sampler, and the
// world-space worldSunShadowFactor() lookup — shared with c_lighting_to_trixel's
// detached world-receive path.
#include "ir_sun_shadow_sample.glsl"

const int kEmptyDistanceEncoded = 65535;

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    // Smooth-camera-Z-yaw per-axis route selector (mirrors
    // FrameDataVoxelToCanvas::perAxisRoute_). 0 = single canvas; nonzero = a
    // per-axis canvas bake, reconstruct world-pos face-locally.
    uniform int perAxisRoute;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
    uniform float visualYaw;
    uniform float rasterYaw;
    uniform float residualYaw;
    uniform float _yawPadding;            // isDetachedCanvas in the full UBO
    uniform vec4 _faceDeformPadding[3];   // faceDeform[3] in the full UBO
    // Per-slot world FaceId (0..5); used only on the per-axis path.
    uniform ivec4 visibleFaceIds;
};

layout(r32i, binding = 0) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 1) writeonly uniform image2D canvasSunShadow;

// On the per-axis path this stage is dispatched indirectly over only each axis's
// OCCUPIED cells (compacted by the STAGE_1 per-axis pre-pass) instead of sweeping
// the full grid. compactedCells holds the occupied linear cell indices;
// cellDrawArgs carries the visibleCount at [kDispatchArgsBaseUint + 3]. Unused on
// the single-canvas 2D path (perAxisRoute == 0).
layout(std430, binding = 25) readonly buffer PerAxisCellCompacted {
    uint compactedCells[];
};
layout(std430, binding = 26) readonly buffer PerAxisCellIndirect {
    uint cellDrawArgs[];
};
const uint kDispatchArgsBaseUint = 8u;      // kPerAxisCellDispatchArgsOffsetBytes / 4
const uint kPerAxisCellComputeTile = 256u;  // kPerAxisCellComputeTile (16×16 threads)

#if IR_SHAPE_RECEIVER
layout(std140, binding = 23) uniform ShapeReceiverFrame {
    ShapeProjectionData receiverFrame;
};
layout(std430, binding = 20) readonly buffer ReceiverShapes { ShapeDescriptor receiverShapes[]; };
layout(std430, binding = 22) readonly buffer ReceiverOwners { uint receiverOwners[]; };
layout(std430, binding = 30) readonly buffer ReceiverTiles { ShapeTileDescriptor receiverTiles[]; };

#endif

void main() {
    const ivec2 size = imageSize(trixelDistances);
    ivec2 pixel;
    if (perAxisRoute != 0) {
        // 2-D-folded indirect dispatch — recover the flat group index
        // (matches c_per_axis_cell_finalize's capped grid + c_voxel_visibility_compact).
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
    // Per-axis canvas uses INT_MAX as the empty sentinel; single-canvas uses 65535.
    if (encoded >= (perAxisRoute != 0 ? 0x7FFFFFFF : kEmptyDistanceEncoded)) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }
    if (shadowsEnabled == 0) {
        imageStore(canvasSunShadow, pixel, vec4(1.0, 0.0, 0.0, 0.0));
        return;
    }

    // Shared decode helpers (ir_iso_common) own both encodings' bit layouts
    // (per-axis / single-canvas, flip carrier).
    int rawDepth = decodeDepthRoute(encoded, perAxisRoute);
    int face = decodeSlot(encoded);
    int flip = decodeFlipRoute(encoded, perAxisRoute);
    int cardinalIndex = rasterYawCardinalIndex(rasterYaw);

    // Smooth camera Z-yaw: a per-axis canvas stores the world frame face-locally,
    // so recover world-pos via isoPixelToPos3D and read the world-frame outward
    // normal directly (no cardinal rotation — the store already wrote world
    // coords). The single canvas uses its cardinal-snap reconstruction +
    // R_z(-rasterYaw) normal rotation (per-axis canvases are only allocated while
    // rotating).
    bool perAxis = perAxisRoute != 0;
    vec3 pos3D;
    vec3 normal;
    if (perAxis) {
        int faceId = visibleFaceIds[face];
        // Sub-cell recovery — the receiver must sample the sun map at the
        // drawn surface, not the lattice cell origin.
        pos3D = perAxisCellToWorld3DSubCell(pixel, encoded, faceId, size, frameCanvasOffset, voxelRenderOptions);
        normal = faceOutwardNormal6(faceId);
    } else if (residualYaw != 0.0) {
        // Smooth-yaw receive. While rotating, voxels leave the single canvas
        // (per-axis scatter) and its remaining SDF/text content is stored at the
        // FULL visualYaw with view-frame depth — recover with the matching smooth
        // inverse. The cardinal recovery would return a residual-rotated world pos
        // here and sample the sun map off the true surface.
        pos3D = trixelCanvasPixelToWorld3DSmoothYaw(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, visualYaw
        );
        normal = rotateYawZInv(faceOutwardNormal(face), visualYaw);
    } else {
        pos3D = trixelCanvasPixelToWorld3D(
            pixel, rawDepth, trixelCanvasOffsetZ1, frameCanvasOffset, voxelRenderOptions, rasterYaw
        );
        // Rotate raster-frame face normal to world frame so normal bias and slope
        // bias are applied in the correct world-space direction at non-zero camera
        // yaw. No-op at yaw=0 (cardinalIndex=0). Matches the AO shader pattern.
        normal = rotateCardinalZInv(faceOutwardNormal(face), cardinalIndex);
    }
    // Riser-polarity flip: a flipped face's true outward normal is the
    // NEGATION of the slot-derived one — without it the normal bias pushes the
    // shadow sample INTO the caster and the riser reads fully sun-shadowed.
    // Negation commutes with the per-branch frame rotations, so one flip covers
    // all three recovery branches; flip == 0 everywhere on non-rotated content.
    if (flip != 0) {
        normal = -normal;
    }

#if IR_SHAPE_RECEIVER
    if (!perAxis && receiverFrame.shapeCount > 0) {
        uint key = receiverOwners[uint(pixel.y * size.x + pixel.x)];
        if (key != 0xffffffffu) {
            int shapeIndex = receiverTiles[key / kShapeSamplesPerTile].shapeIndex;
            vec3 exactPosition, exactNormal;
            if (shapeBoxReceiver(receiverShapes[shapeIndex], receiverFrame,
                                 vec2(pixel), exactPosition, exactNormal)) {
                pos3D = exactPosition;
                normal = exactNormal;
            }
        }
    }

#endif

    // World iso depth picks the cascade; rawDepth IS the world iso depth for the
    // world canvas this pass runs on. The cascade PCF lookup is shared with the
    // detached world-receive path (ir_sun_shadow_sample.glsl).
    float factor = worldSunShadowFactor(pos3D, normal, float(rawDepth));
    imageStore(canvasSunShadow, pixel, vec4(factor, 0.0, 0.0, 0.0));
}
