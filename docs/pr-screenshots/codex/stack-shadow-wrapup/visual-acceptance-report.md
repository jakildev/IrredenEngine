# Integrated rendering stack visual acceptance

Captured at `75a4729d69437b1eee7e2aa23c1c553bfb5f3d40` on Metal (M4 Max). All 19 manifests use the same binary and shader hashes. The source-face oracle script is unchanged between that capture commit and the analysis checkout.

- **20/20 source-face oracle views passed:** four identity frame sun-beauty views at yaws 22.5°, 112.5°, 202.5°, and 292.5°; four each of rotating frame shadow and normal overlays; four each of rotating octahedron shadow and normal overlays. Every oracle reported zero missing, extra, wrong-normal, false-shadow, missed-shadow, and invalid-overlay interior pixels. All 12 sun-shadow views exercised occlusion: 40,696 shadowed interior pixels in aggregate.
- **31/31 exact RGB pairs matched:** mixed lighting 5, no-shadows 5, sky-only 5, subdivision-8 9, parked visibility 3, released visibility 4.
- **4/4 frame feature-toggle occupancy masks matched.** The feature changed RGB on 1,172, 1,044, 796, and 1,808 pixels respectively, so the mask equality is not a vacuous duplicate-image comparison.
- Representative beauty, debug-overlay, mixed, no-shadow, sky, subdivision, parked, and released frames were visually inspected. The frame shape, inner apertures, normal/shadow debug colors, scene placement, cast shadows, and expected mask-preserving light change appear coherent.

The oracle intentionally ignores one-pixel silhouette/face-edge bands, and the shadow oracle ignores one-pixel light-transition bands. It excluded 2,276 boundary pixels across identity-frame beauty, 1,539 across rotating-frame shadows, and 4,200 across octahedron shadows. Thus zero errors proves the tested interiors, not every transition pixel. The exact RGB control pairs and full-image occupancy equality cover their respective scenarios without that tolerance.

Machine-readable detail: `metric-results.json`, `paired-comparisons.json`. Each oracle's `oracle-N-errors.png` diagnostic is also retained next to its source capture.
