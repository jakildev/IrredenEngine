#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
#include "ir_per_axis_cell_dispatch.metal"
#include "ir_fog_common.metal"

// Mirrors shaders/c_fog_to_trixel.glsl: one kernel, main-canvas and per-axis
// routes, shaded by the shared reveal model (ir_fog_common.metal). A fully
// revealed pixel is never read or rewritten.


static float3 fogPixelToWorld(
    int2 pixel,
    int encoded,
    int faceId,
    int2 size,
    int cardinalDepth,
    constant FrameDataVoxelToTrixel& frameData
) {
    if (frameData.perAxisRoute != 0) {
        return perAxisCellToWorld3DSubCell(
            pixel, encoded, faceId, frameData.perAxisStoreFrame
        );
    }
    if (frameData.residualYaw != 0.0f) {
        return trixelCanvasPixelToWorld3DSmoothYaw(
            pixel,
            decodeDepthSingle(encoded),
            frameData.trixelCanvasOffsetZ1,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions,
            frameData.visualYaw
        );
    }
    return trixelCanvasPixelToWorld3D(
        pixel,
        cardinalDepth,
        frameData.trixelCanvasOffsetZ1,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions,
        frameData.rasterYaw
    );
}

kernel void c_fog_to_trixel(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    texture2d<float, access::read_write> trixelColors [[texture(0)]],
    texture2d<int, access::read> trixelDistances [[texture(1)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    // Read only for the fog BODY carrier (decodeFogBody / decodeFogBodyFactor)
    // and the analytic-surface carrier bit.
    texture2d<uint, access::read> triangleCanvasEntityIds [[texture(3)]],
    // Line-of-sight column field (ir_fog_los). Slot 4 is lighting's
    // sun-shadow input too; lighting rebinds it inside its own tick.
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
        const uint idx = perAxisCellInvocationIndex(
            groupId.x, groupId.y, numGroups.x, localIndex
        );
        if (idx >= cellDrawArgs[kDispatchArgsBaseUint + 3u]) {
            return;
        }
        pixel = perAxisCellPixel(compactedCells[idx], size.x);
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

    const uint2 rawId = triangleCanvasEntityIds.read(uint2(pixel)).xy;
    if (decodeFogBody(rawId)) {
        const float bodyState = float(decodeFogBodyFactor(rawId)) / 255.0f;
        if (bodyState < 1.0f) {
            trixelColors.write(
                fogApplyBody(bodyState, trixelColors.read(uint2(pixel)), fogObservers),
                uint2(pixel)
            );
        }
        return;
    }

    const int slot = decodeSlot(encoded);
    const int faceId =
        frameData.visibleFaceIds[slot] ^ decodeFlipRoute(encoded, frameData.perAxisRoute);
    const int scale = effectiveTrixelSubdivisionScale(frameData.voxelRenderOptions);
    const bool analyticCardinal = frameData.perAxisRoute == 0 &&
        frameData.residualYaw == 0.0f && scale > 1 && decodeAnalyticSurface(rawId);
    const int rasterDepth = decodeDepthSingle(encoded);
    const int cardinalDepth = analyticCardinal
        ? rasterDepth - cardinalRasterLatticeDepthOffset(scale)
        : rasterDepth;
    const float3 pos3D = fogPixelToWorld(pixel, encoded, faceId, size, cardinalDepth, frameData);
    float aaFloor = 0.0f;
    float3 losSample = pos3D;
    if (fogObservers.visionCircleCount > 0) {
        const float3 neighbor = fogPixelToWorld(
            pixel + int2(1, 0), encoded, faceId, size, cardinalDepth, frameData);
        aaFloor = length(neighbor.xy - pos3D.xy);
        if (fogObservers.losSourceMask != 0) {
            int losRoute = kFogLosRouteAnalytic;
            if (frameData.perAxisRoute != 0) {
                losRoute = kFogLosRoutePerAxis;
            } else if (frameData.residualYaw == 0.0f && !decodeAnalyticSurface(rawId)) {
                losRoute = kFogLosRouteCardinal;
            }
            losSample = fogLosCanonicalSample(
                pos3D,
                faceId,
                losRoute,
                scale
            );
        }
    }

    const FogReveal reveal = fogRevealSample(
        pos3D,
        losSample,
        aaFloor,
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
