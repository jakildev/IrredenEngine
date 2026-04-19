#version 460 core

// Screen-space lighting application pass. Runs after all geometry has been
// rasterized to the trixel canvas (voxels, shapes, text) and before
// compositing. Samples world-space lighting data (AO currently; shadows
// and flood-fill propagation later) and modulates the canvas color in
// place.

layout(local_size_x = 16, local_size_y = 16, local_size_z = 1) in;

layout(std140, binding = 27) uniform FrameDataLightingToTrixel {
    // Main-canvas pass sets this to 1 once the AO texture is bound. GUI
    // canvases always see 0 so GUI-sourced pixels are not modulated.
    uniform int   lightingEnabled;
    // 1 activates LUT palette shading, which replaces the plain grayscale
    // AO multiplication with a luminance-indexed palette lookup driven by
    // the per-pixel AO value.
    uniform int   lutEnabled;
    // Reserved for future shadow-preview use; kept in the UBO for std140
    // layout stability. The shader currently uses AO.r as the LUT X-axis.
    uniform float debugLightLevel;
    uniform int   _padding2;
};

layout(rgba8, binding = 0) uniform image2D trixelColors;
layout(r32i, binding = 1) readonly uniform iimage2D trixelDistances;
layout(rgba8, binding = 2) readonly uniform image2D canvasAO;
layout(binding = 3) uniform sampler2D paletteLUT;

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
    const float ao  = imageLoad(canvasAO, pixel).r;
    const vec4  src = imageLoad(trixelColors, pixel);

    if (lutEnabled == 0) {
        imageStore(trixelColors, pixel, vec4(src.rgb * ao, src.a));
        return;
    }

    // LUT palette shading: AO drives the X axis (light level) and pixel
    // luminance selects the palette row so highlights and shadows get
    // distinct cel-shade colour casts. The LUT encodes both the brightness
    // curve and the shadow tint, so the plain AO multiply is skipped.
    const float luminance = dot(src.rgb, vec3(0.299, 0.587, 0.114));
    const vec4  lut       = texture(paletteLUT, vec2(ao, luminance));
    imageStore(trixelColors, pixel, vec4(src.rgb * lut.rgb, src.a));
}
