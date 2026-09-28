#ifndef IR_PER_AXIS_SURFACE_METAL_INCLUDED
#define IR_PER_AXIS_SURFACE_METAL_INCLUDED

#include "ir_iso_common.metal"

// The signed face plane is already in faceOrigin; the raster anchor applies once.
inline float3 perAxisFaceSurfacePoint(float3 faceOrigin, int faceId, float2 quadParam) {
    float3 eu, ev;
    faceInPlaneUnitAxes(faceId >> 1, eu, ev);
    return faceOrigin + eu * quadParam.x + ev * quadParam.y - kVoxelRasterCellAnchor;
}

#endif
