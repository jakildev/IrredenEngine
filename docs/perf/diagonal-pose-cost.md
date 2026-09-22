# The exact diagonal: a band around 45°

The pinned full-turn sweep
([continuous-yaw-sweep.md](continuous-yaw-sweep.md)) has two special poses:
the frame after a cardinal, which the parked per-axis set removes, and the
exact diagonal, where the sweep that lands on 45° reads 54 to 57 ms against
about 41 at its neighbours and the overflow lane peaks at 2,208,000 entries
against 971,724. This document measures the diagonal as a static pose: how
wide it is, what carries it, and which of the pipeline's counters moves with
it.

## Method

Five static poses around 45°, pivot pinned, `IRPerfGrid --wave-freeze
--no-overlay --auto-profile 300 --config-preset configs/perf/million.lua --yaw
<radians> --pivot-origin` (stage profiling on, the million scene: 100³
entities, 128³ pool, zoom 4, frozen wave), Release, the binary `18ab1d21…`
of the parked-set change with shaders `6a13afcaf26f7615`, through
`repeat_profile.py` one run at a time, round-robin over the five poses, two
rounds, so host drift lands on every pose. Apple M4 Max, Metal, battery power
falling from 64% to 59%, host load 3.5 to 4.4 with the fleet live: no
millisecond here is a reference, and the second round ran slower across the
board. Every run's witness reads its pose to 0.01° with `explicit focus on
300 of 300 frames` and nothing dropped. Reports and manifests are under
[diagonal-pose-cost/](diagonal-pose-cost/), one directory per pose and round.

| Pose | Steady mean ms, round 1 / 2 | Steady p99 ms | GPU frame envelope ms | Overflow lane max entries | Visible candidates (cull mean) | Per-axis entries (cull mean) |
|---|---:|---:|---:|---:|---:|---:|
| 44.4° | 43.94 / 44.03 | 45.7 / 47.5 | 30.33 / 30.32 | 578,826 | 909,387 | 2,728,161 |
| 44.9° | 52.80 / 55.03 | 56.6 / 58.1 | 37.64 / 39.18 | 1,751,439 | 909,377 | 2,728,131 |
| 45.0° | 60.55 / 68.54 | 63.9 / 73.5 | 43.60 / 49.24 | 2,208,000 | 909,358 | 2,728,074 |
| 45.1° | 52.91 / 60.58 | 57.5 / 67.8 | 37.18 / 42.27 | 1,712,278 | 909,377 | 2,728,131 |
| 45.6° | 44.21 / 48.85 | 51.8 / 59.0 | 30.01 / 32.59 | 541,449 | 909,402 | 2,728,206 |

The lane count and the cull means are identical in both rounds of every pose.

## What it shows

1. **The diagonal is a band, not a pose.** A tenth of a degree either side of
   45° already costs 9 to 11 ms over 44.4° (52.8 and 52.9 ms in the first
   round against 43.9), and the exact diagonal 16.6 ms more in the first round
   and 24.5 in the second (60.6 and 68.5 against 44.0). The two shoulders read
   alike in count and time, so the band is symmetric about 45°. The 1.2° a
   frame sweep steps over it (44.4° and 45.6° are ordinary frames); a turn
   slow enough to spend frames inside a tenth of a degree of a diagonal pays
   the band on each of the four diagonals. How far the band extends past
   ±0.1° is not measured here.
2. **The GPU carries it.** The frame envelope goes 30.3 → 37.6 → 43.6 →
   37.2 → 30.0 ms across the five poses in the first round, the same shape
   as the frame time, and the CPU systems move by tenths of a millisecond
   (`SingleVoxelToCanvasFirst` 4.76 to 5.10, `BuildLightOcclusionGrid` 1.58 to
   1.78).
3. **One counter moves with it: the overflow lane.** Visible candidates
   (909,358 to 909,402) and the per-axis face entries (2,728,074 to
   2,728,206) are the same to within 50 at every pose, so the faces the
   per-axis store walks do not change. The lane, which carries the faces
   that are view-visible and not their cardinal cell's winner, holds 578,826
   entries at 44.4°, 1,751,439 a tenth of a degree from the diagonal and
   2,208,000 on it, 3.8× the off-band count and 26% of the 8,388,608 cap.
4. **The stage rows are not additive here.** Per sampled invocation, the
   three stages that read the lane rise with it between 44.4° and 45.0°: the
   overflow append and sort (`voxelPerAxisOverflow`, 7.4 → 17.7 ms), the
   scatter that draws the entries (`perAxisScatter`, 2.0 → 7.4) and the
   overflow relight (`lightingOverflow`, 12.4 → 17.5). So do two stages that
   never read it, the light volume (24.9 → 35.7) and the per-axis AO (22.9
   → 33.4), and the rows sum to 87 ms at 44.4° and 130 at 45.0° against
   envelopes of 30 and 44 to 49, so the rows carry queueing behind each other
   and say which stages are in the frame, not what each costs
   ([gpu-stage-timing-cost-model.md](../design/gpu-stage-timing-cost-model.md)).
   The attribution this document supports is by the lane's population and
   by the stages that consume it; how the 13 to 19 ms of envelope split
   between the sort, the scatter and the relight is the separating
   experiment below.

| GPU stage, avg ms per sampled invocation (mean of both rounds) | 44.4° | 44.9° | 45.0° | 45.1° | 45.6° | 45.0 − 44.4 |
|---|---:|---:|---:|---:|---:|---:|
| computeLightVolume | 24.89 | 28.30 | 35.69 | 29.53 | 25.97 | +10.80 |
| computeVoxelAoPerAxis | 22.87 | 26.17 | 33.44 | 27.27 | 23.76 | +10.57 |
| voxelPerAxisOverflow | 7.44 | 10.82 | 17.74 | 11.38 | 8.10 | +10.30 |
| perAxisScatter | 1.95 | 6.57 | 7.38 | 6.85 | 2.01 | +5.43 |
| lightingOverflow | 12.44 | 13.79 | 17.49 | 14.23 | 12.71 | +5.05 |
| trixelToFb | 0.04 | 0.30 | 0.32 | 0.28 | 0.04 | +0.28 |
| voxelPerAxisStore | 5.08 | 5.04 | 5.26 | 5.34 | 5.22 | +0.18 |
| voxelPerAxisFinalize | 10.15 | 10.11 | 10.23 | 10.35 | 10.23 | +0.07 |
| voxelSunFaces | 1.95 | 2.00 | 1.77 | 1.79 | 1.75 | −0.18 |
| sum of the sampled stages | 87.39 | 103.71 | 129.96 | 107.66 | 90.40 | +42.57 |

(Stages under 0.2 ms at every pose are left out.)

## A hypothesis, not established

The lane admits a face when its quantized yawed depth lies within
`kOverflowDepthEpsSteps` (8 steps, about half a world unit) of its view-mask
cell's winner and it is not its own cardinal cell's store winner
([per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ The overflow lane). At 45° the X and Y face axes project to equal screen
extents and coset members tie in yawed depth, so the epsilon would admit far
more of them than a tenth of a degree away, where the ties spread; that would
give the sharp peak on 45.0° over a shoulder that is itself high. Whether the
entries on the diagonal are exact ties or near-ties inside the epsilon, and
whether they are drawn behind cell quads that already cover their pixels, is
what the counters below would say. It is a reading of the lane's admission
rule, not a measurement.

## Next measurements

1. The separating experiment: the sort-disabled and overflow-albedo-only
   probes of [gpu-cost-attribution.md](gpu-cost-attribution.md) § Method at
   45.0° against 44.4°, interleaved, so the 13 to 19 ms of envelope is split
   between the append and sort, the scatter and the relight by full-frame
   controls rather than by stage rows.
2. The band's width: 44.6°, 44.8°, 45.2° and 45.4° between the poses above,
   and the same five poses about 135°.
3. A histogram of the lane's entries by their depth distance from the
   view-mask winner (0 steps, 1 to 8 steps) at 45.0° and 44.4°, from a local
   patch that counts them, and the share of overflow quads that lose the
   composite depth test to a cell quad: the count of entries that change no
   pixel is the count that need not be appended.

## What this does not say

- No millisecond here is a reference: battery, the fleet live, and stage
  profiling on (the profiling-off sweep reads 54 to 57 ms on the diagonal
  against 60.6 and 68.5 here). The lane counts are exact; the milliseconds
  are one host on one evening, two runs a pose.
- Which stage owns the extra envelope. The stage rows rise together and sum
  to three times the envelope.
- Why the lane grows: the admission rule reading above is a hypothesis.
