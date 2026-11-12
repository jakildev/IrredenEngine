#ifndef IR_PER_AXIS_OCCLUSION_INCLUDED
#define IR_PER_AXIS_OCCLUSION_INCLUDED

#include "ir_per_axis_occluder_layout.glsl"

// Occlusion test of one per-axis face against the scatter occluder pyramid.
// Requires ir_per_axis_face_footprint.glsl ahead of it and a
// `uniform isampler2D occluderDepth` holding the pyramid
// c_per_axis_occluder_reduce built this frame.

// A face is hidden only when its nearest corner lies more than one tie band
// beyond the farthest occluder in its footprint. Its own fragments sit at
// least one further band back (the lane's two-band offset less the band
// floor), so two bands separate them from any occluder fragment.
const int kOcclusionSlackCodes = 32;
// Most texels one fine test reads.
const int kOcclusionFineTaps = 48;

const int kFaceMayOwnPixel = 0;
// No sample of the raster target lies inside the footprint.
const int kFaceUnsampled = 1;
// Every sample in the footprint is covered by a nearer occluder.
const int kFaceHidden = 2;

// Whether every pyramid texel of @p level over the pixel rectangle [lo, hi]
// is nearer than @p nearCode.
bool occluderRectNearer(int level, ivec2 lo, ivec2 hi, int nearCode, ivec2 framebufferSize) {
    const ivec2 origin = occluderLevelOrigin(level, framebufferSize);
    const ivec2 texelLo = lo >> level;
    const ivec2 texelHi = hi >> level;
    for (int y = texelLo.y; y <= texelHi.y; ++y) {
        for (int x = texelLo.x; x <= texelHi.x; ++x) {
            if (texelFetch(occluderDepth, origin + ivec2(x, y), 0).r >= nearCode) {
                return false;
            }
        }
    }
    return true;
}

int occluderRectTaps(int level, ivec2 lo, ivec2 hi) {
    const ivec2 span = (hi >> level) - (lo >> level) + ivec2(1);
    return span.x * span.y;
}

// Classifies a face against the occluder prepass: kFaceHidden and
// kFaceUnsampled faces cannot own a pixel of the composite.
int perAxisFaceOcclusion(PerAxisFaceFootprint footprint, ivec2 framebufferSize) {
    ivec2 lo;
    ivec2 hi;
    perAxisFootprintTexelBounds(footprint, lo, hi);
    lo = max(lo, ivec2(0));
    hi = min(hi, framebufferSize - ivec2(1));
    if (hi.x < lo.x || hi.y < lo.y) {
        return kFaceUnsampled;
    }
    const float nearest = clamp(perAxisFootprintNearestDepth(footprint), 0.0, 1.0);
    const int nearCode = int(floor(nearest * kOccluderDepthCodes)) - kOcclusionSlackCodes;

    // The coarsest answer first: four texels settle a face in a covered region.
    int coarse = 1;
    while (coarse < kOccluderLevels && occluderRectTaps(coarse, lo, hi) > 4) {
        ++coarse;
    }
    if (occluderRectTaps(coarse, lo, hi) > kOcclusionFineTaps) {
        return kFaceMayOwnPixel;
    }
    if (occluderRectNearer(coarse, lo, hi, nearCode, framebufferSize)) {
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
    return occluderRectNearer(fine, lo, hi, nearCode, framebufferSize) ? kFaceHidden
                                                                       : kFaceMayOwnPixel;
}

#endif
