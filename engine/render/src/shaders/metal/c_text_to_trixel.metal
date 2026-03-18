#include <metal_stdlib>
using namespace metal;

kernel void c_text_to_trixel(uint3 gid [[thread_position_in_grid]]) {
    (void)gid;
}
