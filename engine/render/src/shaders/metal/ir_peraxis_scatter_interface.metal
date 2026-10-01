#ifndef IR_PERAXIS_SCATTER_INTERFACE_METAL_INCLUDED
#define IR_PERAXIS_SCATTER_INTERFACE_METAL_INCLUDED

struct VertexOut {
    float4 position [[position]];
    float4 color [[flat]];
    float3 faceOrigin [[flat]];
    int faceId [[flat]];
    int2 ownerPixel [[flat]];
    int3 visibilityExtent [[flat]];
    // No-perspective interpolation keeps depth and surface queries on the
    // finite face projected by the vertex shader.
    float depth [[center_no_perspective]];
    float2 quadParam [[center_no_perspective]];
    // Face-center iso-depth for depth-color. Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    float isoDepth [[flat]];
    int depthColorMode [[flat]];
    float depthColorExtent [[flat]];
    // Face/cell priority within a depth band.
    float cellTieOffset [[flat]];
};

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

#endif
