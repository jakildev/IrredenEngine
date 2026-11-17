#version 450 core

// Diagnostic census of the per-axis overflow lane against the finished scatter
// composite: how many records own at least one framebuffer pixel. Dispatched
// after the scatter draw, off the overflow relight's indirect grid. Metal
// mirror in metal/c_per_axis_overflow_census.metal.
//
// A record is counted as an owner when some sample inside its quad holds a
// composite depth within the tie-band window its own fragment could write
// there. Coverage and depth are recomputed here, not taken from the raster, so
// both tests carry slack and the count is an upper bound: it includes a record
// whose only such sample lies in the slack just outside its quad, on a
// neighbouring face that shares the edge. The interior count is the owners
// with such a sample inside the quad proper.

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

#include "ir_scatter_depth.glsl"
#include "ir_per_axis_face_footprint.glsl"

// The overflow scratch from its control block on: word 1 is the settled record
// count and the four-word records start kOverflowControlUints in.
layout(std430, binding = 28) readonly buffer OverflowLane {
    uint overflowLane[];
};

// [0] records examined, [1] owners, [2] records whose footprint holds no
// sample of the raster target, [3] owners with an owned sample inside the quad
// proper.
layout(std430, binding = 26) buffer OverflowCensus {
    uint censusCounters[];
};

layout(binding = 0) uniform isampler2D storeDistances;
layout(binding = 1) uniform sampler2D compositeDepth;

// Mirrors C_PerAxisTrixelCanvases::kOverflowControlUints.
const uint kOverflowControlUints = 128u;
// A footprint larger than this many texels is counted as an owner unexamined.
const int kCensusMaxTexels = 4096;
// Outward coverage slack in pixels: a sample this close to an edge counts as
// inside.
const float kCensusEdgeSlack = 0.0625;

void main() {
    const uint workGroupIndex = gl_WorkGroupID.x + gl_WorkGroupID.y * gl_NumWorkGroups.x;
    const uint gid = workGroupIndex * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
    const uint entryCount = overflowLane[1u];
    if (gid >= entryCount) {
        return;
    }
    atomicAdd(censusCounters[0u], 1u);

    const uint entryBase = kOverflowControlUints + gid * 4u;
    const uint packedCell = overflowEntryCell(overflowLane[entryBase + 0u]);
    const ivec2 cell = ivec2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int rawDist = int(overflowLane[entryBase + 2u]);

    const PerAxisScatterView view = perAxisScatterView(textureSize(storeDistances, 0));
    const PerAxisFaceFootprint footprint = perAxisFaceFootprint(cell, rawDist, view);
    ivec2 lo;
    ivec2 hi;
    perAxisFootprintTexelBounds(footprint, lo, hi);
    lo = max(lo, ivec2(0));
    hi = min(hi, textureSize(compositeDepth, 0) - ivec2(1));
    if (hi.x < lo.x || hi.y < lo.y) {
        atomicAdd(censusCounters[2u], 1u);
        return;
    }
    if ((hi.x - lo.x + 1) * (hi.y - lo.y + 1) > kCensusMaxTexels) {
        atomicAdd(censusCounters[1u], 1u);
        return;
    }

    const vec2 edgeU = footprint.pixel[1] - footprint.pixel[0];
    const vec2 edgeV = footprint.pixel[2] - footprint.pixel[0];
    const float area = edgeU.x * edgeV.y - edgeU.y * edgeV.x;
    if (area == 0.0) {
        atomicAdd(censusCounters[2u], 1u);
        return;
    }
    const float slackU = kCensusEdgeSlack * length(edgeV) / abs(area);
    const float slackV = kCensusEdgeSlack * length(edgeU) / abs(area);
    const float depthU = footprint.depth[1] - footprint.depth[0];
    const float depthV = footprint.depth[2] - footprint.depth[0];

    bool owner = false;
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const vec2 offset = vec2(float(x), float(y)) + 0.5 - footprint.pixel[0];
            const float u = (offset.x * edgeV.y - offset.y * edgeV.x) / area;
            const float v = (edgeU.x * offset.y - edgeU.y * offset.x) / area;
            if (u < -slackU || u > 1.0 + slackU || v < -slackV || v > 1.0 + slackV) {
                continue;
            }
            // The fragment's band: two bands behind the cell path, plus up to
            // one for the tie code and one for a receding edge, with a band of
            // slack either side.
            const float planar = footprint.depth[0] + u * depthU + v * depthV;
            const float band =
                floor(clamp(planar, 0.0, 1.0) / kScatterCellTieBand) * kScatterCellTieBand;
            const float composite = texelFetch(compositeDepth, ivec2(x, y), 0).r;
            if (composite < band + kScatterCellTieBand ||
                composite > band + 5.0 * kScatterCellTieBand) {
                continue;
            }
            owner = true;
            if (u >= 0.0 && u <= 1.0 && v >= 0.0 && v <= 1.0) {
                atomicAdd(censusCounters[1u], 1u);
                atomicAdd(censusCounters[3u], 1u);
                return;
            }
        }
    }
    if (owner) {
        atomicAdd(censusCounters[1u], 1u);
    }
}
