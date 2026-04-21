#version 460 core

// Screen-space fog-of-war pass. Runs after LIGHTING_TO_TRIXEL and before
// TRIXEL_TO_TRIXEL. For each rasterized pixel, recovers the source voxel's
// world (x,y) column from the encoded distance + iso pixel coords, samples
// the 2D fog visibility texture at that column, and modulates the canvas
// color according to the cell's fog state:
//   visible    (.r ~= 1.0) — pass through
//   explored   (.r ~= 0.5) — desaturate + darken (fog-of-war "memory")
//   unexplored (.r == 0.0) — black
//
// Background pixels (no rasterized geometry) keep their cleared color so
// the engine's default clear paints around the visible region — adding a
// world-grounded fog tint to background pixels would require sampling the
// fog along the camera ray, which v1 defers.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

// Mirrors C_CanvasFogOfWar in
// engine/prefabs/irreden/render/components/component_canvas_fog_of_war.hpp.
// World-space fog grid is centered on origin with half-extent = size / 2.
const int kFogOfWarSize = 256;
const int kFogOfWarHalfExtent = 128;

// Same threshold LIGHTING_TO_TRIXEL uses for "empty pixel" — encoded
// distances >= 65535 mean the clear value was never overwritten.
const int kEmptyDistanceEncoded = 65535;

// State thresholds matching kFogStateExplored=128, kFogStateVisible=255.
// Compare in normalized .r space (128/255 ≈ 0.50, 192/255 ≈ 0.75).
const float kFogVisibleThreshold = 0.75;
const float kFogExploredThreshold = 0.25;

// Mirrors FrameDataVoxelToTrixel; we only need frameCanvasOffset,
// trixelCanvasOffsetZ1, and voxelRenderOptions for the pixel→pos3D
// reconstruction, but std140 layout requires the full block.
layout(std140, binding = 7) uniform FrameDataVoxelToTrixel {
    uniform vec2 frameCanvasOffset;
    uniform ivec2 trixelCanvasOffsetZ1;
    uniform ivec2 voxelRenderOptions;
    uniform ivec2 voxelDispatchGrid;
    uniform int voxelCount;
    uniform int _voxelDispatchPadding;
    uniform ivec2 canvasSizePixels;
    uniform ivec2 cullIsoMin;
    uniform ivec2 cullIsoMax;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasFogOfWar;

void main() {
    const ivec2 pixel = ivec2(gl_GlobalInvocationID.xy);
    const ivec2 size = imageSize(trixelColors);
    if (pixel.x >= size.x || pixel.y >= size.y) {
        return;
    }

    // Background pixels: no geometry, no associated world cell — leave
    // alone. (See header comment for the deferred camera-ray approach.)
    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= kEmptyDistanceEncoded) {
        return;
    }

    // Recover world-space pos3D from the iso pixel + raw depth, applying
    // the same subdivision-aware scaling that c_compute_voxel_ao.glsl
    // uses. The three pos3D-recovery shaders (AO, sun shadow, fog) must
    // stay in lockstep with the stage-2 encoding.
    const int rawDepth = encoded >> 2;
    const ivec2 isoRel =
        pixel - trixelCanvasOffsetZ1 - ivec2(floor(frameCanvasOffset));
    const int subdivisions = max(voxelRenderOptions.y, 1);
    vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
    if (voxelRenderOptions.x != 0) {
        pos3D /= float(subdivisions);
    }
    const ivec3 surfaceVoxel = ivec3(round(pos3D));

    // Iso convention: X-Y is the floor plane, +Z is the downward height
    // axis, so the fog grid lookup is (x, y) with half-extent offset.
    const ivec2 fogCell = ivec2(
        surfaceVoxel.x + kFogOfWarHalfExtent,
        surfaceVoxel.y + kFogOfWarHalfExtent
    );
    const ivec2 fogSize = imageSize(canvasFogOfWar);
    if (fogCell.x < 0 || fogCell.x >= fogSize.x ||
        fogCell.y < 0 || fogCell.y >= fogSize.y) {
        // Out-of-range columns are treated as visible. Matches the
        // image-binding wrap-mode invariant documented in the component
        // header — imageLoad has no sampler wrap-mode behavior, so the
        // bounds check is load-bearing.
        return;
    }

    const float state = imageLoad(canvasFogOfWar, fogCell).r;
    if (state >= kFogVisibleThreshold) {
        return;
    }

    const vec4 src = imageLoad(trixelColors, pixel);
    if (state >= kFogExploredThreshold) {
        // "Memory" pass: desaturate to luminance, then darken. Keeps
        // shape silhouettes visible so the player remembers what was
        // there without confusing it with what is *currently* there.
        const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
        imageStore(
            trixelColors,
            pixel,
            vec4(vec3(luminance) * 0.4, src.a)
        );
        return;
    }

    // Unexplored: hard-black. Alpha preserved so any text/overlay
    // antialiasing still composites cleanly.
    imageStore(trixelColors, pixel, vec4(0.0, 0.0, 0.0, src.a));
}
