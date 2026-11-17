// Mirror of shaders/c_per_axis_overflow_census.glsl; keep the two in lockstep.
//
// Diagnostic census of the per-axis overflow lane against the finished scatter
// composite: how many records own at least one framebuffer pixel. Dispatched
// after the scatter draw, off the overflow relight's indirect grid.
//
// A record is counted as an owner when some sample inside its quad holds a
// composite depth within the tie-band window its own fragment could write
// there. Coverage and depth are recomputed here, not taken from the raster, so
// both tests carry slack and the count is an upper bound: it includes a record
// whose only such sample lies in the slack just outside its quad, on a
// neighbouring face that shares the edge. The interior count is the owners
// with such a sample inside the quad proper.

#include "ir_scatter_depth.metal"
#include "ir_per_axis_face_footprint.metal"

// Mirrors C_PerAxisTrixelCanvases::kOverflowControlUints.
constant uint kCensusOverflowControlUints = 128u;
// A footprint larger than this many texels is counted as an owner unexamined.
constant int kCensusMaxTexels = 4096;
// Outward coverage slack in pixels: a sample this close to an edge counts as
// inside.
constant float kCensusEdgeSlack = 0.0625f;

// overflowLane is the overflow scratch from its control block on: word 1 is the
// settled record count and the four-word records start
// kCensusOverflowControlUints in. censusCounters: [0] records examined,
// [1] owners, [2] records whose footprint holds no sample of the raster target,
// [3] owners with an owned sample inside the quad proper.
kernel void c_per_axis_overflow_census(
    constant PerAxisScatterGlobals& globals [[buffer(1)]],
    constant PerAxisScatterFrame& frame [[buffer(3)]],
    device const uint* overflowLane [[buffer(28)]],
    device atomic_uint* censusCounters [[buffer(26)]],
    texture2d<int, access::read> storeDistances [[texture(0)]],
    depth2d<float, access::read> compositeDepth [[texture(1)]],
    uint3 groupId [[threadgroup_position_in_grid]],
    uint3 groupCount [[threadgroups_per_grid]],
    uint3 localId [[thread_position_in_threadgroup]]
) {
    const uint workGroupIndex = groupId.x + groupId.y * groupCount.x;
    const uint gid = workGroupIndex * 64u + localId.x;
    const uint entryCount = overflowLane[1u];
    if (gid >= entryCount) {
        return;
    }
    atomic_fetch_add_explicit(&censusCounters[0], 1u, memory_order_relaxed);

    const uint entryBase = kCensusOverflowControlUints + gid * 4u;
    const uint packedCell = overflowEntryCell(overflowLane[entryBase + 0u]);
    const int2 cell = int2(int(packedCell & 0xFFFFu), int(packedCell >> 16u));
    const int rawDist = int(overflowLane[entryBase + 2u]);

    const PerAxisScatterView view = perAxisScatterView(
        frame, globals, int2(storeDistances.get_width(), storeDistances.get_height())
    );
    const PerAxisFaceFootprint footprint = perAxisFaceFootprint(cell, rawDist, view);
    int2 lo;
    int2 hi;
    perAxisFootprintTexelBounds(footprint, lo, hi);
    lo = max(lo, int2(0));
    hi = min(hi, int2(compositeDepth.get_width(), compositeDepth.get_height()) - int2(1));
    if (hi.x < lo.x || hi.y < lo.y) {
        atomic_fetch_add_explicit(&censusCounters[2], 1u, memory_order_relaxed);
        return;
    }
    if ((hi.x - lo.x + 1) * (hi.y - lo.y + 1) > kCensusMaxTexels) {
        atomic_fetch_add_explicit(&censusCounters[1], 1u, memory_order_relaxed);
        return;
    }

    const float2 edgeU = footprint.pixel[1] - footprint.pixel[0];
    const float2 edgeV = footprint.pixel[2] - footprint.pixel[0];
    const float area = edgeU.x * edgeV.y - edgeU.y * edgeV.x;
    if (area == 0.0f) {
        atomic_fetch_add_explicit(&censusCounters[2], 1u, memory_order_relaxed);
        return;
    }
    const float slackU = kCensusEdgeSlack * length(edgeV) / abs(area);
    const float slackV = kCensusEdgeSlack * length(edgeU) / abs(area);
    const float depthU = footprint.depth[1] - footprint.depth[0];
    const float depthV = footprint.depth[2] - footprint.depth[0];

    bool owner = false;
    for (int y = lo.y; y <= hi.y; ++y) {
        for (int x = lo.x; x <= hi.x; ++x) {
            const float2 offset = float2(float(x), float(y)) + 0.5f - footprint.pixel[0];
            const float u = (offset.x * edgeV.y - offset.y * edgeV.x) / area;
            const float v = (edgeU.x * offset.y - edgeU.y * offset.x) / area;
            if (u < -slackU || u > 1.0f + slackU || v < -slackV || v > 1.0f + slackV) {
                continue;
            }
            // The fragment's band: two bands behind the cell path, plus up to
            // one for the tie code and one for a receding edge, with a band of
            // slack either side.
            const float planar = footprint.depth[0] + u * depthU + v * depthV;
            const float band =
                floor(clamp(planar, 0.0f, 1.0f) / kScatterCellTieBand) * kScatterCellTieBand;
            const float composite = compositeDepth.read(uint2(uint(x), uint(y)));
            if (composite < band + kScatterCellTieBand ||
                composite > band + 5.0f * kScatterCellTieBand) {
                continue;
            }
            owner = true;
            if (u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f) {
                atomic_fetch_add_explicit(&censusCounters[1], 1u, memory_order_relaxed);
                atomic_fetch_add_explicit(&censusCounters[3], 1u, memory_order_relaxed);
                return;
            }
        }
    }
    if (owner) {
        atomic_fetch_add_explicit(&censusCounters[1], 1u, memory_order_relaxed);
    }
}
