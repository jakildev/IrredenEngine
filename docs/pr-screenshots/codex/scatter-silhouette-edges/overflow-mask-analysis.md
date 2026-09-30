# Six finite faces hidden by the overflow origin mask

The hardware-quad capture at camera yaw 292.5° exposes six game-framebuffer
pixels on the frozen orbit-6 GRID cube that the prior conservative margins
painted with the wrong Y+ normal. A finite-box ray through each pixel center
hits a Z− face. Those Z− faces lose their cardinal store cells to nearer
members of the same `(1,1,1)` coset and therefore require overflow records.
`overflowAppendTap` rejects both records using its origin-only view mask.

Run the independent standard-library reproduction from the repository root:

```sh
python3 docs/pr-screenshots/codex/scatter-silhouette-edges/overflow-mask-analysis.py --output /tmp/overflow-mask-analysis.json
```

The retained [result](overflow-mask-analysis.json) records every ray hit, face
coordinate, cardinal coset, and 2×2 mask winner. The script constructs the
12³ cube's 45° Y inverse-resampled occupancy (1,740 cells), casts finite box
rays at the 1280×720 game framebuffer centers with zoom 4 and yaw 292.5°, and
then independently reproduces the shared `viewMaskTap` / `overflowAppendTap`
equations. It reads no screenshots or GPU buffers.

The native normal-overlay fixture that revealed the pixels is shot 13 of:

```sh
fleet-run IRCanvasStress --auto-screenshot 6 --only orbit --focus-orbit 6 --no-spin --no-auto-rotate --pivot-origin --no-ao --debug-overlay normals --zoom 4 --subdivisions 2 --sweep-yaw 0 6.28318530718 17
```

| Game pixel | True Z− voxel | Z− face `(u,v)` | Same-voxel Y+ `v` | Z− key | Largest mask key |
| --- | --- | --- | ---: | --- | --- |
| (726,307) | (4,6,−5) | (.814065,.988585) | −.021092 | `0x3fffffc1` | `0x3fffffb8` |
| (725,308) | (4,6,−5) | (.616919,.974574) | −.046980 | `0x3fffffc1` | `0x3fffffb8` |
| (724,309) | (4,6,−5) | (.419774,.960564) | −.072869 | `0x3fffffc1` | `0x3fffffb8` |
| (731,309) | (5,6,−4) | (.963111,.990989) | −.016650 | `0x3fffffe6` | `0x3fffffb8` |
| (723,310) | (4,6,−5) | (.222629,.946553) | −.098757 | `0x3fffffc1` | `0x3fffffb8` |
| (730,310) | (5,6,−4) | (.765965,.976978) | −.042538 | `0x3fffffe6` | `0x3fffffb8` |

All six Z− coordinates lie strictly inside the finite face. The Y+ face of
the same voxel is just outside its finite footprint; its old conservative
margin supplied the wrong color. The two Z− faces are the farthest exposed
members of their cardinal Z-route cosets, so the normal cell draw cannot
recover them. The mask writes the minimum quantized yawed depth at each
rounded face *origin*. For a candidate face position `p`, the append pass
queries the four cells starting at `floor(P_yaw(p−½))`, takes their maximum
stored key, and rejects when

```text
floor(16 · D_yaw(p−½)) + 0x40000000 − 8 > max(mask keys of four origin cells).
```

For voxel (4,6,−5), its own exposed Y+ face wins the most permissive probed
mask cell at `0x3fffffb8`; the Z− key is nine quantization steps farther,
just past the eight-step allowance. For voxel (5,6,−4), that same Y+ key is
46 steps nearer. Raising the allowance by one step repairs only the first
voxel. The mask compares face-origin depths, while the two finite footprints
cover different pixel centers; it cannot infer that the whole Z− face is
occluded.

The buffer has room for a correctness-first no-mask route:
`C_PerAxisTrixelCanvases::overflowCapacityFor` reserves at least
`voxelCapacity × kAxisCount` records, rounded up to a power of two. The
resolve-mode-3 lane emits at most one record per voxel per axis. Removing this
mask therefore cannot exceed its stated record bound, though the larger sort
and draw require a separate performance measurement.

A local footprint expansion of this origin mask still cannot prove safe
culling: each mask value says only that a *face origin* had a nearer depth in
that cell. It does not certify that an opaque nearer face covers every pixel
center of the candidate's projected finite quad. Checking more origins or
corners may reduce false rejects but has the same logical gap. A conservative
reject would need coverage and depth bounds over the *entire* projected face
footprint, for example from a full-face visibility prepass. That is a different
algorithm, not a small neighborhood or epsilon adjustment.
