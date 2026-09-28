#ifndef IR_PER_AXIS_SURFACE_GLSL_INCLUDED
#define IR_PER_AXIS_SURFACE_GLSL_INCLUDED

#include "ir_iso_common.glsl"

// The signed face plane is already in faceOrigin; the raster anchor applies once.
vec3 perAxisFaceSurfacePoint(vec3 faceOrigin, int faceId, vec2 quadParam) {
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return faceOrigin + eu * quadParam.x + ev * quadParam.y - kVoxelRasterCellAnchor;
}

// The in-plane axes are orthonormal, so clamping gives the nearest finite-face
// point for conservative raster margins without extending the shadow receiver.
vec3 perAxisFaceClosestPoint(vec3 faceOrigin, int faceId, vec2 quadParam) {
    return perAxisFaceSurfacePoint(faceOrigin, faceId, clamp(quadParam, vec2(0.0), vec2(1.0)));
}

#endif
