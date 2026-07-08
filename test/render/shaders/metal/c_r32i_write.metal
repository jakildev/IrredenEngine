#include <metal_stdlib>
using namespace metal;

// Vehicle A (#1640) repro — populate half. Writes a per-texel distinct value
// (its linear index) into an R32I distance texture via a plain access::write
// store, mirroring c_voxel_to_trixel_stage_2's texture write (the step at which
// a canvas's distance texture becomes canonical). A distinct-per-texel value
// (not a constant) makes a partial or missing write detectable in readback.
//
// One thread per texel: this kernel is intentionally absent from
// threadgroupSizeForFunctionName, so threadsPerThreadgroup defaults to 1x1x1
// and dispatchCompute's threadgroup counts (W,H,1) make the grid W*H — so
// thread_position_in_grid IS the texel coordinate.
kernel void c_r32i_write(
    texture2d<int, access::write> dist [[texture(0)]],
    uint3 gid [[thread_position_in_grid]]
) {
    uint width = dist.get_width();
    uint2 px = gid.xy;
    if (px.x >= width || px.y >= dist.get_height()) {
        return;
    }
    dist.write(int4(int(px.y * width + px.x), 0, 0, 0), px);
}
