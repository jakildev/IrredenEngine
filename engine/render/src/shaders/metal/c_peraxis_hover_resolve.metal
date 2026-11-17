#include <metal_stdlib>
using namespace metal;
#include "ir_pick_record.metal"

struct HoverResolveFrame {
    float4 compositePrefix[16];
    int2 mouseFramebufferPixel;
    int hoverResolveMode;
    int gatherFilterDiagnostic;
    float4 cameraViewRotation;
};

kernel void c_peraxis_hover_resolve(
    constant HoverResolveFrame &frame [[buffer(3)]],
    device PickRecord &pick [[buffer(14)]],
    texture2d<uint, access::read> entityIdAttachment [[texture(0)]],
    depth2d<float, access::read> compositeDepth [[texture(1)]]
) {
    const int2 size(compositeDepth.get_width(), compositeDepth.get_height());
    const int2 pixel(frame.mouseFramebufferPixel.x, size.y - 1 - frame.mouseFramebufferPixel.y);
    if (any(pixel < int2(0)) || any(pixel >= size)) return;
    const float depth = compositeDepth.read(uint2(pixel));
    const uint2 id = frame.hoverResolveMode == 0 ? entityIdAttachment.read(uint2(pixel)).rg : uint2(0);
    scatterPick(pick, id, depth, frame.hoverResolveMode);
}
