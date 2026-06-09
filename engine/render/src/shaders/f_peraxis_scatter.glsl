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

flat   in vec4  vColor;
smooth in float vYawedDepth;  // continuous yawed iso depth across the face (#1457)
flat   in float vDepthBias;   // slot + distanceOffset - kMin
flat   in float vDepthScale;  // 1 / (kMax - kMin)

out vec4 FragColor;

void main() {
    if (vColor.a < 0.1) {
        discard;
    }
    FragColor = vColor;
    // Per-fragment composite depth (#1457): round the interpolated yawed depth
    // to its integer iso band HERE (per pixel, matching the SDF path's
    // per-pixel roundHalfUp), then fold in the *4 + slot SDF co-sort and
    // normalize into the framebuffer depth range. floor(x + 0.5) is the inlined
    // roundHalfUp (ir_iso_common.glsl) — ties round up, matching CPU/SDF.
    gl_FragDepth = (floor(vYawedDepth + 0.5) * 4.0 + vDepthBias) * vDepthScale;
}
