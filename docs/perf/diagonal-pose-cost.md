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
millisecond here is a reference. The second round read the same at 44.4°
(44.03 against 43.94 ms) and 2 to 8 ms slower on the band poses, at a lower
host load there (3.5 against 4.4), so its excess sits where the GPU works
hardest; on battery that reads as a GPU power or thermal response rather than
host drift, and nothing here decides it. Every run's witness reads its pose
to 0.01° with `explicit focus on
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
   45° already costs 9 ms over 44.4° in the first round (52.8 and 52.9
   against 43.9) and 11.0 and 16.6 in the second (55.0 and 60.6 against
   44.0), and the exact diagonal costs 16.6 ms over 44.4° in the first round
   and 24.5 in the second (60.6 and 68.5). The two shoulders read alike in
   count (1,751,439 and 1,712,278, 2% apart) and in the first round's time;
   the second round's shoulders differ by 5.5 ms. The 1.2° a frame sweep
   steps over the band (44.4° and 45.6° are ordinary frames) and shows the
   exact pose costing the same at all four diagonals; the band's width was
   measured at 45° only, and how far it extends past ±0.1° is not measured.
2. **The GPU envelope carries about four fifths of it.** The envelope goes
   30.3 → 37.6 → 43.6 → 37.2 → 30.0 ms across the five poses in the first
   round, the same shape as the frame time: 13.3 of the 16.6 ms rise in the
   first round and 18.9 of the 24.5 in the second. The rest is the
   fixed-update catch-up a longer frame admits, 2.65 → 3.6 and 4.0 updates a
   frame at about 2.4 ms each (the sweep doc's finding 4 is the same
   mechanism). Per call the CPU systems move by tenths (means of both rounds:
   `SingleVoxelToCanvasFirst` 4.76 to 5.10, `BuildLightOcclusionGrid` 1.58 to
   1.78).
3. **One counter moves with it: the overflow lane.** Visible candidates
   (909,358 to 909,402, a spread of 44) and the per-axis face entries
   (2,728,074 to 2,728,206, a spread of 132, 0.005%) are the same at every
   pose, so the faces the per-axis store walks do not change. The lane, which
   carries the faces that are view-visible and not their cardinal cell's
   winner, holds 578,826 entries at 44.4°, 1,751,439 a tenth of a degree from
   the diagonal and 2,208,000 on it, 3.8× the off-band count and 26% of the
   8,388,608 cap. The cost is not linear in that count: the frame rises 7.6
   to 7.9 ms per million entries from 44.4° to the shoulders and 17 ms per
   million from a shoulder to the peak (the envelope 6.2 and 13), which is
   what the sort's span in the next section predicts.
4. **The stage rows are not additive here.** Per sampled invocation, the
   three stages that read the lane rise with it between 44.4° and 45.0°: the
   overflow append and sort (`voxelPerAxisOverflow`, 7.4 → 17.7 ms), the
   scatter that draws the entries (`perAxisScatter`, 2.0 → 7.4) and the
   overflow relight (`lightingOverflow`, 12.4 → 17.5). So do two stages that
   never read it, the light volume (24.9 → 35.7) and the per-axis AO (22.9
   → 33.4), and the rows sum to 87 ms at 44.4° and 130 at 45.0° against
   envelopes of 30 and 44 to 49: a tagged scope spans its first encoder's
   start to its last encoder's end and the per-axis loops interleave, so
   overlapping rows do not sum and say which stages are in the frame, not
   what each costs
   ([gpu-stage-timing-cost-model.md](../design/gpu-stage-timing-cost-model.md)).
   The attribution this document supports is by the lane's population, by
   the span the sort encodes from it, and by the stages that consume it; how
   the 13 to 19 ms of envelope split between the sort, the scatter and the
   relight is the separating experiment below.

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
| sum over every sampled stage | 87.39 | 103.71 | 129.96 | 107.66 | 90.40 | +42.57 |

(Stages under 0.2 ms at every pose are left out of the rows; the sum
includes them.)

## Two readings, one of them checkable in code

**The sort's encoded span is a staircase in the lane count.**
`detail::overflowSortDispatchSpan` in `system_voxel_to_trixel.hpp` opens
`min(cap, max(block, nextPowerOfTwo(lagged) × 2))` merge-stage encoders,
with a count above half the cap opening the whole cap. From the counts
above that is 2,097,152 at 44.4° and 45.6°, 4,194,304 a tenth of a degree
from the diagonal and 8,388,608, the full cap, on it: the span doubles at
each step while the count grows 3.0× and then 1.26×. The `voxelPerAxisOverflow`
row (7.44 → 10.82 → 17.74 ms) fits a constant append of about 4.6 ms plus a
sort term proportional to the span and its merge stages to within a
millisecond at all three levels, and the doubling of the marginal cost per
entry from shoulder to peak (finding 3) is what a span that doubles across
a count that grows a quarter predicts. This is a pure function of the count
in code and needs no patch to test: two poses whose counts sit just under
and just over 2,097,152 (or 4,194,304) would cost the same lane and a
different span. The separating experiment's sort-off arm is read against it.

**Why the lane grows toward 45° is not established.** The lane admits a
face when its quantized yawed depth lies within `kOverflowDepthEpsSteps`
(8 steps, about half a world unit) of its view-mask cell's winner and it is
not its own cardinal cell's store winner
([per-axis-trixel-canvas-rotation.md](../design/per-axis-trixel-canvas-rotation.md)
§ The overflow lane). The cardinal store's winner and the yawed view's winner
disagree most where the residual yaw is largest, so a growing lane toward
45° is expected; the sharp step from 1.75 million a tenth of a degree away
to 2.21 million on the pose is not explained by that alone. It is not a
coset tie: the design doc places coset ties at the 120° and 240° depth
degeneracy, and the nearest coset pair separates by about 2.7 world units
of yawed depth, more than the epsilon. Whether the extra entries at 45.0°
are near-ties inside the epsilon among faces of one cardinal column, or
something else, is what a histogram of entries by depth distance from the
mask winner would say.

## The separating experiment

The three consumers of the lane switched off one at a time, at 45.0° against
44.4°, each arm a full frame: the overflow scatter draw off
(`IR_PERAXIS_OVERFLOW_DISABLE`, the composite's existing kill switch), the
overflow relight off (`IR_OVERFLOW_LIGHTING_DISABLE`, likewise) and the
overflow sort off ([sort-off.patch](diagonal-pose-cost/sort-off.patch): the
overflow `sortFaceRecords` call in `dispatchPerAxisCanvases` is skipped, the
append and the source-face sort are kept; built into the binary `e83ce0cf…`,
the other arms run the unpatched `18ab1d21…`). The sort-off manifests report
a clean tree under `changes` because the patch was applied and reverted
around the build; the patch file is the record. Every probe changes the
picture and none is an optimization. Same preset, pivot and scene as above;
two rounds, the arms in a fixed order (control, scatter off, relight off,
sort off) at 45.0° and then at 44.4°. The fleet loaded the host during the
run: the first round ran at `host_load_1m` 4.0 to 5.0 at 45.0° and 7.1 to 19
at 44.4° (the relight-off arm at 19), the second at 6.0 to 9.7, and the
battery fell from 52% to 35%. Reports under
[diagonal-pose-cost/](diagonal-pose-cost/) as `split-<arm>-<pose>-<round>/`.

| Arm | Pose | Steady mean ms, r1 / r2 | GPU envelope ms, r1 / r2 | Overflow entries | `voxelPerAxisOverflow` | `perAxisScatter` | `lightingOverflow` | Load, r1 / r2 |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| control | 45.0° | 59.57 / 93.50 | 43.80 / 70.54 | 2,208,000 | 21.57 | 9.40 | 20.50 | 5.0 / 9.1 |
| scatter off | 45.0° | 53.42 / 80.25 | 37.80 / 51.81 | 2,208,000 | 19.80 | 0.40 | 19.28 | 4.7 / 9.7 |
| relight off | 45.0° | 59.89 / 68.63 | 44.19 / 52.21 | 2,208,000 | 18.43 | 7.55 | absent | 4.3 / 7.7 |
| sort off | 45.0° | 51.66 / 51.10 | 36.19 / 36.66 | 2,208,000 | 6.26 | 8.03 | 24.94 | 4.0 / 9.0 |
| control | 44.4° | 47.41 / 45.77 | 33.21 / 31.96 | 578,826 | 8.20 | 2.11 | 12.95 | 7.9 / 7.8 |
| scatter off | 44.4° | 71.22 / 43.72 | 45.66 / 30.26 | 578,826 | 10.38 | 0.40 | 15.58 | 7.1 / 6.9 |
| relight off | 44.4° | 155.64 / 46.00 | 38.93 / 31.97 | 578,826 | 8.58 | 2.16 | absent | 19.3 / 6.7 |
| sort off | 44.4° | 65.42 / 42.78 | 45.15 / 29.57 | 578,826 | 7.59 | 2.52 | 30.04 | 9.1 / 6.0 |

(Stage rows are the mean of both rounds, ms per sampled invocation, dropped
cells included; the relight-off reports carry no `lightingOverflow` row at
all, since the scope never opens.)

- **Every probe fired, and the lane is unchanged by all three.** In the
  cells read below, the scatter row falls from 6.66 to 0.36 ms (the
  cell-path draw remains), the relight row is absent, the row that holds the
  sort falls from 16.34 to 5.99 (the append remains), and the lane's count
  is 2,208,000 and 578,826 in every arm, so the probes removed consumers and
  not the population.
- **Which cells are read.** The lower-load round at each pose: the first
  round at 45.0° (loads 5.0, 4.7, 4.3, 4.0) and the second at 44.4° (7.8,
  6.9, 6.7, 6.0). That is a per-round choice, because the four arms of one
  round share the host's drift, and not a load cut: the dropped cells ran at
  7.1 to 19, overlapping the kept 44.4° round, and read 0.99 to 1.63 times
  their kept twin, one of them 3.4 times (relight off at 44.4°, load 19). The
  kept 44.4° control reads 45.77 ms and 31.96 of envelope against the band
  table's 43.94 to 44.03 and 30.3 on the same binary an hour earlier at
  load 4, so this section's 44.4° baseline is about 1.6 ms high and its
  diagonal excess is 11.8 ms of envelope (43.80 against 31.96) where the
  band table reads 13 to 19.
- **What those cells read.** At 45.0° the envelope falls 7.6 ms with the
  sort off and 6.0 with the scatter off, and rises 0.4 with the relight off;
  at 44.4° it falls 2.4 and 1.7, and moves 0.0. Net of the off-band deltas,
  the sort's share of the 11.8 ms excess reads about 5 ms and the scatter's
  about 4, and the relight's nothing the envelope can see although its row
  is 16.7 ms on the diagonal, which reads as a pass that overlaps others, or
  a scope that brackets a queue wait. Three things make these net readings
  and not shares. The arm order is fixed and the load fell through both
  rounds read (5.0 → 4.0 and 7.8 → 6.0), so the sort-off arm always ran on
  the quietest host and the control on the busiest, a bias in the sort's
  favour. With the sort off the other two consumers slow down
  (`lightingOverflow` 16.7 → 24.8 at 45.0° and 12.9 → 23.8 at 44.4°,
  `perAxisScatter` 6.7 → 7.8), so the sort-off delta is net of what unsorted
  records cost the relight and the scatter. And
  [gpu-cost-attribution.md](gpu-cost-attribution.md) § Results holds a
  sort-disabled arm at 45° at −4.07 ms of GPU frame, in Debug, grouped rather
  than interleaved, with a probe that kept the sort's argument generation and
  sentinel fill: the two deltas were taken under different builds, probes and
  host conditions and are not yet comparable.
- **This is a reading, not a finding.** One round per cell, on a host the
  fleet was loading, the two poses' cells from different rounds, in an order
  that favours the last arm. It says where to look (the sort's merge network
  and the scatter's instanced draw over 2.2 million quads) and not by how
  much; the same sixteen arms on a quiet host (issue 3638 in the tracker),
  in reversed or shuffled arm order, are owed before any number here is
  quoted as the split.

## Next measurements

1. The separating experiment above on a quiet host: the same sixteen arms
   in reversed or shuffled arm order, so the sort's and the scatter's shares
   of the diagonal's envelope excess are numbers and not a reading.
2. The band's width: 44.6°, 44.8°, 45.2° and 45.4° between the poses above,
   and the same five poses about 135°; and the staircase test, two poses whose
   lane counts bracket 2,097,152 entries, which cost the same lane and a
   different sort span.
3. A histogram of the lane's entries by their depth distance from the
   view-mask winner (0 steps, 1 to 8 steps) at 45.0° and 44.4°, from a local
   patch that counts them, and the share of overflow quads that lose the
   composite depth test to a cell quad: the count of entries that change no
   pixel is the count that need not be appended.

## What this does not say

- No millisecond here is a reference: battery, the fleet live, and stage
  profiling on (the profiling-off sweep's frame on the diagonal reads 54 to
  57 ms against 60.6 and 68.5 here, a sweep frame with profiling off against
  a held pose with it on, two differences at once). The lane counts are
  exact; the milliseconds are one host on one evening, two runs a pose.
- Which stage owns the extra envelope. The stage rows rise together and sum
  to three times the envelope; the separating experiment's cells point at
  the sort and the scatter and at nothing the relight does, from one round
  each on a loaded host, in an arm order that favours the sort.
- Why the lane grows toward 45° and steps on the pose: the reading above is
  not a measurement. The sort's span staircase is code, and its cost share
  is the separating experiment's to measure.
