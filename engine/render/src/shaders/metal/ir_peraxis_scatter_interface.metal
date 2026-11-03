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
    // Depth change per unit of each in-plane parameter, on depth's scale.
    float2 depthSlope [[flat]];
    // Face-center iso-depth for depth-color. Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    float isoDepth [[flat]];
    int depthColorMode [[flat]];
    float depthColorExtent [[flat]];
    // Face/cell priority within a depth band.
    float cellTieOffset [[flat]];
    // An overflow entry's fog class byte, plus kScatterFogCarrierCutFace on a
    // cut face; 0 for a cell, whose class rides its axis canvas's entity id.
    uint fogCarrier [[flat]];
    uint2 overflowEntityId [[flat]];
};

struct FragmentOut {
    float4 color [[color(0)]];
    uint2 entityId [[color(1)]];
    float depth [[depth(any)]];
};

#endif
