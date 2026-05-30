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

out vec4 FragColor;

void main() {
    if (vColor.a < 0.1) {
        discard;
    }
    FragColor = vColor;
    gl_FragDepth = vDepth;
}
