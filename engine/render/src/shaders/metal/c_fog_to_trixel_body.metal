#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
#include "ir_fog_common.metal"

// Mirrors shaders/c_fog_to_trixel_body.glsl: one kernel body, main-canvas and
// per-axis routes, shaded by the shared reveal model (ir_fog_common.metal). A
// fully revealed pixel is never read or rewritten. Include-FRAGMENT: the
// wrapper defines IR_FOG_KERNEL_NAME and IR_FOG_LOS_SMOOTH (the smooth
// line-of-sight variant, main canvas only).

constant uint kDispatchArgsBaseUint = 8u;
constant uint kPerAxisCellComputeTile = 256u;

static float3 fogPixelToWorld(
    int2 pixel,
    int encoded,
    int faceId,
    int2 size,
    constant FrameDataVoxelToTrixel& frameData
) {
    if (frameData.perAxisRoute != 0) {
        return perAxisCellToWorld3DSubCell(
            pixel,
            encoded,
            faceId,
            size,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        );
    }
    return trixelCanvasPixelToWorld3D(
        pixel,
        decodeDepthSingle(encoded),
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );
}

#if IR_FOG_LOS_SMOOTH
// The smooth line-of-sight sample of a single-canvas vertical face — mirror of
// the GLSL twin.
static FogLosSample fogLosPixelFaceSample(
    int2 pixel,
    int encoded,
    int worldFaceId,
    constant FrameDataVoxelToTrixel& frameData
) {
    const int cardinalIndex = rasterYawCardinalIndex(frameData.rasterYaw);
    const int3 voxel = fogLosFaceVoxel(
        trixelCanvasPixelToIsoRel(
            pixel,
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        ),
        decodeDepthSingle(encoded),
        rotateFaceIdCardinalZ(worldFaceId, cardinalIndex),
        effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions),
        frameData.voxelRenderOptions.x != 0,
        cardinalIndex
    );
    return fogLosFaceSample(voxel, worldFaceId);
}
#endif

kernel void IR_FOG_KERNEL_NAME(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    // Read only for the fog whole-body carrier bit (decodeFogWholeBody).
    texture2d<uint, access::read> triangleCanvasEntityIds [[texture(3)]],
    // Line-of-sight horizons (ir_fog_los). Slot 4 is lighting's sun-shadow
    // input too; lighting rebinds it inside its own tick.
    texture2d<float, access::read> fogLineOfSight [[texture(4)]],
    // buffer(27) ALIASES kBufferIndex_FrameDataLightingToTrixel — fog runs
    // after lighting is done with the slot.
    constant FogObserverData& fogObservers [[buffer(27)]],
    const device uint* compactedCells [[buffer(25)]],
    const device uint* cellDrawArgs [[buffer(26)]],
    uint3 globalId [[thread_position_in_grid]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint localIndex [[thread_index_in_threadgroup]],
    uint3 numGroups [[threadgroups_per_grid]]
) {
    const int2 size = int2(
        int(trixelColors.get_width()),
        int(trixelColors.get_height())
    );
    int2 pixel;
    if (frameData.perAxisRoute != 0) {
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

    const int encoded = trixelDistances.read(uint2(pixel)).x;
    if (encoded >= (frameData.perAxisRoute != 0 ? 0x7FFFFFFF : 65535)) {
        return;
    }

    const int slot = decodeSlot(encoded);
    const int faceId =
        frameData.visibleFaceIds[slot] ^ decodeFlipRoute(encoded, frameData.perAxisRoute);
    const float3 pos3D = fogPixelToWorld(pixel, encoded, faceId, size, frameData);
    float aaFloor = 0.0f;
    bool fogWholeBody = false;
    if (fogObservers.visionCircleCount > 0) {
        const float3 neighbor =
            fogPixelToWorld(pixel + int2(1, 0), encoded, faceId, size, frameData);
        aaFloor = length(neighbor.xy - pos3D.xy);
        fogWholeBody = decodeFogWholeBody(triangleCanvasEntityIds.read(uint2(pixel)).xy);
    }

    FogLosSample losSample = fogLosSurfaceSample(pos3D);
#if IR_FOG_LOS_SMOOTH
    if ((faceId >> 1) != kZFace && fogLosSmoothSampleNeeded(fogWholeBody, fogObservers)) {
        losSample = fogLosPixelFaceSample(pixel, encoded, faceId, frameData);
    }
#endif

    const FogReveal reveal = fogRevealSample(
        pos3D,
        aaFloor,
        fogWholeBody,
        losSample,
        fogObservers,
        canvasFogOfWar,
        fogLineOfSight
    );
    if (reveal.state >= 1.0f) {
        return;
    }
    const float4 sourceColor = trixelColors.read(uint2(pixel));
    trixelColors.write(
        fogApplyReveal(reveal, faceId >> 1, sourceColor, fogObservers),
        uint2(pixel)
    );
}
