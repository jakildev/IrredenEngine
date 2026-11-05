# LOD zoom profiles — native fixture evidence (macOS / Metal)

`fleet-run IRShapeDebug --lod-zoom-profile --auto-screenshot 20`, then
`python3 scripts/render-lod-zoom-profile-metric.py --log <log> --screenshots build/creations/demos/shape_debug`.

Every pair is a red coarse member on `[LOD_3 .. LOD_4]` and a blue fine member
on `[LOD_0 .. LOD_2]`. Left to right in the world row: pinned to `LOD_0`
(always blue), SDF, DETACHED_REVOXELIZE, DETACHED, GRID. Top-right portrait
inherits the main profile; bottom-right portrait owns a 0.45-octave profile on
the default thresholds.

Main profile thresholds 2, 5, 10, 20. Shots 0–3 at width 0, then a
quarter-octave half-width (band `[4.20, 5.95]` around 5×).

| shot | main zoom | width | expected world | main tier | inherit portrait (zoom → tier) | own portrait (zoom → tier) |
|---|---|---|---|---|---|---|
| 0 | 4.9 | 0 | coarse | 3 | 4 → 3 | 2 → 3 |
| 1 | 5.1 | 0 | fine (swap) | 2 | 8 → 2 | 4 → 3 (held) |
| 2 | 4.9 | 0 | coarse (swap) | 3 | 4 → 3 | 2 → 3 |
| 3 | 5.1 | 0 | fine (swap) | 2 | 8 → 2 | 4 → 3 (held) |
| 4 | 6.0 | 0.25 | fine | 2 | 4 → 3 | 8 → 2 (swap) |
| 5–7 | 4.9 / 5.1 / 4.9 | 0.25 | fine (held) | 2 | 8/4/8 | 4 → 2 (held) / 8 / 4 → 2 (held) |
| 8 | 4.1 | 0.25 | coarse (one swap, below 4.20) | 3 | 4 → 3 | 2 → 3 (swap) |
| 9–11 | 5.1 / 4.9 / 5.9 | 0.25 | coarse (held) | 3 | 8/4/8 | 4 → 3 (held) / 2 / 4 → 3 (held) |
| 12 | 6.0 | 0.25 | fine (one swap, above 5.95) | 2 | 4 → 3 | 8 → 2 (swap) |
| 13–14 | 4.9 / 5.1 | 0.25 | fine (held) | 2 | 8 / 4 | 4 → 2 (held) / 2 → 3 (swap) |

Under the default profile the inherit portrait's 4× shots would be `LOD_2`
(blue); they read `LOD_3` (red), so the portrait selects with the main
camera's profile. The own portrait's 4× shots hold whichever side it arrived
from, independent of the main camera's history.

- `tier-trace.txt` — the per-shot `LOD-PROFILE-TIER` lines (main tier, both
  portrait tiers, the main half-width in force).
- `regions.txt` — the per-shot screen boxes and the variant each must show.
- `metric.txt` — the pixel gate: 105/105 regions show the expected colour and
  none of the other. A control run of the gate against the same captures with
  every expected label flipped reports 0/105.
- Each capture also asserted in-process (`IR_ASSERT`): the main and portrait
  tiers, exactly one DENSE member `renders()` per pair, the pinned pair fine,
  each portrait's `drawnSubject` is the expected member, and every gated
  member's authored voxel records (and both SDF records) are unchanged.

Captures: `lod_profile_00_sharp_coarse_z4_9.png`, `lod_profile_01_sharp_fine_z5_1.png`
(width 0 swaps across 5×), `lod_profile_05_band_held_fine_z4_9.png` (held fine
inside the band), `lod_profile_08_band_exit_down_z4_1.png` (one swap below the
band), `lod_profile_11_band_held_coarse_z5_9.png` (held coarse at the band's
top), `lod_profile_12_band_exit_up_z6_0.png` (one swap above it).

The DETACHED pair draws fragmented faces in every shot. A run with a lone
DETACHED set (its twin left in GRID) shows the same fragments, so it is the
fixture's DETACHED source-face presentation rather than the LOD gate; the
colour the gate selects is still the only one in its box.
