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

int pos3DtoDistance(int3 position) {
    return position.x + position.y + position.z;
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

int2 pos3DtoPos2DIso(int3 position) {
    return int2(
        -position.x + position.y,
        -position.x - position.y + (2 * position.z)
    );
}

float4 unpackColor(uint packedColor) {
    return float4(
        float(packedColor & 0xFFu) / 255.0,
        float((packedColor >> 8) & 0xFFu) / 255.0,
        float((packedColor >> 16) & 0xFFu) / 255.0,
        float((packedColor >> 24) & 0xFFu) / 255.0
    );
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

void writeDistanceTap(
    texture2d<int, access::read> triangleCanvasDistances,
    device atomic_int* distanceScratch,
    int2 canvasPixel,
    int voxelDistance
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
    atomic_fetch_min_explicit(&distanceScratch[linearIndex], voxelDistance, memory_order_relaxed);
}

kernel void c_voxel_to_trixel_stage_1(
    constant FrameDataVoxelToTrixel& frameData [[buffer(7)]],
    device const float4* positions [[buffer(5)]],
    device const uint* colors [[buffer(6)]],
    device atomic_int* distanceScratch [[buffer(16)]],
    texture2d<int, access::read> triangleCanvasDistances [[texture(1)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 localId3 [[thread_position_in_threadgroup]]
) {
    const uint voxelIndex = groupId.x + groupId.y * uint(frameData.voxelDispatchGrid.x);
    if (voxelIndex >= uint(frameData.voxelCount)) {
        return;
    }
    const uint2 localId = localId3.xy;
    const float4 color = unpackColor(colors[voxelIndex]);
    if (color.a == 0.0) {
        return;
    }

    const float4 voxelPosition = positions[voxelIndex];
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
        writeDistanceTap(triangleCanvasDistances, distanceScratch, canvasPixel, voxelDistance);
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
            writeDistanceTap(triangleCanvasDistances, distanceScratch, canvasPixel, voxelDistance);
        }
    }
}
