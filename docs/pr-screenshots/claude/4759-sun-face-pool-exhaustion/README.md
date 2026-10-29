# Sun record-pool exhaustion: native Metal controls

A rigid source plate (`--probe-record-pool-filler`) forces the global
65,536-record sun face pool to overflow without crowding the receiver's tiles.
Creating it **before** the shadowbox casters makes their bake dispatches lose
every record; creating it **after** (`--probe-record-pool-filler-last`) loses
only filler records. Each case runs twice from one binary: `parent` stages
origin/master's four sun-index shaders, `head` uses this branch's.

| Parent: retained receiver records, pool exhausted elsewhere | Head |
|---|---|
| ![parent](parent/far-128-filler-last/receiver.png) | ![head](head/far-128-filler-last/receiver.png) |
| ![parent oblique](parent/oblique-aligned-128-filler-last/receiver.png) | ![head oblique](head/oblique-aligned-128-filler-last/receiver.png) |
| ![parent moving](parent/moving-128-filler-last/receiver-3.png) | ![head moving](head/moving-128-filler-last/receiver-3.png) |

Crops are rectangle `(1100,600)-(1460,840)` of native 2560×1440 frames; the
full frames and gzipped logs sit beside them for the key cases. Before this
change, any record exhaustion disabled the overflow hierarchy for the whole
frame, so 128 distant casters erased the lower shadow even though every
receiver record was stored.

## Results

All 82 captures exited CLEAN. "Outside band" counts changed pixels more than
2 px from an edge of the control crop; missing = brighter than control,
excess = darker. [captures.json](captures.json) retains commands, binary,
shader, frame and crop hashes, and the per-cascade tile counters of every
shot; `index.csv.gz` keeps the full tile dump for three head cases.

### Static casters

| Case | Records requested | Lost-record tiles near/far (head) | Parent missing / excess outside band | Head missing / excess outside band |
|---|---:|---:|---:|---:|
| far-128 (tile overflow, complete pool) | 5,164 | 0 / 0 | 0 / 0 | 0 / 0 |
| lower + filler last | 68,014 | 347 / 502 | 0 / 0 | 0 / 0 |
| lower + filler first | 68,014 | 261 / 373 | 0 / 0 | 0 / 0 |
| **far-128 + filler last** | 71,470 | 486 / 694 | **200** / 0 | **0** / 0 |
| far-128 h30 + filler last ¹ | 71,470 | 521 / 727 | 0 / 0 | 0 / 0 |
| **far-128 h90 + filler last** ¹ | 71,470 | 526 / 690 | **200** / 0 | **0** / 0 |
| far-128 + filler first 270 (under capacity) | 63,970 | 0 / 0 | 0 / 0 | 0 / 0 |
| far-128 + filler first 280 (straddles capacity) | 66,420 | 18 / 10 | 200 / 0 | 200 / 0 |
| far-128 + filler first | 71,470 | 312 / 463 | 200 / 0 | 200 / 0 |
| **oblique-aligned-128 + filler last** ² | 72,188 | 502 / 730 | **228** / 0 | **0** / 0 |
| oblique-aligned-128 + filler first ² | 72,188 | 215 / 382 | 228 / 0 | 228 / 0 |

¹ Graded against the same-height caster scene without a filler, whose nearby
h30 casters legitimately shadow the receiver. h30 differs from its control by
88 parent pixels, all inside the band. ² Graded against `oblique-lower`; the
oblique filler needs side 340 to exhaust the pool.

Lost-record tile counts vary between identical runs (a full rerun moved
`far-128 + filler last` from 515 / 692 to 486 / 694): failed reservations race
across parallel bake workgroups. Records requested, every crop grade and every
moving pose repeated exactly.

### Mixed finite, analytic and raster-only casters

The `analytic-*` cases replace the lower box with an analytic sphere at
subdivisions 2, where it indexes a typed sun-ray record (record type 1 in a
`--sun-face-geometry-probe` dump; at subdivisions 1 it writes none). The 128
casters stay finite and the GRID floor stays raster-only. Controls are the
sphere without the casters.

| Case | Records requested | Lost-record tiles near/far (head) | Parent missing / excess | Head missing / excess |
|---|---:|---:|---:|---:|
| analytic-far-128 (tile overflow, complete pool) | 5,057 | 0 / 0 | 0 / 0 | 0 / 0 |
| analytic-far-128 + filler last | 71,363 | 527 / 697 | 200 / 0 | 200 / 0 |
| analytic-far-128 + filler first | 71,363 | 255 / 395 | 200 / 0 | 200 / 0 |
| analytic-oblique-aligned-128 + filler last | 71,889 | 542 / 757 | 240 / 0 | 240 / 0 |

Creation order does not protect the sphere. Analytic shapes bake in
`SHAPES_TO_TRIXEL`, after every voxel canvas, so under exhaustion their
records are lost first: a geometry dump of both `filler last` and
`filler first` retains 65,536 records, all finite (type 0). Every incomplete
receiver tile then carries the lost-record bit, so this is the lost-own-record
limit below, with the loss decided by bake stage rather than creation order.

### Moving casters

Each track moves the 128-caster stack along +X at 120 units/s through the
engine's `VELOCITY_3D`: 2 units per fixed 60 Hz UPDATE tick, and auto-capture
runs one tick per frame. It runs 7 shots, 10 frames apart,
from x = -62 to +58 (cardinal) or -44 to +76 (oblique), crossing over the
receiver at shot 3. Each shot is graded against a static capture at the pose
the shot logged (`PROBE-DRIFT`). Without a filler, every shot of both tracks
is pixel-identical to its static pose control in both variants, so motion
leaves no stale-frame residue.

