/*
 * Project: Irreden Engine
 * File: v_peraxis_scatter_detached.glsl
 * Author: Evin Killian jakildev@gmail.com
 * Created Date: June 2026
 * -----
 * Detached-entity SO(3) — P3b (#1475) forward-scatter composite. The
 * per-DETACHED-entity analog of v_peraxis_scatter.glsl: where the camera path
 * scatters the world per-axis canvases under a scalar Z-yaw, this scatters a
 * rotating detached entity's own three per-axis canvases (populated by P3a,
 * #1464) under the entity's octahedral-snap RESIDUAL quaternion. Like the
 * camera path it forward-projects each non-empty cell's true deformed face
 * quad straight to the framebuffer (GL_LESS), bypassing the single-parity
 * de-tile gather (f_trixel_to_framebuffer) — so the #1256 stripe class cannot
 * occur. Pairs with f_peraxis_scatter.glsl (reused unchanged).
 */

#version 450 core

#include "ir_iso_common.glsl"

// Unit quad corner in [-0.5, 0.5]^2 from the shared QuadVAO. (aPos + 0.5)
// gives the {0,1}^2 corner selector for the two in-plane face axes.
layout (location = 0) in vec2 aPos;

layout (binding = 0) uniform sampler2D  triangleColors;
layout (binding = 1) uniform isampler2D triangleDistances;

// binding = 1 is reused for the GlobalConstants UBO; sampler image units and
// UBO binding points are separate GL namespaces (same pattern as
// v_peraxis_scatter.glsl / f_trixel_to_framebuffer.glsl).
layout(std140, binding = 1) uniform GlobalConstants {
    int kMinTriangleDistance;
    int kMaxTriangleDistance;
};

// Shared with v_/f_trixel_to_framebuffer + v_peraxis_scatter (binding 3). The
// gather + camera-scatter shaders read only the prefix through visibleFaceIds;
// the detached scatter reads the two P3b std140-appended fields at the end
// (detachedResidual / detachedDepthAxis), so existing offsets are unchanged.
layout (std140, binding = 3) uniform FrameDataIsoTriangles {
    mat4 mpMatrix;
    vec2 zoomLevel;
    vec2 canvasOffset;
    vec2 textureOffset;
    vec2 mouseHoveredTriangleIndex;
    vec2 effectiveSubdivisionsForHover;
    float showHoverHighlight;
    int distanceOffset;
    ivec2 perAxisBase;       // recovery base for this entity's axis canvases
    float visualYaw;         // unused on the detached path
    int _scatterPad;
    ivec4 visibleFaceIds;    // per-slot model FaceId (visibleTriplet); .w pad
    vec4 detachedResidual;   // octahedral-snap residual quat (qx,qy,qz,qw)
    vec4 detachedDepthAxis;  // isoDepthAxisModel(residual); .xyz used, .w pad
};

flat out vec4 vColor;
flat out float vDepth;

// Mirror of faceSpanCorner in v_peraxis_scatter.glsl: in-plane corner of a face
// whose `origin` already sits on the face plane (the store bakes the polarity
// via faceMicroPositionFixed6). cornerSel in {0,1}^2.
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
    // Empty cell — degenerate the instance off-screen (matches the camera path).
    if (color.a < 0.1) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        vColor = vec4(0.0);
        vDepth = 1.0;
        return;
    }

    const int rawDist = texelFetch(triangleDistances, ij, 0).r;
    const int rawDepth = rawDist >> 2;       // pos3DtoDistance of the model face origin
    const int slot = rawDist & 3;            // visible-triplet slot
    const int faceId = visibleFaceIds[slot];
    const int axis = faceId >> 1;

    // Recover the exact integer MODEL face origin from P3a's face-local store —
    // one integer subtraction, no inverse, exact at every residual. The camera
    // iso term baked into perAxisBase by the store cancels here (the anchor is
    // computed identically from the same perAxisBase + canvasSize), so `origin`
    // is the entity-local model position regardless of camera pan.
    const ivec3 anchor = faceLocalAnchor(perAxisBase, canvasSize);
    const ivec2 inPlane = ij - faceLocalBase(axis, anchor, canvasSize);
    const ivec3 origin = faceOriginFromInPlane(faceId, inPlane, rawDepth);

    // SO(3) reposition under the octahedral-snap residual (the detached analog
    // of the camera path's pos3DtoPos2DIsoYawed). The result is iso relative to
    // the entity's model origin (iso of model (0,0,0) == (0,0)); the entity's
    // on-screen position + scale + sub-pixel snap live entirely in mpMatrix
    // (built by ENTITY_CANVAS_TO_FRAMEBUFFER), so perAxisBase is recovery-only.
    const vec2 cornerSel = aPos + vec2(0.5);
    const vec3 modelCorner = faceSpanCorner(axis, vec3(origin), cornerSel);
    const vec2 isoModel = pos3DtoPos2DIsoRotated(modelCorner, detachedResidual);
    gl_Position = mpMatrix * vec4(isoModel, 1.0, 1.0);

    vColor = color;
    // Composite depth along the residual's model iso axis (#1475). NOT the store
    // key (rawDepth = un-rotated x+y+z, the origin-recovery key only) and NOT the
    // full composed rotation — the reposition's residual, so the entity's faces
    // co-sort with the smooth on-screen placement above. Keep the *4 + slot
    // encoding so it shares the world distance range (kMin/kMax). At identity
    // residual detachedDepthAxis == (1,1,1) so this collapses to x+y+z — but the
    // per-axis canvases are released at a snap, so the scatter never runs there.
    int residualDist = isoDepthAlongAxis(origin, detachedDepthAxis.xyz) * 4 + slot;
    vDepth = float(residualDist + distanceOffset - kMinTriangleDistance) /
             float(kMaxTriangleDistance - kMinTriangleDistance);
}
