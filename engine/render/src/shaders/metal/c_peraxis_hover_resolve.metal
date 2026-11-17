#include <metal_stdlib>
using namespace metal;
#include "ir_pick_record.metal"

struct HoverResolveFrame {
    float4 compositePrefix[16];
    int2 mouseFramebufferPixel;
    int hoverResolveMode;
    int gatherFilterDiagnostic;
    float4 cameraViewRotation;
    int2 presentedPickSize;
};

kernel void c_peraxis_hover_resolve(
    constant HoverResolveFrame &frame [[buffer(3)]],
    device PickRecord *picks [[buffer(14)]],
    texture2d<uint, access::read> entityIdAttachment [[texture(0)]],
    depth2d<float, access::read> compositeDepth [[texture(1)]],
    uint3 group [[threadgroup_position_in_grid]],
    uint3 groups [[threadgroups_per_grid]],
    uint3 local [[thread_position_in_threadgroup]]
) {
    const uint index = (group.x + group.y * groups.x) * 64u + local.x;
    const int2 size(compositeDepth.get_width(), compositeDepth.get_height());
    if (index > uint(frame.presentedPickSize.x * frame.presentedPickSize.y)) return;
    const int2 pixel = index == 0u
        ? int2(frame.mouseFramebufferPixel.x, size.y - 1 - frame.mouseFramebufferPixel.y)
        : int2(int(index - 1u) % size.x, int(index - 1u) / size.x);
    if (any(pixel < int2(0)) || any(pixel >= size)) return;
    const float depth = compositeDepth.read(uint2(pixel));
    const uint2 id = frame.hoverResolveMode == 0 ? entityIdAttachment.read(uint2(pixel)).rg : uint2(0);
    scatterPick(picks[index], id, depth, frame.hoverResolveMode);
}
