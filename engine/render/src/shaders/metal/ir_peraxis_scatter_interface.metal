#ifndef IR_PERAXIS_SCATTER_INTERFACE_METAL_INCLUDED
#define IR_PERAXIS_SCATTER_INTERFACE_METAL_INCLUDED

struct VertexOut {
    float4 position [[position]];
    float4 color [[flat]];
    float3 faceOrigin [[flat]];
    int faceId [[flat]];
    int2 ownerPixel [[flat]];
    int3 visibilityExtent [[flat]];
    // Per-fragment PLANAR composite depth + margin classification —
    // mirror of v_/f_peraxis_scatter.glsl. depth is the face plane's exact
    // depth linearly interpolated (no-perspective, w==1) from per-corner
    // planar keys; quadParam spans the exact footprint on [0,1]^2 with
    // dilated corners landing outside, so the fragment stage can make
    // conservative-dilation margins yield by marginBias instead of letting
    // draw order decide same-plane overlaps (which paints wrong-voxel-color
    // bands).
    float depth [[center_no_perspective]];
    float2 quadParam [[center_no_perspective]];
    float marginBias [[flat]];
    // Per-axis margin-yield slope, vDepth units per unit quad-param
    // penetration — mirror of v_/f_peraxis_scatter.glsl. The fragment stage scales
    // a margin's yield by penetration * slope so a cell-deep margin yields a shared
    // ridge to the neighbor face's exact footprint (the doubled top<->side sliver).
    float marginYieldGradU [[flat]];
    float marginYieldGradV [[flat]];
    // Interior-edge yield-slope floor, vDepth units per unit quad-param
    // penetration. The per-axis slopes above are the OWN plane's depth
    // gradients — near zero along a foreshortened axis — but a margin that
    // penetrates an INTERIOR edge extends over the ADJACENT visible face,
    // whose plane can diverge from the extrapolation at up to
    // 2*sqrt(2)*encScale per world unit. At fractional offsets the sub-pixel
    // phase then tips the near-balanced margin-vs-exact contest per pixel,
    // producing a shared-edge fringe. Flooring the slope at
    // kScatterMarginYieldGradScale * encScale (>= the divergence bound) for
    // interior-edge penetration makes such margins always lose to the
    // adjacent face's exact fragments; they keep only their gap-fill job.
    // Boundary (silhouette) penetrations keep the tighter own-slope yield.
    float marginYieldGradFloor [[flat]];
    // Flat interior-edge yield: covers the constant (flip << 2) | slot
    // key-tiebreak span between adjacent faces' planes — the
    // penetration-independent advantage a sub-pixel interior margin can hold
    // over the adjacent face's exact fragments. Equals
    // kScatterMarginInteriorBiasKey (ir_iso_common.metal) in depth units.
    float marginInteriorYieldBias [[flat]];
    // Face-center iso-depth for depth-color. Flat (constant across the
    // quad) — origin is the same for all 4 corners of a face instance so
    // interpolation is a no-op; flat avoids rasterization divergence.
    float isoDepth [[flat]];
    int depthColorMode [[flat]];
    float depthColorExtent [[flat]];
    // Face/cell priority within a depth band. Displaced cells can share
    // a code; final coverage arbitration only separates margin/exact ties.
    float cellTieOffset [[flat]];
    // Per-edge interior/boundary classification for analytic coverage —
    // .x = u-low, .y = u-high, .z = v-low, .w = v-high (in the face's eu/ev basis);
    // 1 = interior (fill solid / close seam), 0 = true silhouette (crisp trim). An
    // edge is interior if the face continues to its same-axis in-plane neighbour OR
    // it points toward a visible perpendicular face (a convex cube edge shared with
    // another visible face). Flat: classified once per instance, constant across its
    // quad.
    float4 edgeInterior [[flat]];
};

struct FragmentOut {
    float4 color [[color(0)]];
    float depth [[depth(any)]];
};

#endif
