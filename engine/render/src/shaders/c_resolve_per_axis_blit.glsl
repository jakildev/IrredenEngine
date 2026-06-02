#version 450 core

// Smooth camera Z-yaw — per-axis sun-shadow resolve, blit pass (#1435).
//
// Materializes the scatter pass's scratch SSBO (main-canvas-sized front-most
// iso-depth) into the resolve R32I TEXTURE that BAKE_SUN_SHADOW_MAP reads
// through its cardinal path, then resets each scratch slot to the empty
// sentinel so the next frame starts clean WITHOUT a separate clear dispatch.
// Every screen texel is written every frame, so the texture never carries
// stale per-axis depth.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

const int kEmptyDistanceEncoded = 65535;

layout(std430, binding = 28) restrict buffer PerAxisResolveScratch {
    int resolveScratch[];
};

layout(r32i, binding = 1) writeonly uniform iimage2D resolveDepth;

void main() {
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(resolveDepth);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }
    const int idx = pixel.y * size.x + pixel.x;
    imageStore(resolveDepth, pixel, ivec4(resolveScratch[idx], 0, 0, 0));
    resolveScratch[idx] = kEmptyDistanceEncoded;
}
