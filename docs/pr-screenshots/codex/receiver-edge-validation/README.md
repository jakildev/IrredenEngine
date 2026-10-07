# Selected box receiver normal validation

Native macOS Metal, 1280×720 game resolution and 2560×1440 framebuffer.
Base source is `973c12a183ab0bde11fa2e037be22e79b0bb84d3`.
[implementation.patch](implementation.patch) is the production shader change
against that source. The capture JSON files record commands and image hashes;
logs retain the actual camera poses and clean-exit results.

## Change and coverage

The smooth-yaw solid BOX emitter stores the entered **world** face through the
cardinal triplet. At a strict receiver miss, compute lighting and shadow bias
previously treated that slot as a view-space face and rotated it a second time.
The selected owner now identifies this emitter contract and publishes its world
normal through the existing receiver-face channel. The recovered position is
unchanged; the normal-dependent shadow bias changes with the normal. Other
producers keep their existing slot convention. No new buffer, dispatch, blur or
silhouette expansion is introduced.

`test_render_shape_receiver.py` executes both shader adapters, producer
eligibility and slab/slot behavior, signed boundary-plane ownership, and the
normal carrier's consumer. Mutation controls reject wrong face frames, polarity,
eligibility and accepting emitter slab misses. These scalar checks supplement
native execution; they do not replace a full GPU geometry oracle.

| Capture | Command | Result |
|---|---|---|
| Compute normal before/after | `fleet-run IRCanvasStress --auto-screenshot 120 --debug-overlay normals` | Changed floor-edge samples decode to the expected world -Y instead of the yaw-rotated fallback |
| Canvas beauty after | `fleet-run IRCanvasStress --auto-screenshot 120` | All 12 full frames byte-identical to the parent control |
| Fog beauty after | `fleet-run IRFogDemo --auto-screenshot 10 --explored-decay` | All five full frames byte-identical to the parent control |

[comparisons.json](comparisons.json) records all 17 beauty comparisons. Parent
beauty controls come from the [previous attribution experiment](../lighting-density-cleanup/README.md).
Representative full-frame pairs are retained here. Unchanged beauty in these
controls does not imply unchanged output under every light direction: finite
fragment lighting already uses its own exact normal, and the fog fixture uses
a vertical sun that gives both old and corrected side normals zero Lambert.

## Floor corners

The settled camera yaws are 1.308994 and 1.0471947 radians, density one, zoom one.
The floor is a 120×120×4 BOX centered at (0,0,4). At framebuffer pixels
(1152,974) and (1036,950), and the other samples in their respective 4×4 blocks:

- Strict finite rays miss the floor; silhouette recovery reaches its -Y plane.
- Independent slab intersection and the production receiver agree on world
  y=-60, z≈5.9992–5.9997, x≈59.26–59.71, and normal (0,-1,0).
- A temporary fragment diagnostic returns `normal * 0.5 + 0.5` from
  `shapeSurfaceLighting` for the floor material. Its RGB is (128,0,128) at both
  points, confirming the final fragment normal. Full frames and the diagnostic
  patch are retained as `*-fragment-normal.png` and
  [floor-fragment-normals.patch](floor-fragment-normals.patch).
- With sun direction normalize(-.42,-.60,-.55), ambient .30 and intensity .70,
  the factor is .7585579, predicting RGB (114,115,121) for albedo (150,152,160).
  The older reference's (45,46,48) is ambient-only.

The built-in normal overlay observes the compute path, which bypasses finite
fragment lighting. Before this fix it therefore did not report the final normal
at these pixels. The diagnostic patch is not part of the production shaders.

## Fog panel investigation

Four temporary shader diagnostics isolate the three affected blocks in
`fog_explored_decay_tiers_yaw`. Each command is
`fleet-run IRFogDemo --auto-screenshot 10 --explored-decay`; each diagnostic
is an independent patch against the base source, removed before production
validation. Selected full frames, patches, commands and logs are retained as
`fog-{columns,prefog,normals,receiver-query}.*`.

| Framebuffer sample | Rounded world XY | Fog grid value | Incoming red | Old normal RGB | Relative receiver query | Stored slot |
|---|---|---|---|---|---|---|
| (554,544) | (38,2) | 255, fully visible | 92 | (8,84,128) | (13,-5) | 0 |
| (534,566) | (38,-1) | 255, fully visible | 92 | (8,84,128) | (8,6) | 0 |
| (530,570) | (38,-1) | 255, fully visible | 92 | (8,84,128) | (7,8) | 0 |

The columns diagnostic encodes XY plus 96 in RG and grid state in B. The prefog
diagnostic replaces B with incoming red. Normal RGB encodes the lighting normal
as `normal * 0.5 + 0.5`. Receiver-query RG stores the integer query relative to
the shape's projected center plus 64; B stores the slot. Thus the old normal is
approximately (-cos(.35),-sin(.35),0), confirming the extra rotation.

This panel's BOX descriptor is (4,4,1), center (40,0,2), density four: finite
half-extents are (1.625,1.625,.125), not the larger half-size suggested by the
demo's existing comments. All three recorded queries strictly miss the finite
box. Their fog cells are fully visible; the gray is present before fog. The
remaining question is emitter/display coverage at those silhouette samples,
not the explored-state color mapping. No screenshot reference or tolerance is
changed by this PR.

## Limits

This closes the demonstrated normal-frame mismatch. It does not claim all
inherited edge artifacts are fixed, certify performance, or validate native
OpenGL on this host. The three previously recorded reference failures remain
tracked separately while panel coverage is investigated.
