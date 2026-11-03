#include <metal_stdlib>
using namespace metal;

struct HoverResolveFrame {
    float4 compositePrefix[16];
    int2 mouseFramebufferPixel;
    int hoverResolveMode;
    int hoverResolvePad;
};
struct HoverResolveRecord {
    uint2 entityId;
    float depth;
    float pad;
    uint2 scatterEntityId;
    float scatterDepth;
    uint scatterOwner;
};

kernel void c_peraxis_hover_resolve(
    constant HoverResolveFrame &frame [[buffer(3)]],
    device HoverResolveRecord &hover [[buffer(14)]],
    texture2d<uint, access::read> entityIdAttachment [[texture(0)]],
    depth2d<float, access::read> compositeDepth [[texture(1)]]
) {
    const int2 size(compositeDepth.get_width(), compositeDepth.get_height());
    if (any(frame.mouseFramebufferPixel < int2(0)) || any(frame.mouseFramebufferPixel >= size)) return;
    const uint2 pixel(frame.mouseFramebufferPixel.x, size.y - 1 - frame.mouseFramebufferPixel.y);
    const float depth = compositeDepth.read(pixel);
    if (frame.hoverResolveMode == 0) {
        const uint2 id = entityIdAttachment.read(pixel).rg;
        hover.scatterOwner = any(id != uint2(0)) ? 1u : 0u;
        hover.scatterEntityId = id;
        hover.scatterDepth = depth;
    } else if (hover.scatterOwner != 0u && as_type<uint>(depth) == as_type<uint>(hover.scatterDepth)) {
        hover.entityId = hover.scatterEntityId;
        hover.depth = depth;
    }
}
