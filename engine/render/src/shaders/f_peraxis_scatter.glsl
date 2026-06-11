/*
 * Project: Irreden Engine
 * File: f_peraxis_scatter.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: May 2026
 * -----
 * Smooth camera Z-yaw — T3 (#1310) forward-scatter composite.
 * Writes the scattered face quad's color + depth into the shared framebuffer;
 * the GL_LESS depth test composites the three per-axis canvases per pixel.
 */

#version 450 core

flat in vec4 vColor;
flat in float vDepth;
flat in float vIsoDepth;
flat in int vDepthColorMode;
flat in float vDepthColorExtent;

out vec4 FragColor;

// HSV → RGB. Identical to hsvToRgb in c_shapes_to_trixel.glsl — voxel-scatter
// depth-color is bit-exact with the SDF twin when mode is on (#1697).
vec3 hsvToRgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    if (vColor.a < 0.1) {
        discard;
    }
    if (vDepthColorMode != 0) {
        float dColor = vDepthColorExtent;
        float denomC = max((4.0 / 3.0) * dColor, 1.0);
        float t = clamp((vIsoDepth + dColor) / denomC, 0.0, 1.0);
        FragColor = vec4(hsvToRgb(vec3(0.66 * t, 1.0, 1.0)), 1.0);
    } else {
        FragColor = vColor;
    }
    gl_FragDepth = vDepth;
}
