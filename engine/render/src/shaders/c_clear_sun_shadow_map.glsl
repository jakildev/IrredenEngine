#version 450 core

// Resets the sun depth map to the lit-sentinel (0xFFFFFFFF) before the
// BAKE pass writes via atomicMin; otherwise stale texels persist as false
// shadows. Dispatched as a compute pass to avoid a 4 MB CPU upload.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_sun_projection.glsl"
#include "ir_sun_face_query_layout.glsl"
const int kTotalTexels = int(kSourceFaceFallbackOffset);

layout(std430, binding = 28) restrict writeonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int linearIdx = gid.y * kSunShadowMapDim + gid.x;
    if (linearIdx >= kTotalTexels) {
        return;
    }
    if (linearIdx == 0) sunDepthBuf[sourceFaceHeaderIndex(uint(sunDepthBuf.length()))] = 0u;
    if (uint(linearIdx) < kSourceFaceTileCount) sunDepthBuf[sourceFaceTileBase(uint(linearIdx))] = 0u;
    sunDepthBuf[linearIdx + kSourceFaceFallbackOffset] = 0xFFFFFFFFu;
    sunDepthBuf[linearIdx] = 0xFFFFFFFFu;
}
