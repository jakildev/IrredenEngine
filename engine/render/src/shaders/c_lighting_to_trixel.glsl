#version 460 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO, directional sun
// shadow, flood-fill light volume) and modulates the canvas color in
// place.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

#include "ir_iso_common.glsl"

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    // Main-canvas pass sets this to 1 once the AO texture is bound. GUI
    // canvases always see 0 so GUI-sourced pixels are not modulated.
    uniform int   lightingEnabled;
    // 1 activates LUT palette shading, which replaces the plain grayscale
    // AO multiplication with a luminance-indexed palette lookup driven by
    // the per-pixel AO value.
    uniform int   lutEnabled;
    // 1 activates flood-fill light-volume sampling: per-pixel world
    // voxel position recovered from the distance texture, sampled in the
    // bound 3D light volume, and additively combined with the AO base.
    uniform int   lightVolumeEnabled;
    uniform float debugLightLevel;
};

// Mirrors the FrameDataVoxelToTrixel UBO used by VOXEL_TO_TRIXEL stages
// and COMPUTE_VOXEL_AO. We only need a subset of fields here (canvas
// offset + subdivision toggles) but the full layout must match for
// std140 binding compatibility.
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
layout(rgba8, binding = 2) readonly uniform image2D canvasAO;
layout(binding = 3) uniform sampler2D paletteLUT;
// canvasSunShadow sits at image unit 4. The Metal backend flattens
// texture_ and imageTexture_ tables into a shared setTexture slot space,
// so it cannot collide with paletteLUT at unit 3 or lightVolume at unit
// 5 — keep the unit numbers in lockstep across GLSL and MSL.
layout(rgba8, binding = 4) readonly uniform image2D canvasSunShadow;
layout(binding = 5) uniform sampler3D lightVolume;

// Mirror of `kLightVolumeSize` in component_canvas_light_volume.hpp.
// The volume covers world voxels in [-half, half) with one texel per
// voxel; sample coords are `(worldVoxel + half + 0.5) / size` to land
// at texel centers.
const float kLightVolumeSize = 128.0;
const float kLightVolumeHalfExtent = 64.0;

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
    const int encoded = imageLoad(trixelDistances, pixel).x;
    if (encoded >= 65535) {
        return;
    }

    // Alpha is preserved so text/overlay antialiasing composites unchanged.
    const float ao     = imageLoad(canvasAO, pixel).r;
    // Shadow factor is 1.0 for lit pixels and kShadowDarken (0.45) for
    // pixels whose sun ray hit an occluder in COMPUTE_SUN_SHADOW.
    const float shadow = imageLoad(canvasSunShadow, pixel).r;
    const vec4  src    = imageLoad(trixelColors, pixel);

    vec3 baseRgb;
    if (lutEnabled == 0) {
        baseRgb = src.rgb * ao * shadow;
    } else {
        // LUT palette shading: AO drives the X axis (light level) and pixel
        // luminance selects the palette row so highlights and shadows get
        // distinct cel-shade colour casts. Shadow darkening is applied after
        // the LUT lookup so palette shading and directional shadows compose
        // without needing a 3D LUT.
        const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
        const vec4  lut       = texture(paletteLUT, vec2(ao, luminance));
        baseRgb = src.rgb * lut.rgb * shadow;
    }

    if (lightVolumeEnabled != 0) {
        // Recover the world voxel position of this pixel from the encoded
        // depth + iso offset, mirroring the math in c_compute_voxel_ao.glsl.
        const int rawDepth = encoded >> 2;
        const ivec2 isoRel =
            pixel - trixelCanvasOffsetZ1 - ivec2(floor(frameCanvasOffset));
        const int subdivisions = max(voxelRenderOptions.y, 1);
        vec3 pos3D = isoPixelToPos3D(isoRel.x, isoRel.y, float(rawDepth));
        if (voxelRenderOptions.x != 0) {
            pos3D /= float(subdivisions);
        }

        // Sample the light volume at the surface voxel. CLAMP_TO_EDGE
        // means out-of-volume samples read zero light (the border texels
        // were cleared during BFS staging).
        const vec3 sampleCoord =
            (pos3D + vec3(kLightVolumeHalfExtent) + vec3(0.5)) /
            vec3(kLightVolumeSize);
        const vec3 light = texture(lightVolume, sampleCoord).rgb;
        baseRgb = baseRgb + src.rgb * light;
    }

    imageStore(trixelColors, pixel, vec4(baseRgb, src.a));
}
