#include "ir_iso_common.metal"
#include "ir_per_axis_lighting.metal"
#include "ir_fog_common.metal"

kernel void c_fog_overflow_faces(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    constant FogObserverData& fogObservers [[buffer(27)]],
    device uint* overflowScratch [[buffer(8)]],
    texture2d<float, access::read> canvasFogOfWar [[texture(2)]],
    texture2d<float, access::read> fogLineOfSight [[texture(4)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint gid = workGroupIndex * 64u + localId.x;
    const int4 layout = frameData.overflowScratchLayout;
    const uint entryCount = overflowScratch[uint(layout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }

    const uint entryBase = uint(layout.z) + gid * 3u;
    const uint packedCell = overflowScratch[entryBase + 0u];
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int encoded = int(overflowScratch[entryBase + 2u]);
    const int2 cell = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int faceId = frameData.visibleFaceIds[decodeSlot(encoded)] ^
        decodeFlipPerAxis(encoded);
    const float3 pos3D = perAxisCellToWorld3DSubCell(
        cell,
        encoded,
        faceId,
        frameData.canvasSizePixels,
        frameData.frameCanvasOffset,
        frameData.voxelRenderOptions
    );
    const uint fogClassByte = colorPacked >> 24u;
    const bool fogWholeBody = fogClassByte == 254u;
    float aaFloor = 0.0f;
    if (fogObservers.visionCircleCount > 0) {
        const float3 neighbor = perAxisCellToWorld3DSubCell(
            cell + int2(1, 0),
            encoded,
            faceId,
            frameData.canvasSizePixels,
            frameData.frameCanvasOffset,
            frameData.voxelRenderOptions
        );
        aaFloor = length(neighbor.xy - pos3D.xy);
    }

    const FogReveal reveal = fogRevealSample(
        pos3D,
        aaFloor,
        fogWholeBody,
        fogLosVoxelSample(pos3D, faceId),
        fogObservers,
        canvasFogOfWar,
        fogLineOfSight
    );
    if (reveal.state >= 1.0f) {
        return;
    }
    const float4 sourceColor = unpackColor(colorPacked);
    overflowScratch[entryBase + 1u] =
        packColor(fogApplyReveal(reveal, faceId >> 1, sourceColor, fogObservers));
}
