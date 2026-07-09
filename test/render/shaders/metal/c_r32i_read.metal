#include <metal_stdlib>
using namespace metal;

// Vehicle A (#1640) repro — read half. Reads an R32I distance texture via
// access::read — the exact path c_bake_sun_shadow_map uses
// (trixelDistances.read(px).x) — in a SECOND in-tick compute dispatch, and
// copies each texel into an output SSBO for CPU readback. If this second-
// dispatch read of a texture a prior same-command-buffer dispatch wrote returns
// the clear sentinel instead of the written value, that is the #1640 gap.
//
// One thread per texel (see c_r32i_write.metal for the 1x1x1-threadgroup note).
kernel void c_r32i_read(
    texture2d<int, access::read> dist [[texture(0)]],
    device int *out [[buffer(0)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint width = dist.get_width();
    uint2 px = gid.xy;
    if (px.x >= width || px.y >= dist.get_height()) {
        return;
    }
    out[px.y * width + px.x] = dist.read(px).x;
}
