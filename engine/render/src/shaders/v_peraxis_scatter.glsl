/*
 * Project: Irreden Engine
 * File: v_peraxis_scatter.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: May 2026
 * -----
 * Smooth camera Z-yaw — T3 (#1310) forward-scatter composite.
 */

#version 450 core

#include "ir_iso_common.glsl"

// Unit quad corner in [-0.5, 0.5]^2 from the shared QuadVAO. (aPos + 0.5)
// gives the {0,1}^2 corner selector for the two in-plane face axes.
layout (location = 0) in vec2 aPos;

layout (binding = 0) uniform sampler2D  triangleColors;
layout (binding = 1) uniform isampler2D triangleDistances;

// binding = 1 is intentionally reused below for the GlobalConstants UBO: in
// GL 4.5 sampler texture-image units and uniform-buffer binding points are
// separate namespaces, so the shared index does not collide (same pattern as
// f_trixel_to_framebuffer.glsl).
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with f_/v_trixel_to_framebuffer (binding 3). The cardinal fast path
// reads only the prefix; the T3 scatter adds perAxisBase / visualYaw /
// visibleFaceIds at the end (std140 append — existing offsets unchanged).
layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    ivec2 perAxisBase;       // canvas-pixel origin of this axis canvas (#1310)
    float visualYaw;         // continuous camera Z-yaw (radians)
    int _scatterPad;
    ivec4 visibleFaceIds;    // per-slot world FaceId (0..5); .w pad
};

flat out vec4 vColor;
flat out float vDepth;

// World (or micro) in-plane corner of a cube face. cornerSel in {0,1}^2 maps
// to the face's two in-plane axes; the fixed axis sits at the NEG (low) or
// POS (high, +1) side.
vec3 faceCorner(int faceId, vec3 origin, vec2 cornerSel) {
    if (faceId == kFaceXNeg) return origin + vec3(0.0, cornerSel.x, cornerSel.y);
    if (faceId == kFaceXPos) return origin + vec3(1.0, cornerSel.x, cornerSel.y);
    if (faceId == kFaceYNeg) return origin + vec3(cornerSel.x, 0.0, cornerSel.y);
    if (faceId == kFaceYPos) return origin + vec3(cornerSel.x, 1.0, cornerSel.y);
    if (faceId == kFaceZNeg) return origin + vec3(cornerSel.x, cornerSel.y, 0.0);
    return origin + vec3(cornerSel.x, cornerSel.y, 1.0); // kFaceZPos
}

void main() {
    const ivec2 canvasSize = textureSize(triangleDistances, 0);
    const int cell = gl_InstanceID;
    const ivec2 ij = ivec2(cell % canvasSize.x, cell / canvasSize.x);

    const vec4 color = texelFetch(triangleColors, ij, 0);
    // Empty cell — kColorClear alpha is 0 (matches the gather's discard test).
    // Degenerate the whole instance off-screen so it produces no fragments.
    if (color.a < 0.1) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColor = vec4(0.0);
        vDepth = 1.0;
        return;
    }

    const int rawDist = texelFetch(triangleDistances, ij, 0).r;
    const int rawDepth = rawDist >> 2;       // pos3DtoDistance of the face origin
    const int slot = rawDist & 3;            // visible-triplet slot
    const int faceId = visibleFaceIds[slot];

    // Recover the integer world (or micro) origin from the stored cell.
    // (isoRel, rawDepth) + visualYaw uniquely invert pos3DtoPos2DIsoYawed:
    //   isoRel.x = -vx + vy ; isoRel.y = -vx - vy + 2z ; rawDepth = x+y+z
    // with (vx,vy) = R_z(-yaw)(x,y). Solving:
    //   z = (rawDepth + P(c+s) - Q(c-s)) / (2c+1),  P=(ix+iy)/2, Q=(ix-iy)/2
    // The denominator 2cos(yaw)+1 is the depth-monotonicity quantity from the
    // design doc — strictly positive (>= 1+sqrt2) across the +/-45 bracket, so
    // the inverse is always well defined.
    const vec2 isoRel = vec2(ij - perAxisBase);
    const float c = cos(visualYaw);
    const float s = sin(visualYaw);
    const float P = (isoRel.x + isoRel.y) * 0.5;
    const float Q = (isoRel.x - isoRel.y) * 0.5;
    const float z = (float(rawDepth) + P * (c + s) - Q * (c - s)) / (2.0 * c + 1.0);
    const float vx = z - P;
    const float vy = Q + z;
    const vec3 origin = vec3(
        roundHalfUp(vx * c - vy * s),
        roundHalfUp(vx * s + vy * c),
        roundHalfUp(z)
    );

    // Project the selected cube-face corner under the same continuous yaw the
    // store used (pos3DtoPos2DIsoYawed is linear, so this IS P(theta)*corner —
    // the true deformed footprint, with no gather / parity inverse).
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 worldCorner = faceCorner(faceId, origin, cornerSel);
    const vec2 cornerIso = vec2(perAxisBase) + pos3DtoPos2DIsoYawed(worldCorner, visualYaw);

    // Inverse of the gather's aPos->canvasPixel map (v_trixel_to_framebuffer):
    //   canvasPixel = (aPos.x + 0.5, -aPos.y + 0.5) * canvasSize
    // so the scatter lands at the same screen scale/offset as the fast path.
    vec2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5;
    quadPos.y = 0.5 - cornerIso.y / float(canvasSize.y);
    gl_Position = mpMatrix * vec4(quadPos, 1.0, 1.0);

    vColor = color;
    vDepth = float(rawDist + distanceOffset - kMinTriangleDistance) /
             float(kMaxTriangleDistance - kMinTriangleDistance);
}
