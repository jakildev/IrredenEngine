#version 450 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_iso_common.glsl"
#include "ir_per_axis_lighting.glsl"
#define IR_FOG_LOS_BINDING 4
#include "ir_fog_common.glsl"

layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    vec2 frameCanvasOffset;
    ivec2 _trixelCanvasOffsetZ1;
    ivec2 voxelRenderOptions;
    ivec2 _voxelDispatchGrid;
    int _voxelCount;
    int _perAxisRoute;
    ivec2 canvasSizePixels;
    ivec2 _cullIsoMin;
    ivec2 _cullIsoMax;
    float _visualYaw;
    float _rasterYaw;
    float _residualYaw;
    float _isDetachedCanvas;
    vec4 _faceDeform[3];
    ivec4 visibleFaceIds;
    vec4 _voxelDepthAxis;
    vec4 _detachedWorldReceive;
    ivec4 _visibleIsoBounds;
    int _resolveMode;
    int _occlusionCullMipCount;
    int _feederSubCap;
    int _feederPassTailBase;
    ivec4 overflowScratchLayout;
};

layout(std430, binding = 8) buffer OverflowFogScratch {
    uint overflowScratch[];
};

void main() {
    const uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    const uint gid = workGroupIndex * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
    const uint entryCount = overflowScratch[uint(overflowScratchLayout.y) + 1u];
    if (gid >= entryCount) {
        return;
    }

    const uint entryBase = uint(overflowScratchLayout.z) + gid * 3u;
    const uint packedCell = overflowScratch[entryBase + 0u];
    const uint colorPacked = overflowScratch[entryBase + 1u];
    const int encoded = int(overflowScratch[entryBase + 2u]);
    const ivec2 cell = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int faceId =
        visibleFaceIds[decodeSlot(encoded)] ^ decodeFlipPerAxis(encoded);
    const vec3 pos3D = perAxisCellToWorld3DSubCell(
        cell,
        encoded,
        faceId,
        canvasSizePixels,
        frameCanvasOffset,
        voxelRenderOptions
    );
    const uint fogClassByte = colorPacked >> 24u;
    const bool fogWholeBody = fogClassByte == 254u;
    float aaFloor = 0.0;
    if (visionCircleCount > 0) {
        const vec3 neighbor = perAxisCellToWorld3DSubCell(
            cell + ivec2(1, 0),
            encoded,
            faceId,
            canvasSizePixels,
            frameCanvasOffset,
            voxelRenderOptions
        );
        aaFloor = length(neighbor.xy - pos3D.xy);
    }

    const FogReveal reveal =
        fogRevealSample(pos3D, aaFloor, fogWholeBody, fogLosVoxelSample(pos3D, faceId));
    if (reveal.state >= 1.0) {
        return;
    }
    const vec4 sourceColor = unpackColor(colorPacked);
    overflowScratch[entryBase + 1u] =
        packColor(fogApplyReveal(reveal, faceId >> 1, sourceColor));
}