| Track (shots 0–6) | Records requested | Parent missing at shot 3 (others) | Head missing, all shots |
|---|---:|---:|---:|
| moving-128 | 5,164 | 0 (0) | 0 |
| **moving-128 + filler last** | 71,470 | **220** (0) | **0** |
| moving-128 + filler first | 71,470 | 220 (0) | 220 at shot 3, else 0 |
| oblique-moving-128 | 5,676 | 0 (0) | 0 |
| **oblique-moving-128 + filler last** | 72,188 | **228** (0) | **0** |

Excess shadow is 0 in every row.

### Reading the results

- **Repaired:** whenever the receiver's records were retained, head matches
  its sufficient-capacity control exactly, static or moving: 0 changed pixels
  in every `filler last` row with finite casters. Parent loses 200–228 shadow
  pixels in five of them.
- **Unchanged limit:** when the receiver's own records are lost (filler first,
  the 280 straddle, or an analytic sphere baked after every canvas), parent
  and head produce identical crops with the lower shadow's center missing.
  Those blockers exist only in the nearest-depth sampled layer, where the
  distant casters' depth hides them.
- `lower + filler first` differs from `lower` by 192 pixels, all inside the
  band: sampled fallback edges, not lost coverage.
- Capacity-boundary sweep: 270 → 280 → 300 moves the receiver from complete
  (0 lost tiles) to partially lost to fully lost; the hole appears exactly
  when the lower shadowbox's records are lost.

## Lost-record tiles and IRPerfGrid

A tile carrying the lost-record bit answers from the sampled layers and never
traverses the retained hierarchy. Letting it traverse for hits alone was
measured first: IRPerfGrid's 262,144-voxel grid exhausts the pool every frame
and nearly every tile loses records, so each receiver walked the retained
pool. [perf-grid/](perf-grid/) retains every report; `perf_grid.py` reruns
it. One 60-frame `--auto-profile` run per cell, all `CLEAN`, on a shared
host without the benchmark quiet guard, so read them as direction:

| Sun shaders | zoom 1 steady frame | zoom 1 `computeSunShadow` / `computeLightVolume` GPU | zoom 4 steady frame | zoom 4 `computeSunShadow` / `computeLightVolume` GPU |
|---|---:|---:|---:|---:|
| origin/master | 21.35 | 0.070 / 2.644 | 23.89 | 0.050 / 5.811 |
| lost tiles traverse for hits (`1fdb66458`) | 24.16 | 4.901 / 6.169 | 41.40 | 15.846 / 20.705 |
| **head: lost tiles answer from the sampled layers** | **21.21** | **0.074 / 2.602** | **23.44** | **0.051 / 6.103** |

All values are ms. The hierarchy build (`bakeSunShadowMap`) now runs on every
exhausted frame and costs 0.035–0.092 ms here against 0.006–0.021 ms on master.

## Timing (separate runs)

Steady-state profile, 360 frames, one run per row, parent/head/parent order.
[timing/](timing/) retains every summary and manifest. No capture or index
probe ran.

| Case | Parent-a frame ms | Head frame ms | Parent-b frame ms | entityCanvasToFb ms (a / head / b) | bakeSunShadowMap ms (a / head / b) |
|---|---:|---:|---:|---:|---:|
| far-128 | 72.99 | 73.41 | 73.38 | 6.567 / 6.763 / 6.718 | 0.020 / 0.020 / 0.020 |
| far-128 + filler last | 102.73 | 103.88 | 104.60 | 6.824 / 8.459 / 6.926 | 0.008 / 0.036 / 0.009 |
| far-128 + filler first | 104.74 | 104.62 | 107.53 | 6.985 / 7.076 / 7.083 | 0.007 / 0.039 / 0.010 |

Head's steady frame average is inside the parent spread for both exhausted
cases and 0.03 ms above it without exhaustion, where the shaders take the
same branches as the parent. When
the pool exhausts, the hierarchy build now runs (~0.03 ms). In the repaired
case, exact queries over the retained pool replace the parent's incorrect
sampled answer on the overflowed receiver tiles, costing about 1.6 ms of
composite GPU time. With the receiver's records lost (filler first), the
composite matches the parent.

The sun buffer layout and size are unchanged at 30,670,864 bytes, and
production frames gain no readback.

## Replay and limits

```bash
ir-acquire gpu -- python3 docs/pr-screenshots/claude/4759-sun-face-pool-exhaustion/capture.py --output /tmp/pool-exhaustion
python3 docs/pr-screenshots/claude/4759-sun-face-pool-exhaustion/timing.py --output /tmp/pool-exhaustion-timing
python3 docs/pr-screenshots/claude/4759-sun-face-pool-exhaustion/perf_grid.py --output /tmp/pool-exhaustion-grid --ref origin/master
```

All three drivers stage GLSL instead of Metal off macOS, so the same commands
run on Windows/OpenGL. Captured on Apple M4 Max, macOS, Metal Debug.
**Windows/OpenGL captures were not run on this host.** The CPU shader adapters
execute both backends' sources, not the OpenGL driver. Capture frames
synchronize the GPU and are not timing evidence.
