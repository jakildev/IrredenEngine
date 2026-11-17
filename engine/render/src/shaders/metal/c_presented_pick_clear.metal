#include <metal_stdlib>
using namespace metal;
#include "ir_pick_record.metal"
struct PickImageFrame {
    float4 prefix[18];
    int2 presentedPickSize;
};
kernel void c_presented_pick_clear(
    constant PickImageFrame &frame [[buffer(3)]],
    device PickRecord *picks [[buffer(14)]],
    uint3 group [[threadgroup_position_in_grid]],
    uint3 groups [[threadgroups_per_grid]],
    uint3 local [[thread_position_in_threadgroup]]
) {
    const uint index = (group.x + group.y * groups.x) * 64u + local.x;
    if (index > uint(frame.presentedPickSize.x * frame.presentedPickSize.y)) return;
    picks[index] = PickRecord{uint2(0), 1.0f, 0.0f, uint2(0), 1.0f, 0u};
}
