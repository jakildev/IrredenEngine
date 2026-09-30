#ifndef IR_PER_AXIS_SURFACE_GLSL_INCLUDED
#define IR_PER_AXIS_SURFACE_GLSL_INCLUDED

#include "ir_iso_common.glsl"

// The signed face plane is already in faceOrigin; the raster anchor applies once.
vec3 perAxisFaceSurfacePoint(vec3 faceOrigin, int faceId, vec2 quadParam) {
    vec3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return faceOrigin + eu * quadParam.x + ev * quadParam.y - kVoxelRasterCellAnchor;
}

#endif
