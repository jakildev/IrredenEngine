# ShapeDebug receiver reference attribution

The native Metal ShapeDebug verification originally passed 51 of 56 checks.
Five default-scene references failed after cylinder receiver reconstruction.
A separate pair of clean, 21-frame default-scene captures isolated that change
before any reference update. The candidate used commit `37c0716848aaf41eb7c97737a9adf481a02b1ce6`;
the parent arm restored only the four previous receiver shader files.

Both arms have identical launch commands and settled camera records. Their
executable hashes differ: rebuilding after an unrelated temporary LOD edit was
reversed recompiled the same C++ source. The parent source patch records the
actual shader differences. The parent matching historical images provides a
stronger control than assuming identical executables across source versions.

## Attribution and reference decision

- The parent is pixel-identical to 19 historical references.
- `zoom1_origin` and `zoom1_odd_offset` each have an existing difference of
  16 pixels, with maximum channel delta 53. Candidate and parent are identical
  in both; their historical references remain untouched.
- Candidate changes 17 parent frames, totaling 34,792 pixels. Every changed
  pixel lies within a reviewed cylinder fixture rectangle. No changed pixel
  lies outside those rectangles.
- Equal-size cylinder copies have equal changed-pixel counts when fully
  visible. At yaw 90, viewport clipping reduces one copy's count from 1,216
  to 312.
- All 17 intentional changed references were refreshed, including the 12 that
  previously passed tolerance. The four unchanged references remain untouched.
  No threshold changed.

The [comparison](comparison.json) records parent versus history, candidate
versus history, and candidate versus parent. [Cylinder regions](cylinder-rois.json)
contain the reviewed rectangles; translated camera variants use the matching
image displacement. This is scene-specific spatial attribution, not a semantic
object-ID oracle. [Reference updates](reference-updates.json) preserve historical
and captured hashes for all 21 labels.

The real `scripts/render-compare.py` subsequently passed all 21 stored current
captures against the updated references using the unchanged manifest thresholds:
per-byte tolerance 8, matching bytes at least 99.9%, maximum delta 64, and PSNR
at least 35 dB. [Stored-capture validation](stored-capture-validation.json)
archives commands and results. This was an offline comparison, **not a native
rerun of all 56 checks**.

## Visual review

Full candidate images were inspected for all five failed labels and representative
passing but intentionally changed view families: `zoom2_origin`, `zoom4_origin`,
`zoom8_origin`, `zoom4_yaw90`, `zoom4_yaw45_inter_cardinal`, and
`lod_swap_zoom16_sphere`. All five historical full images and their original
difference images were inspected before the reference decision. Translation
variants were then checked numerically.

The projected edge on the yellow constant-LOD cylinder becomes sharper, and
self-shadow boundaries on cylinder sides no longer retain inappropriate direct
sunlight. Changes in passing views cluster at cylinder cap/side boundaries.
The main green cylinder and its rotation pair respond consistently. No unrelated
shape or floor projection changes were identified.

These captures do not certify all curved display reconstruction. Side-face
combs remain on several cylinders, and legacy stepped bands remain on other SDF
shapes. Shadows-disabled controls in the parent evidence set show that the
cylinder comb exists independently of sun visibility. The emitted-face
ownership/parity investigation remains open. Independent field/ray tests and
routing exclusions establish the bounded receiver contract; sharper appearance
alone is not the acceptance criterion.

## Preserved captures

Each arm contains the complete 21-frame manifest, clean-exit log, source patch,
original capture hashes and actual camera states. Logs and patches are gzip
compressed. Original PNGs are retained for the five initially failing pairs;
the other captured PNGs remain local and their hashes are archived.

| View | Parent | Candidate |
|---|---|---|
| Zoom 4, yaw 180 | [Image](parent/zoom4_yaw180.png) | [Image](after/zoom4_yaw180.png) |
| Zoom 4, yaw 270 | [Image](parent/zoom4_yaw270.png) | [Image](after/zoom4_yaw270.png) |
| Zoom 8, yaw 180 | [Image](parent/zoom8_yaw180.png) | [Image](after/zoom8_yaw180.png) |
| Zoom 4, panned yaw 90 | [Image](parent/zoom4_pan16_yaw90_pivot.png) | [Image](after/zoom4_pan16_yaw90_pivot.png) |
| Zoom 4, panned yaw 180 | [Image](parent/zoom4_pan16_yaw180_pivot.png) | [Image](after/zoom4_pan16_yaw180_pivot.png) |

Reproduce the native default capture using the respective source tree:

```sh
fleet-run --timeout 180 IRShapeDebug --auto-screenshot 10
```

The capture order is the default `shots` array in
`creations/demos/shape_debug/test/references/manifest.json`. Preserve each run
before launching a later render-verify pass, which clears the screenshot folder.
