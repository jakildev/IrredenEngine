#version 460 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO currently; shadows
// and flood-fill propagation later) and modulates the canvas color in
// place.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    uniform int lightingEnabled;
    uniform int _padding0;
    uniform int _padding1;
    uniform int _padding2;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasAO;

void main() {
    if (lightingEnabled == 0) {
        return;
    }

    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(trixelColors);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    // Empty/background pixels carry kTrixelDistanceMaxDistance (65535).
    // Nothing rasterized here, so nothing to light.
    const int distance = imageLoad(trixelDistances, pixel).x;
    if (distance >= 65535) {
        return;
    }

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    const float ao = imageLoad(canvasAO, pixel).r;
    vec4 color = imageLoad(trixelColors, pixel);
    color.rgb *= ao;
    imageStore(trixelColors, pixel, color);
}
