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
    const uint boundWords = uint(sunDepthBuf.length());
    const uint capacity = sourceFaceCapacity(boundWords);
    // The capacity word beside the header belongs to the allocator and survives the clear.
    if (linearIdx == 0) sunDepthBuf[sourceFaceHeaderIndex(boundWords)] = 0u;
    if (linearIdx == 0) sunDepthBuf[sourceFaceSpillCounterIndex(capacity)] = 0u;
    if (linearIdx == 0) sunDepthBuf[sourceFaceHierarchyNeededIndex(capacity)] = 0u;
    if (linearIdx == 0) sunDepthBuf[sourceFaceHierarchyReadyIndex(capacity)] = 0u;
    if (uint(linearIdx) < kSourceFaceTileCount) {
        sunDepthBuf[sourceFaceTileBase(uint(linearIdx))] = 0u;
        sunDepthBuf[sourceFaceSpillHeadIndex(capacity, uint(linearIdx))] = 0xFFFFFFFFu;
    }
    sunDepthBuf[linearIdx + kSourceFaceFallbackOffset] = 0xFFFFFFFFu;
    sunDepthBuf[linearIdx] = 0xFFFFFFFFu;
    // Slab mins start empty; maxes start at zero, below every packed depth.
    for (uint slot = 0u; slot < kSunDepthSlabSlots; ++slot) {
        sunDepthBuf[sunDepthSlabMinIndex(linearIdx, slot)] = 0xFFFFFFFFu;
        sunDepthBuf[sunDepthSlabMaxIndex(linearIdx, slot)] = 0u;
    }
}
