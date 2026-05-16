#version 450 core

// Resets the sun depth map to the lit-sentinel (0xFFFFFFFF) before the
// BAKE pass writes via atomicMin; otherwise stale texels persist as false
// shadows. Dispatched as a compute pass to avoid a 4 MB CPU upload.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

const int kSunShadowMapDim = 1024;

layout(std430, binding = 28) restrict writeonly buffer SunShadowDepthMap {
    uint sunDepthBuf[];
};

void main() {
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    if (gid.x >= kSunShadowMapDim || gid.y >= kSunShadowMapDim) {
        return;
    }
    sunDepthBuf[gid.y * kSunShadowMapDim + gid.x] = 0xFFFFFFFFu;
}
