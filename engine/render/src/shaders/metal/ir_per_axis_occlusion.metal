// Mirrors shaders/ir_per_axis_occlusion.glsl; keep the two in lockstep.
#ifndef IR_PER_AXIS_OCCLUSION_METAL_INCLUDED
#define IR_PER_AXIS_OCCLUSION_METAL_INCLUDED

#include "ir_per_axis_face_footprint.metal"
#include "ir_per_axis_occluder_layout.metal"

// Occlusion test of one per-axis face against the scatter occluder pyramid
// c_per_axis_occluder_reduce built this frame.

// A face is hidden only when its nearest corner lies more than one tie band
// beyond the farthest occluder in its footprint. Its own fragments sit at
// least one further band back (the lane's two-band offset less the band
// floor), so two bands separate them from any occluder fragment.
constant int kOcclusionSlackCodes = 32;
// Most texels one fine test reads.
constant int kOcclusionFineTaps = 48;

constant int kFaceMayOwnPixel = 0;
// No sample of the raster target lies inside the footprint.
constant int kFaceUnsampled = 1;
// Every sample in the footprint is covered by a nearer occluder.
constant int kFaceHidden = 2;

// Whether every pyramid texel of @p level over the pixel rectangle [lo, hi]
// is nearer than @p nearCode.
inline bool occluderRectNearer(
    int level,
    int2 lo,
    int2 hi,
    int nearCode,
    int2 framebufferSize,
    texture2d<int, access::read> occluderDepth
) {
    const int2 origin = occluderLevelOrigin(level, framebufferSize);
    const int2 texelLo = lo >> level;
    const int2 texelHi = hi >> level;
    for (int y = texelLo.y; y <= texelHi.y; ++y) {
        for (int x = texelLo.x; x <= texelHi.x; ++x) {
            if (occluderDepth.read(uint2(origin + int2(x, y))).r >= nearCode) {
                return false;
            }
        }
    }
    return true;
}

inline int occluderRectTaps(int level, int2 lo, int2 hi) {
    const int2 span = (hi >> level) - (lo >> level) + int2(1);
    return span.x * span.y;
}

// Classifies a face against the occluder prepass: kFaceHidden and
// kFaceUnsampled faces cannot own a pixel of the composite.
inline int perAxisFaceOcclusion(
    PerAxisFaceFootprint footprint,
    int2 framebufferSize,
    texture2d<int, access::read> occluderDepth
) {
    int2 lo;
    int2 hi;
    perAxisFootprintTexelBounds(footprint, lo, hi);
    lo = max(lo, int2(0));
    hi = min(hi, framebufferSize - int2(1));
    if (hi.x < lo.x || hi.y < lo.y) {
        return kFaceUnsampled;
    }
    const float nearest = clamp(perAxisFootprintNearestDepth(footprint), 0.0f, 1.0f);
    const int nearCode = int(floor(nearest * kOccluderDepthCodes)) - kOcclusionSlackCodes;

    // The coarsest answer first: four texels settle a face in a covered region.
    int coarse = 1;
    while (coarse < kOccluderLevels && occluderRectTaps(coarse, lo, hi) > 4) {
        ++coarse;
    }
    if (occluderRectTaps(coarse, lo, hi) > kOcclusionFineTaps) {
        return kFaceMayOwnPixel;
    }
    if (occluderRectNearer(coarse, lo, hi, nearCode, framebufferSize, occluderDepth)) {
        return kFaceHidden;
    }
    // A coarse block reaches past the footprint, so a far texel there may lie
    // outside it; the finest level within the tap budget decides.
    int fine = 1;
    while (fine < coarse && occluderRectTaps(fine, lo, hi) > kOcclusionFineTaps) {
        ++fine;
    }
    if (fine == coarse) {
        return kFaceMayOwnPixel;
    }
    return occluderRectNearer(fine, lo, hi, nearCode, framebufferSize, occluderDepth)
        ? kFaceHidden
        : kFaceMayOwnPixel;
}

#endif
