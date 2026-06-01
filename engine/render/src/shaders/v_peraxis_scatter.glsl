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

// In-plane corner of a face whose `origin` ALREADY sits at the face plane on
// the fixed axis. The store (c_voxel_to_trixel_stage_{1,2}) bakes the polarity
// via faceMicroPositionFixed6 — POS faces store the high-side plane, NEG faces
// the low-side plane — so the recovered depth lands on the face plane and the
// scatter only spans the face's two in-plane world axes (X->y,z  Y->x,z
// Z->x,y, matching faceInPlaneCoords). Re-adding the polarity offset here (the
// old per-faceId +1) double-shifts POS faces one cell past the plane: the
// #1310 back-face seam (a ~1px dark gap between the POS face and its neighbors
// at cardinals 1/2/3). cornerSel in {0,1}^2.
vec3 faceSpanCorner(int axis, vec3 origin, vec2 cornerSel) {
    if (axis == 0) return origin + vec3(0.0, cornerSel.x, cornerSel.y); // X face: span y,z
    if (axis == 1) return origin + vec3(cornerSel.x, 0.0, cornerSel.y); // Y face: span x,z
    return origin + vec3(cornerSel.x, cornerSel.y, 0.0);                // Z face: span x,y
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
    const int axis = faceId >> 1;

    // Recover the exact integer (or micro) face origin from the face-local
    // store. The cell's two in-plane coords + the stored iso depth give the
    // origin by one integer subtraction (faceOriginFromInPlane) — no
    // 2cos(yaw)+1 inverse, so it is exact at every yaw. The replaced iso-inverse
    // dropped compressed-axis faces (cell collisions -> cracks) and went
    // singular at yaw = +/-120 deg (-> a speckled cube). faceLocalAnchor matches
    // stage 1/2's store: same perAxisBase + canvasSize. See ir_iso_common.glsl.
    const ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
    const ivec2 inPlane = ij - faceLocalBase(axis, anchor, canvasSize);
    const vec3 origin = vec3(faceOriginFromInPlane(faceId, inPlane, rawDepth));

    // Project the selected face corner under the continuous yaw
    // (pos3DtoPos2DIsoYawed is linear, so this IS P(theta)*corner — the true
    // deformed footprint, with no gather / parity inverse). `origin` is already
    // the face plane, so only the in-plane axes are spanned (no polarity).
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 worldCorner = faceSpanCorner(axis, origin, cornerSel);
    const vec2 cornerIso = vec2(perAxisBase) + pos3DtoPos2DIsoYawed(worldCorner, visualYaw);

    // Inverse of the gather's aPos->canvasPixel map (v_trixel_to_framebuffer):
    //   canvasPixel = (aPos.x + 0.5, -aPos.y + 0.5) * canvasSize
    // so the scatter lands at the same screen scale/offset as the fast path.
    vec2 quadPos;
    quadPos.x = cornerIso.x / float(canvasSize.x) - 0.5;
    quadPos.y = 0.5 - cornerIso.y / float(canvasSize.y);
    gl_Position = mpMatrix * vec4(quadPos, 1.0, 1.0);

    vColor = color;
    // Yaw-consistent composite depth (#1370). The stored `rawDepth` (= un-yawed
    // world x+y+z) is the face-local origin-recovery KEY and must not change.
    // But the framebuffer depth test must order by the depth that matches the
    // YAWED screen projection above (iso of R_z(-visualYaw)*world), not the
    // un-yawed metric — otherwise the ordering diverges from the on-screen
    // placement as residual yaw grows and a low/back surface (the ground
    // platform) wins the depth test against geometry above it near +/-45 deg.
    // Re-derive the composite depth from the recovered origin, rotated by
    // R_z(-visualYaw); keep the *4 + slot encoding so it co-sorts with the SDF
    // (c_shapes_to_trixel smoothYaw applies the identical transform). Per-axis
    // is residual-only, so the cardinal fast path is untouched (byte-identical).
    float yc = cos(visualYaw);
    float ys = sin(visualYaw);
    float dvx = origin.x * yc + origin.y * ys;
    float dvy = -origin.x * ys + origin.y * yc;
    int yawedDist = roundHalfUp(dvx + dvy + origin.z) * 4 + slot;
    vDepth = float(yawedDist + distanceOffset - kMinTriangleDistance) /
             float(kMaxTriangleDistance - kMinTriangleDistance);
}
