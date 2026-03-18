#include <metal_stdlib>
using namespace metal;

struct FrameDataVoxelToTrixel {
    float2 frameCanvasOffset;
    int2 trixelCanvasOffsetZ1;
    int2 voxelRenderOptions;
    int2 voxelDispatchGrid;
    int voxelCount;
    int voxelDispatchPadding;
};

int2 pos3DtoPos2DIso(int3 position) {
    return int2(
        -position.x + position.y,
        -position.x - position.y + (2 * position.z)
    );
}

int pos3DtoDistance(int3 position) {
    return position.x + position.y + position.z;
}

float4 unpackColor(uint packedColor) {
    return float4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
}

constant int kXFace = 0;
constant int kYFace = 1;
constant int kZFace = 2;

int localIDToFace(uint2 localId) {
    if (localId.y == 0) {
        return kZFace;
    }
    if (localId.x == 1) {
        return kXFace;
    }
    return kYFace;
}

int3 faceMicroPositionFixed(int face, int3 voxelPositionFixed, int u, int v) {
    if (face == kXFace) {
        return int3(voxelPositionFixed.x, voxelPositionFixed.y + u, voxelPositionFixed.z + v);
    }
    if (face == kYFace) {
        return int3(voxelPositionFixed.x + u, voxelPositionFixed.y, voxelPositionFixed.z + v);
    }
    return int3(voxelPositionFixed.x + u, voxelPositionFixed.y + v, voxelPositionFixed.z);
}

float3 snapNearIntegerVoxelPosition(float3 voxelPosition) {
    const float3 voxelRounded = round(voxelPosition);
    const bool3 nearGrid = abs(voxelPosition - voxelRounded) <= float3(0.0001);
    return select(voxelPosition, voxelRounded, nearGrid);
}

float4 adjustColorForFace(float4 color, int face) {
    float brightness = 1.0;
    if (face == kYFace) {
        brightness = 0.75;
    }
    if (face == kZFace) {
        brightness = 1.25;
    }
    return float4(clamp(color.rgb * brightness, 0.0, 1.0), color.a);
}

void writeColorTap(
    texture2d<float, access::write> triangleCanvasColors,
    texture2d<int, access::write> triangleCanvasDistances,
    texture2d<uint, access::write> triangleCanvasEntityIds,
    device const atomic_int* distanceScratch,
    int2 canvasPixel,
    int voxelDistance,
    float4 voxelColor,
    ulong entityId
) {
    if (canvasPixel.x < 0 || canvasPixel.y < 0) {
        return;
    }
    if (canvasPixel.x >= int(triangleCanvasDistances.get_width()) ||
        canvasPixel.y >= int(triangleCanvasDistances.get_height())) {
        return;
    }

    const uint linearIndex =
        uint(canvasPixel.y) * triangleCanvasDistances.get_width() + uint(canvasPixel.x);
    const int canvasDistance =
        atomic_load_explicit(&distanceScratch[linearIndex], memory_order_relaxed);
    if (voxelDistance == canvasDistance) {
        triangleCanvasColors.write(voxelColor, uint2(canvasPixel));
        triangleCanvasDistances.write(int4(voxelDistance), uint2(canvasPixel));
        const uint4 packedEntityId = uint4(
            uint(entityId & 0xffffffffull),
            uint((entityId >> 32ull) & 0xffffffffull),
            0u,
            0u
        );
        triangleCanvasEntityIds.write(packedEntityId, uint2(canvasPixel));
    }
}

kernel void c_voxel_to_trixel_stage_2(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const uint* colors [[buffer(6)]],
    device const ulong* entityIds [[buffer(13)]],
    device const atomic_int* distanceScratch [[buffer(16)]],
    texture2d<float, access::write> triangleCanvasColors [[texture(0)]],
    texture2d<int, access::write> triangleCanvasDistances [[texture(1)]],
    texture2d<uint, access::write> triangleCanvasEntityIds [[texture(2)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint voxelIndex = groupId.x + groupId.y * uint(frameData.voxelDispatchGrid.x);
    if (voxelIndex >= uint(frameData.voxelCount)) {
        return;
    }
    const uint2 localId = localId3.xy;
    const float4 voxelPosition = positions[voxelIndex];
    float4 voxelColor = unpackColor(colors[voxelIndex]);
    if (voxelColor.a == 0.0) {
        return;
    }
    voxelColor = adjustColorForFace(voxelColor, localIDToFace(localId));

    if (frameData.voxelRenderOptions.x == 0) {
        const int3 voxelPositionInt = int3(
            round(voxelPosition.x),
            round(voxelPosition.y),
            round(voxelPosition.z)
        );
        const int voxelDistance = pos3DtoDistance(voxelPositionInt);
        const int2 canvasPixel =
            frameData.trixelCanvasOffsetZ1 +
            int2(floor(frameData.frameCanvasOffset.x), floor(frameData.frameCanvasOffset.y)) +
            int2(localId) +
            pos3DtoPos2DIso(voxelPositionInt);
        writeColorTap(
            triangleCanvasColors,
            triangleCanvasDistances,
            triangleCanvasEntityIds,
            distanceScratch,
            canvasPixel,
            voxelDistance,
            voxelColor,
            entityIds[voxelIndex]
        );
        return;
    }

    const int subdivisions = max(frameData.voxelRenderOptions.y, 1);
    const float3 voxelPositionAligned = snapNearIntegerVoxelPosition(voxelPosition.xyz);
    const int3 voxelPositionFixed = int3(round(voxelPositionAligned * float(subdivisions)));
    const int2 frameOffsetFixed =
        frameData.trixelCanvasOffsetZ1 +
        int2(floor(frameData.frameCanvasOffset * float(subdivisions)));
    const int2 localFaceOffsetFixed = int2(localId);
    const int face = localIDToFace(localId);

    for (int u = 0; u < subdivisions; ++u) {
        for (int v = 0; v < subdivisions; ++v) {
            const int3 microPositionFixed =
                faceMicroPositionFixed(face, voxelPositionFixed, u, v);
            const int depthBase =
                microPositionFixed.x + microPositionFixed.y + microPositionFixed.z;
            const int voxelDistance = depthBase * 4 + face;
            const int2 canvasPixel =
                frameOffsetFixed + localFaceOffsetFixed + pos3DtoPos2DIso(microPositionFixed);
            writeColorTap(
                triangleCanvasColors,
                triangleCanvasDistances,
                triangleCanvasEntityIds,
                distanceScratch,
                canvasPixel,
                voxelDistance,
                voxelColor,
                entityIds[voxelIndex]
            );
        }
    }
}
