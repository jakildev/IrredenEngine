#version 460 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO, shadows, flood-fill
// propagation) and modulates the canvas color in place.
//
// Skeleton: when `lightingEnabled` is 0, every thread is an early-return.
// Later lighting phases (T-010 occupancy grid, T-012 AO, T-013 shadows)
// will populate the inputs and set `lightingEnabled` to 1.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    // 0 while lighting phases are not yet wired up. Main-canvas pass sets
    // this to 1 once the 3D occupancy grid, AO texture, and shadow map are
    // bound. GUI canvases always see 0 so GUI-sourced pixels are not
    // modulated (see acceptance criterion 4 for T-011).
    uniform int lightingEnabled;
    uniform int _padding0;
    uniform int _padding1;
    uniform int _padding2;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;

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

    // Later phases reconstruct world position via isoPixelToPos3D(pixel,
    // distance), sample AO/shadow/flood-fill textures, and combine into a
    // single light scalar. With no lighting inputs bound the early-return
    // above keeps this a true no-op.
}
