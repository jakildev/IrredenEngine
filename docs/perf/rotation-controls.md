# Frozen rotation and density controls

`scripts/perf/rotation_controls.py` separates three questions: changing density
at fixed zoom, changing voxel population at matched projected block extent, and
enabling the existing culling path. Two 300-frame rounds cover 16 cases, with
the second round in reverse order. All 32 runs completed cleanly on native
Metal, M4 Max, Debug, with one verified binary/shader identity. Raw reports,
manifests, the runtime configuration and complete tables are retained in
[rotation-controls/](rotation-controls/).

## Findings

* Fixed zoom 1, frozen 32³ wave, FULL mode: base subdivisions 1 versus 4
  produce effective subdivisions 1 versus 4 in the runtime logs. Cardinal
  stage-1 GPU time rises from 0.480 to 0.749 ms. At 45°, per-axis storage is
  1.553 versus 1.538 ms and finalization 0.444 versus 0.445 ms. The per-axis
  base-resolution path avoids the density multiplier here. This is not proof
  of general zoom parity: zoom changes the visible region and other resources.
* The one-degree case already takes the per-axis path. Its frame means
  (13.345/13.005 ms) are much closer to 45° (13.480/13.670 ms) than cardinal
  (8.605/8.575 ms). The switch to a different pipeline matters even near a
  cardinal angle. Run spread is large; no small frame-time delta is a win.
* Solid blocks with edge/zoom pairs 16/4, 32/2 and 64/1 keep their projected
  extent approximately constant. At 45°, mean retained candidates grow
  1,347 → 5,749 → 23,736 and repeated axis entries 1,531 → 6,124 → 24,494.
  Storage grows 0.663 → 0.681 → 0.814 ms, overflow 0.263 → 0.323 → 0.499 ms.
  Cross-set exposure masking already removes the solid interior; remaining
  source-face work grows with surface population. These controls also change
  world extent and lighting relationships, so they do not isolate pixel cost.
* Frozen 64³ wave, NONE mode, zoom 2, shadows disabled: cardinal culling
  reduces retained candidates 261,267 → 31,422 and stage 1 from 0.508 to
  0.154 ms. Frame means are 8.635 versus 8.940 ms: no whole-frame win proved.
  The unrelated light-volume row varies 3.870 → 4.527 ms. Rotated culling
  on/off retains exactly the same population, as expected from its disabled
  Hi-Z gate; this is a negative control, not active rotated occlusion culling.

GPU figures are means of sampled invocations, not sums of complete frame work.
Many small cases cluster near 8.5 ms; use stage timing rather than interpreting
equal frame times as equal work. Two rounds and a shared interactive host do not
establish small regressions or hardware-level bandwidth/occupancy attribution.

The light-volume implementation dispatches a fixed world-space volume, with
iteration count determined by eligible light radius. It has no subdivision
input. Its measured variation cannot be attributed directly to a subdivision
multiplier. Independent clock/power controls and GPU hardware profiling remain
necessary before such an attribution.

## Rejected experiment

Moving per-axis face-axis rejection ahead of face selection/fog work preserves
the axis invariant, but did not meaningfully improve the animated 64³, yaw 45°,
zoom 4 case: three-run frame means 22.470 → 22.377 ms, with overlapping ranges.
Storage was 5.086 → 5.053 ms and finalization 2.929 → 2.947 ms. The four shader
edits were restored and the original shaders rebuilt. Both experiment reports
are retained in the evidence directory; the candidate shader is not shipped.

## Capture checks

Two 15-pose sweeps cover solid 16³/zoom 4 and 64³/zoom 1 blocks. The attached
40° captures show approximately matched extents with different voxel resolution.
They illustrate the workload; profile measurements use fixed 45° runs rather
than screenshot timing. The default screenshot table overrides zoom/yaw, so
these captures use `--yaw-ramp --yaw-ramp-wave` to retain the requested zoom.
The 64³ screenshot also exposes existing HUD text clipping; retain that as a
diagnostic-display follow-up rather than treating its text as numerical evidence.

## Next work

The focused review found identical per-axis taps from both triangle lanes of
each face, including duplicate overflow records. Test one lane per stored face,
preserving both lanes in actual cardinal/detached triangle rasterization. Then
measure occupied cells, overflow population/capacity and scratch bytes. The
canonical overflow sort uses a capacity-derived CPU dispatch grid and stage
schedule; its kernels prune work to the live-count span. Measure both launch
cost and useful work in frozen displaced scenes.

Runtime configuration is retained, but automatic fingerprints currently cover
binary and shaders only. Extend provenance to runtime scripts/configuration
before comparing independently modified demo assets.
