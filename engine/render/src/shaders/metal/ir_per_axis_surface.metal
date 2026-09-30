#ifndef IR_PER_AXIS_SURFACE_METAL_INCLUDED
#define IR_PER_AXIS_SURFACE_METAL_INCLUDED

#include "ir_iso_common.metal"

// The signed face plane is already in faceOrigin; the raster anchor applies once.
inline float3 perAxisFaceSurfacePoint(float3 faceOrigin, int faceId, float2 quadParam) {
    float3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return faceOrigin + eu * quadParam.x + ev * quadParam.y - kVoxelRasterCellAnchor;
}

// The in-plane axes are orthonormal, so clamping gives the nearest finite-face
// point for conservative raster margins without extending the shadow receiver.
inline float3 perAxisFaceClosestPoint(float3 faceOrigin, int faceId, float2 quadParam) {
    return perAxisFaceSurfacePoint(faceOrigin, faceId, clamp(quadParam, float2(0.0), float2(1.0)));
}

#endif
