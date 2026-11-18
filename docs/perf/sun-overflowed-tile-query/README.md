# Exact overflowed-tile sun query: Windows/OpenGL attribution

This measures how much of the `entityCanvasToFb` composite the exact
overflowed-tile query costs on current master, on Windows/OpenGL, in Debug and
Release. The exact query is the `queryHierarchy` branch of
`sampleCascadeShadow`, in `ir_sun_shadow_sample.glsl` and its Metal twin.
Receivers in a tile that overflowed but kept every record walk the retained
record pool's hierarchy instead of the tile list.

The macOS/Metal replay of the same controller, at the same source revision,
is in `macos-metal/`. The GL/Metal classification is in
[§ macOS/Metal replay and GL/Metal classification](#macosmetal-replay-and-glmetal-classification).
**The classification is that a residual above 2× remains in both build
types.**

The GL-side mechanism behind that residual is in
[§ GL mechanism: every coincident caster shades the stack](#gl-mechanism-every-coincident-caster-shades-the-stack),
with its evidence in `windows-opengl-mechanism/`. **The query term is the
exact query running in hidden fragments of the 127 caster canvases behind the
first one.**

**Result.** On this host the exact query is a real execution cost, not a
timer artifact:

- **Debug `far-128`:** the query is +61.7 ms of a 62.4 ms composite (98.8%),
  with a one-sided 95% lower bound of +60.0 ms. The steady frame moves by
  +51.8 ms.
- **Synchronized timing:** finish-bracketed wall time moves by the same
  +62.0 ms.
- **Release:** the query is +67.5 ms of a 67.7 ms composite (99.7%), with a
  one-sided 95% lower bound of +60.2 ms. The frame moves by +66.0 ms, and the
  synchronized control moves by +68.1 ms.

Turning the branch off removes nearly the whole composite in every case where
the receiver takes the exact path. In the sampled-fallback control it changes
nothing.

| | |
|---|---|
| Source | `5d93af97c` (master; contains the Morton-ordered hierarchy and record-pool growth) |
| Host | Windows 10 19045, NVIDIA GeForce RTX 3060 Ti, driver 32.0.15.8180, MSYS2 mingw64 GCC, OpenGL |
| Resolution | 1280×720 offscreen, 1× output |
| Binaries | Debug `f3bcc111c3d2…`, Release `ec5a847db78b…` (full hashes in `*/provenance.json`) |
| Pristine sampler | `ir_sun_shadow_sample.glsl` `4bb016193aeb…`; the runtime copies byte-match the tracked source before, between and after every staged launch |
| Controller | `run_matrix.py` `8b76606c2daa…` ran every capture and timed arm in both builds, unchanged (`*/provenance.json`); the committed file differs from it only in the pixel comparison, and re-scored the retained captures (`scored_by_controller_sha256` in `diagnostic.json`) |
| Power / quiet | Desktop on mains (`repeat_profile.py` reports no power source on Windows); all 120 retained runs are quiet-window `guarded` |

## Cases

The first three cases use the historical exhaustion timing fixture's
arguments unchanged. They come from `timing.py`, which is retained on the
`media` branch under `docs/pr-screenshots/claude/4759-sun-face-pool-exhaustion/`.
The fixture has 128 coincident caster boxes 60 units above a grid floor, with
the sun straight down.

Since then the record pool grows to measured demand. As a result the two
filler cases now keep all 71,470 records, and the receiver takes the exact
path in both. The two `-frozen` cases pin the pool at its default 65,536
records (`--sun-record-capacity 65536 --sun-record-ceiling 65536`). This
restores the historical exhaustion, so `far-128-filler-first-frozen` is the
sampled-fallback control.

| Case | Records requested / pool | Receiver tiles (cascade x,y: records) | Route |
|---|---:|---|---|
| `far-128` | 5,164 / 65,536 | 0 76,51: 1,970; 1 64,64: 2,744 (overflowed, kept) | exact hierarchy |
| `far-128-filler-last` | 71,470 / 131,072 | same | exact hierarchy |
| `far-128-filler-first` | 71,470 / 131,072 | same | exact hierarchy |
| `far-128-filler-last-frozen` | 71,470 / 65,536 | same; 683 other tiles lost records | exact hierarchy |
| `far-128-filler-first-frozen` | 71,470 / 65,536 | 0 76,51: 25; 1 64,64: 36 (lost records) | sampled fallback |

## Method

`run_matrix.py` is the controller, and `test_controller.py` holds its
hermetic controls. The controller is backend-neutral. It stages the runtime
sampler twin of the active backend in the build tree:
`shaders/ir_sun_shadow_sample.glsl` on OpenGL hosts and
`shaders/metal/ir_sun_shadow_sample.metal` on macOS. It stages between process
launches and never edits a tracked source.

| Variant | Staged change to the runtime sampler | Used for |
|---|---|---|
| production | none | timed arm, oracle, closing capture |
| ablation | `const bool queryHierarchy = false;` | timed arm |
| entry-shadow | `if (queryHierarchy) return 1.0;` after the decision | entry witness |
| entry-lit | `if (queryHierarchy) return 0.0;` after the decision | entry witness |
| hit-cleared | the hierarchy hit's `return 1.0` becomes `return 0.0` | hit witness |

Each change must match exactly once in the pristine sampler, or the
controller refuses to stage it. The decision and the hit return are the same
text in both twins, and `test_controller.py` checks both. Every staged launch
checks the sampler and the whole runtime `shaders/` set against their pristine
hashes, before and after. It restores the pristine bytes on success, on a
failed child run and on interruption.

**Timing.** In each of five pairs, `timing --build debug|release` runs every
case in both arms, then the synchronized control. That control is `far-128`
with a `--config-preset` made of the runtime `config.lua` plus
`config.gpu_stage_timing_legacy = true`. Odd pairs launch production first
and even pairs launch the ablation first. Each arm is one
`scripts/perf/repeat_profile.py --target IRCanvasStress [--release] --repeats 1`
run: 360 profile frames, offscreen.

Each staged variant also gets one untimed warm launch (`warmup/`), so a cold
NVIDIA shader link never lands inside a timed run. A run that breaches the
quiet window is discarded and repeated, up to eight times. Any other failure
stops the matrix.

An arm is kept only if its manifest matches the declared arm. That means
build type, binary hash, staged shader-set hash, runtime scripts hash, source
revision and fixture arguments. The run must also have exited CLEAN with a
fresh report in a guarded quiet window. Counter arms must carry substage
samples and synchronized arms must not, which proves the preset took effect.

**Diagnostics** (`diagnose`, Debug, untimed). Release compiles out the log
line that witnesses the index probe. For each case the controller takes one
`--sun-face-index-probe` screenshot per variant, using the historical capture
fixture's arguments. The first is a pristine production *oracle*, and the
last is a second pristine production capture.

- **Index.** The probe gives each tile's record count, completeness and lost
  flag, plus the retained records and the ready hierarchy leaves. The
  receiver tile is the tile whose sun-space bounds hold the receiver point
  under the caster stack. Its route follows the sampler's own decision:

  | Receiver tile | Route |
  |---|---|
  | complete | indexed |
  | lost records | sampled fallback |
  | incomplete, records kept, and ready leaves equal `sourceSunHierarchyLeafCount(retained)` (groups of 8, rounded up to a power of two) | exact hierarchy |

Every comparison below is exact RGB over every pixel of the named receiver
crop, with no per-pixel classification.

- **Entered.** Receiver-crop pixels that change against the oracle under
  entry-shadow or entry-lit.
- **Hit.** Receiver-crop pixels that change under hit-cleared.
- **Correctness.** The closing production capture against the oracle,
  with zero mismatches required in the receiver crop.

`run_matrix.py score --build <build>` re-runs these comparisons over a
retained diagnostic's captures without capturing again. It refuses a frame
whose bytes no longer hash to what its capture recorded.

**Statistics.** `summarize` pairs each production run with the ablation run
of the same pair. It reports:

- both arms' means and spreads;
- the paired production-minus-ablation mean and its standard deviation;
- the one-sided 95% lower and upper bounds `mean ∓ t(0.95, n−1)·sd/√n`;
- the share, `delta / production mean`.

## Positive fire and pixel witness (Debug, untimed)

The data is in `windows-opengl/debug/diagnostic/diagnostic.json`. The
historical 1× crop `(550,300)-(730,420)` holds two things: the receiver floor
with the stack's shadow, and above it the 128 coincident caster boxes. Those
boxes z-fight differently on every run, so 528–692 of their pixels differ
between any two captures, including two pristine ones. The controller splits
that column at row 356:

- **Receiver crop** `(550,356)-(730,420)`, the floor band. Correctness and
  the entry/hit witnesses are read here, at exact RGB on every pixel.
- **Stack crop** `(550,300)-(730,356)`, the caster boxes. Their mismatches
  are reported as `stack_changed_vs_oracle` and kept out of every verdict.

No z-fight mismatch falls inside the receiver crop in any of the 25 captures.
In the historical crop the caster mismatches end at row 355, and every pixel a
branch-forcing variant changes lies in rows 356–374. The entered and hit
counts therefore stay the same in every case, whatever the stack does.

| Case | Retained records | Ready leaves = leaf count | Entered px (entry-shadow ∪ entry-lit) | Hit px (hit-cleared) | Closing production vs oracle, receiver crop (exact RGB) | Stack crop, z-fight px |
|---|---:|---|---:|---:|---:|---:|
| `far-128` | 5,164 | 1,024 = 1,024 | 371 | 234 | 0 | 624 |
| `far-128-filler-last` | 71,470 | 16,384 = 16,384 | 371 | 234 | 0 | 600 |
| `far-128-filler-first` | 71,470 | 16,384 = 16,384 | 371 | 234 | 0 | 588 |
| `far-128-filler-last-frozen` | 65,536 | 8,192 = 8,192 | 371 | 234 | 0 | 692 |
| `far-128-filler-first-frozen` | 65,536 | 8,192 = 8,192 | **0** | **0** | 0 | 588 |

What the table shows:

- **Every receiver shadow pixel is a hierarchy hit.** The receiver's shadow is
  234 floor pixels, and every one of them is a hierarchy hit: clearing the hit
  lights all 234, and so does skipping the branch.
- **The query also covers lit floor.** Forcing entry to shadow darkens another
  137 lit floor pixels that the query certified as unoccluded.
- **The frozen filler-first control never enters the branch.** Its receiver
  tiles lost records, so it answers from the sampled layers.
- **Pristine captures are stable on the receiver.** All 25 captures exited
  CLEAN. In every case the closing pristine capture matches the opening oracle
  at exact RGB on every pixel of the receiver crop.
- **The rest of the frame is not a correctness signal.** Whole-frame
  mismatches (`frame_changed_vs_oracle`) are 624 for `far-128` and about
  47,000 for the filler cases, whose 300 coincident filler bodies z-fight the
  same way. They are reported, never compared.

## Timing

Each row has five interleaved pairs, and every run is quiet-window `guarded`
and CLEAN. "Delta" is production minus ablation, paired by launch order. The
bounds are one-sided 95% (`mean ∓ t·sd/√n`). Full tables, including every
other GPU stage row, are in `windows-opengl/<build>/summary.md` and
`summary.json`.

### Debug

| Case | Row | Production ms | Ablation ms | Delta ms (95% lower / upper) | Share |
|---|---|---:|---:|---:|---:|
| `far-128` | `entityCanvasToFb` (357–359 samples) | 62.443 (60.62–65.04) | 0.764 | +61.68 (+60.02 / +63.34) | 98.8% |
| | steady frame | 88.65 | 36.83 | +51.82 (+48.87 / +54.77) | 58.5% |
| `far-128-filler-last` | `entityCanvasToFb` | 48.628 (45.14–56.25) | 0.919 | +47.71 (+43.30 / +52.12) | 98.1% |
| | steady frame | 84.63 | 83.03 | +1.61 (−3.06 / +6.27) | 1.9% |
| `far-128-filler-first` | `entityCanvasToFb` | 46.030 (43.78–48.18) | 0.927 | +45.10 (+43.41 / +46.80) | 98.0% |
| | steady frame | 81.78 | 77.61 | +4.17 (−1.48 / +9.82) | 5.1% |
| `far-128-filler-last-frozen` | `entityCanvasToFb` | 46.619 (45.14–48.39) | 0.871 | +45.75 (+44.56 / +46.94) | 98.1% |
| | steady frame | 81.51 | 83.64 | −2.13 (−5.09 / +0.82) | −2.6% |
| `far-128-filler-first-frozen` | `entityCanvasToFb` | 0.964 | 0.912 | +0.05 (+0.00 / +0.10) | 5.4% |
| | steady frame | 80.87 | 79.85 | +1.02 (−2.62 / +4.66) | 1.3% |
| `far-128`, synchronized | `entityCanvasToFb` (360) | 64.303 (62.75–65.45) | 2.309 | +61.99 (+61.06 / +62.93) | 96.4% |
| | steady frame | 101.69 | 35.96 | +65.73 (+64.72 / +66.74) | 64.6% |

### Release

| Case | Row | Production ms | Ablation ms | Delta ms (95% lower / upper) | Share |
|---|---|---:|---:|---:|---:|
| `far-128` | `entityCanvasToFb` (357 samples) | 67.728 (61.38–80.68) | 0.234 | +67.49 (+60.16 / +74.82) | 99.7% |
| | steady frame | 93.76 | 27.72 | +66.05 (+58.41 / +73.68) | 70.4% |
| `far-128-filler-last` | `entityCanvasToFb` | 53.098 (46.87–61.82) | 0.730 | +52.37 (+45.12 / +59.62) | 98.6% |
| | steady frame | 83.31 | 39.57 | +43.74 (+35.77 / +51.72) | 52.5% |
| `far-128-filler-first` | `entityCanvasToFb` | 50.239 (43.54–59.31) | 0.718 | +49.52 (+43.67 / +55.37) | 98.6% |
| | steady frame | 81.47 | 39.63 | +41.84 (+35.20 / +48.48) | 51.4% |
| `far-128-filler-last-frozen` | `entityCanvasToFb` | 54.158 (47.01–61.38) | 0.667 | +53.49 (+46.99 / +59.99) | 98.8% |
| | steady frame | 85.14 | 39.04 | +46.10 (+38.91 / +53.29) | 54.1% |
| `far-128-filler-first-frozen` | `entityCanvasToFb` | 0.732 | 0.669 | +0.06 (+0.05 / +0.07) | 8.6% |
| | steady frame | 38.64 | 38.79 | −0.15 (−1.55 / +1.26) | −0.4% |
| `far-128`, synchronized | `entityCanvasToFb` (360) | 69.415 (61.39–82.21) | 1.341 | +68.08 (+59.17 / +76.98) | 98.1% |
| | steady frame | 107.15 | 36.16 | +71.00 (+61.16 / +80.83) | 66.3% |

The query costs the same in both builds, because GLSL compiles the same way
whatever the C++ build type. Release production arms spread more than Debug
ones (`far-128` 61–81 ms against 61–65 ms). Their ablation arms do not, and
every Release lower bound still clears +43 ms.

## Timer classification

For `far-128` the same production/ablation contrast is measured four ways.
The timestamp rows are GPU timer-query spans around the composite system.
The synchronized rows (`config.gpu_stage_timing_legacy`) are CPU wall time
between `finish()` brackets. The frame rows are steady wall frames.

| `far-128` delta, ms | Debug | Release |
|---|---:|---:|
| `entityCanvasToFb`, timestamps | +61.68 (+60.02 / +63.34) | +67.49 (+60.16 / +74.82) |
| `entityCanvasToFb`, synchronized | +61.99 (+61.06 / +62.93) | +68.08 (+59.17 / +76.98) |
| steady frame, timestamp mode | +51.82 (+48.87 / +54.77) | +66.05 (+58.41 / +73.68) |
| steady frame, synchronized mode | +65.73 (+64.72 / +66.74) | +71.00 (+61.16 / +80.83) |

All four agree, so the residual is **execution cost of the exact query on
this GPU and driver**. It is not a timestamp-boundary or driver-attribution
artifact:

- **The two timers agree.** The synchronized interval drains outstanding GPU
  work before it starts and waits for the composite to finish, and it moves by
  the same amount as the timestamp span. Their confidence intervals overlap in
  both builds.
- **The frame moves with them.** The frame moves by the composite delta
  wherever the frame is not already bound elsewhere.
- **The Debug filler frames are bound elsewhere.** They are CPU-bound at about
  80 ms, which is why their frame delta is inside noise while their composite
  delta is +45–48 ms. The same cases in Release move the frame by +42–46 ms.

GPU frame accounting is unavailable on OpenGL, so there is no whole-frame GPU
row. The rows are never summed.

## Observations for the Metal replay

- The Windows Debug `far-128` composite on current master is 62.4 ms. PR
  #4843's pre-growth replay at `a6b9953c3` measured 37.8 ms on the same host
  and driver. The merged spatial ordering has not removed this case's cost on
  this GPU. These evidence sets come from different binaries and sun-buffer
  layouts. No attribution of that change is attempted here.
- The filler cases show that a composite cost this large can still be absent
  from the frame. Those Debug frames are CPU-bound at about 80 ms with the 300
  filler bodies, so the composite cannot reach the frame average. `far-128`
  has no filler and its ablation frame is 37 ms, so there the query sets the
  frame.
- The sampled-fallback control costs about 0.9 ms on both arms. The frozen
  filler-last case keeps its receiver's records while the pool is exhausted
  elsewhere, and it pays the same ~46 ms as the growth cases. The cost follows
  the route, not the pool size: from 5,164 to 71,470 retained records the
  composite stays between 45 and 62 ms.

## macOS/Metal replay and GL/Metal classification

The controller above was replayed unchanged on macOS/Metal. The run used
`run_matrix.py` `8f04a40b357f…`, the committed file and the hash that scored
the Windows diagnostic. The worktree was detached at the Windows source
revision `5d93af97c`, with this directory overlaid untracked. Every measured
path (`engine`, `creations/demos/canvas_stress`, `scripts/perf`,
`scripts/verify_common.py`) was clean, so each manifest records
`head = 5d93af97cd1a…`. Fixture, frame count, camera, cases, pair count,
variants and statistics are all the controller's own.

| | |
|---|---|
| Source | `5d93af97c`, the revision the Windows reports record |
| Host | macOS 26.5.2 (25F84), Apple M4 Max (32-core GPU, Metal 4), Apple clang 21.0.0 (clang-2100.1.1.101) |
| Resolution | Game framebuffer 1280×720 (identical runtime `config.lua`, `runtime_scripts_sha256` `c3ab47543e58…` on both hosts), trixel canvases 642×722 on both; only the final screen target differs (2560×1440 on the Retina host, 1280×720 on Windows) |
| Binaries | Debug `0eebb3615361…`, Release `23e419510623…` (full hashes in `macos-metal/*/provenance.json`) |
| Pristine sampler | `ir_sun_shadow_sample.metal` `890ea100e642…`; after the last launch both runtime shader sets still hash to their provenance (Debug `e419d9ebb850…`, Release `74e05386a988…`) |
| Power / quiet | AC power on all 128 launches; all 120 timed arms CLEAN and quiet-window `guarded`, `quiet_attempts_discarded: 0`, each build finished on its first controller launch |

**Positive fire.** Both hosts show the same diagnostic
(`macos-metal/debug/diagnostic/diagnostic.json`). All 25 captures are CLEAN.
The Metal frames are 2560 wide, so the controller scales the receiver crop to
`(1100,712)-(1460,840)`, and every pixel count is 4× the Windows 1× count.
That is 1,484 entered pixels against 371, and 936 hit pixels against 234.
The exact query fires on `far-128` and on all three exact-path filler cases.
In `far-128-filler-first-frozen` the receiver tiles (25 and 36 records) lost
records, so the route is `sampled-fallback` and the entered and hit counts are
0. In every case the closing production capture matches the oracle at exact
RGB across the receiver crop. The retained-record counts, leaf counts and
receiver tiles match the Windows run. Only the record-lost tile count
elsewhere in the frozen cases differs (1,266 and 624 on Metal, 683 and 352 on
Windows). No receiver tile is among them.

**Timing.** `entityCanvasToFb` composite in ms, five pairs per row, against
the matching Windows row. "Ratio" is GL production over Metal production.
The Metal delta carries its one-sided 95% bounds. Full tables are in
`macos-metal/<build>/summary.md`.

| Build | Case | GL production | Metal production (min–max) | Ratio | Metal ablation | Metal delta (95% lower / upper) | Metal share |
|---|---|---:|---:|---:|---:|---:|---:|
| Debug | `far-128` | 62.443 | 9.325 (9.09–9.54) | 6.70× | 6.014 | +3.31 (+3.14 / +3.48) | 35.5% |
| | `far-128-filler-last` | 48.628 | 8.943 (8.80–9.06) | 5.44× | 6.375 | +2.57 (+2.40 / +2.74) | 28.7% |
| | `far-128-filler-first` | 46.030 | 8.935 (8.74–9.09) | 5.15× | 6.488 | +2.45 (+2.28 / +2.61) | 27.4% |
| | `far-128-filler-last-frozen` | 46.619 | 8.676 (8.42–9.00) | 5.37× | 6.375 | +2.30 (+2.05 / +2.55) | 26.5% |
| | `far-128-filler-first-frozen` | 0.964 | 6.433 (6.25–6.54) | 0.15× | 6.357 | +0.08 (−0.12 / +0.27) | 1.2% |
| | `far-128`, synchronized | 64.303 | 12.097 (11.71–12.49) | 5.32× | 8.838 | +3.26 (+2.98 / +3.54) | 26.9% |
| Release | `far-128` | 67.728 | 9.085 (8.79–9.41) | 7.46× | 6.038 | +3.05 (+2.81 / +3.29) | 33.5% |
| | `far-128-filler-last` | 53.098 | 8.861 (8.49–9.00) | 5.99× | 6.523 | +2.34 (+2.25 / +2.42) | 26.4% |
| | `far-128-filler-first` | 50.239 | 8.989 (8.91–9.11) | 5.59× | 6.455 | +2.53 (+2.42 / +2.65) | 28.2% |
| | `far-128-filler-last-frozen` | 54.158 | 8.844 (8.64–9.00) | 6.12× | 6.498 | +2.35 (+2.23 / +2.46) | 26.5% |
| | `far-128-filler-first-frozen` | 0.732 | 6.430 (6.25–6.55) | 0.11× | 6.382 | +0.05 (−0.12 / +0.22) | 0.8% |
| | `far-128`, synchronized | 69.415 | 12.121 (11.92–12.47) | 5.73× | 8.752 | +3.37 (+3.18 / +3.56) | 27.8% |

On Metal the query is a real but small cost. In `far-128` it moves the
steady frame (+3.64 ms Debug, +3.30 ms Release) and the GPU command-buffer
span (+3.50, +3.09) by the same ~3 ms as the composite. The synchronized
control agrees with the counter delta. The filler steady frames move by
+4.6–7.5 ms, against a composite delta of +2.3–2.6 ms. Their frames run
near 100 ms against a GPU command-buffer span near 50–55 ms, so they are not
GPU-bound, and this run does not attribute the difference. The two builds are
within noise of each other, as on GL.

**Classification.** Debug and Release get the same verdict: **a residual
above 2× remains.**

- **The 2× test fails everywhere the query runs.** GL production is
  5.2–6.7× the matching Metal mean on the exact-path cases in Debug, and
  5.6–7.5× in Release. The tails do not rescue it: the slowest Metal
  `far-128` arm (9.54 ms Debug, 9.41 ms Release) against the fastest GL arm
  (60.62 ms, 61.38 ms) is still 6.4× and 6.5×. So the Morton-ordered
  hierarchy from #4855 has not superseded the GL excess.
- **The timer-artifact control does not explain it.** On Windows the
  synchronized control and the frame row move with the timestamp delta (§
  Timer classification). On Metal the synchronized delta agrees with the
  counter delta too. The excess is GL execution cost on the RTX 3060 Ti
  (driver 32.0.15.8180), not attribution.
- **The query term is where they differ.** The production-minus-ablation
  delta is 18–20× larger on GL in Debug (`far-128` +61.68 against +3.31 ms)
  and 19–23× in Release (+67.49 against +3.05 ms).
- **The base composite runs the other way.** With the branch off, Metal's
  composite (6.0–6.5 ms) costs 7–8× GL's in Debug (0.76–0.93 ms) and 9–26×
  in Release (0.23–0.73 ms). That includes the sampled-fallback control,
  where GL is 0.15× and 0.11× of Metal. That base cost belongs to the Metal composite with the query
  compiled out, not to the exact query, and this run does not attribute it.

Following this verification run's own rule, the measurements are committed
and the run stops here. A revised optimization plan for the GL exact-query
cost is separate work.

## GL mechanism: every coincident caster shades the stack

This is the phase-0 premise probe of the #4935 plan, run on the same Windows
host and driver. It names where the GL query term goes. It also prices
#4936 (four-word source-face order records) against a binary built without
it.

**Result.**

- **The cost is hidden coincident fragments.** The 128 caster boxes are 128
  canvases at bit-identical depth. On GL every one of them shades the whole
  stack: 128 × 106 = 13,568 fragment-shader invocations per frame, for the 106
  framebuffer samples they own when the composite ends. The first caster drawn
  owns all 106 samples, because ties go to the first draw under `LESS`. The
  other 127 lose every tie, but their fragments still run, and every caster
  fragment runs the exact query.
- **The excess is cross-canvas.** 100% of the hidden invocations on master,
  and 99.6–99.7% before #4936, belong to canvases other than the front one.
  The source-face shadowbox and the filler body run at 1.00 invocations per
  owned sample on master.
- **Forcing early fragment tests removes it.** The same draws with
  `layout(early_fragment_tests) in;` on the early-route fragment stage shade
  106 invocations for the 106 samples. The Debug `far-128` composite then
  drops from 47.95 ms to 2.80 ms. The query term left (+2.16 ms) is the same
  order as Metal's (+3.31 ms). So GL is not slow at the walk; it runs the walk
  127 extra times per pixel. The fragments fail the depth test, but GL shades
  them before it applies the test.
- **#4936 alone does not halve the composite.** It cuts the Debug `far-128`
  composite by 27% (one-sided 95% upper bound −25%), not ≥ 50%. It removes an
  intra-canvas duplicate (the shadowbox goes from 2.3–2.4 invocations per
  owned sample to 1.00, and the filler body gets its dropped faces back), but
  the 128-fold cross-canvas excess is unchanged.

Under the plan's own stops, this ends the run before phase 1. The plan's
lever, a per-canvas depth pre-pass, assumes the excess is within a canvas.
Here it is across canvases at equal depth. Canvas *k*'s shading pass would
still pass `EQUAL` against canvas 1's depth and shade in full (plan
correction 3).

### Setup

| | |
|---|---|
| Source | `45a220089` (`source_revision` in every `provenance.json`): master `44955ae9f` plus the witness and this controller |
| Controller | `run_matrix.py` `2a3bd59a6f52…` ran every witness, arm and capture |
| Post binaries | that commit: Debug `8c3e90d1541a…`, Release `71387cb90d78…` |
| Pre binaries | the same commit with `84d6f7bc9` (#4936) reverted, built in the same tree and copied out by `run_matrix.py snapshot`: Debug `1b6310828d70…`, Release `a00ebd91355b…`. Their `tree.json` and `tree.diff` are under `windows-opengl-mechanism/<build>/pre-tree/` |
| Host | Windows 10 19045, NVIDIA GeForce RTX 3060 Ti, driver 32.0.15.8180, OpenGL 4.5 context with `GL_ARB_pipeline_statistics_query` |
| Fixture | the timing fixture and five cases above, unchanged |

### The draw witness

`IR_COMPOSITE_DRAW_WITNESS="<first frame>,<frame count>,<csv>"` arms a
diagnostic in `ENTITY_CANVAS_TO_FRAMEBUFFER`. On each witnessed frame it runs
the composite unchanged and records, for every draw:

- **Invocations.** `GL_FRAGMENT_SHADER_INVOCATIONS` around the draw.
- **GPU time.** A timestamp pair around the draw. Draws overlap on GL, so a
  single draw's span is not exclusive. Summed over a group of draws it is
  indicative only; the timing verdicts come from `repeat_profile.py` arms.
- **Owned samples.** After the composite the draws replay in their original
  order, with no colour or depth writes, under depth `EQUAL` and a stencil
  first claim. A draw counts (`GL_SAMPLES_PASSED`) only the samples at the
  final depth that no earlier draw claimed. That is the first draw to reach a
  depth, which is the draw `LESS` lets keep it.

The counters bracket the draws and never enter the measured shaders. So
they cannot change which fragments early depth rejects (plan gotcha 1). A
backend without invocation statistics writes `unsupported` and no rows;
Metal is one, so the witness is OpenGL only.

`run_matrix.py witness` takes frames 60–64 of an unprofiled launch per tree,
case and variant (`production`, `ablation`, `early-tests`). It groups draws by
canvas placement: the 128 casters share one framebuffer centre, extent and
depth offset. Each group reports invocations, owned samples, and its hidden
invocations (invocations minus owned). The hidden invocations split into the
front canvas's own overdraw and everything the other canvases shade.

**Controls.** The counters read what is drawn. A canvas nothing covers
reports invocations equal to owned samples: the shadowbox's 1,200 and the
filler body's 98,566 on master. Under `early-tests` the hidden casters drop
to zero invocations, so the counter does not count depth-rejected fragments.
In `far-128-filler-first-frozen` the receiver tiles lost records, and the
caster group carries no query term (−0.5 ms), as the attribution above
predicts for the sampled fallback.

### Witness (Debug, frames 60–64, medians)

Caster group = the 128 coincident caster canvases. Query term = the group's
summed per-draw GPU time, production minus `queryHierarchy = false`.

| Tree | Case | Caster invocations / owned | Cross-canvas share of hidden | Caster query term ms | Early-tests invocations / owned | Shadowbox invocations / owned |
|---|---|---:|---:|---:|---:|---:|
| pre | `far-128` | 12,472 / 106 = 117.7 | 99.6% | +58.7 | 1.04 | 2.34 |
| pre | `far-128-filler-last` | 118.2 | 99.7% | +61.6 | 1.04 | 2.33 |
| pre | `far-128-filler-first` | 117.9 | 99.6% | +57.3 | 1.04 | 2.42 |
| pre | `far-128-filler-last-frozen` | 118.0 | 99.7% | +59.3 | 1.04 | 2.33 |
| pre | `far-128-filler-first-frozen` | 117.4 | 99.6% | −0.4 | 1.04 | 2.44 |
| post | `far-128` | 13,568 / 106 = 128.0 | 100.0% | +58.0 | 1.00 | 1.00 |
| post | `far-128-filler-last` | 128.0 | 100.0% | +42.9 | 1.00 | 1.00 |
| post | `far-128-filler-first` | 128.0 | 100.0% | +40.1 | 1.00 | 1.00 |
| post | `far-128-filler-last-frozen` | 128.0 | 100.0% | +38.6 | 1.00 | 1.00 |
| post | `far-128-filler-first-frozen` | 128.0 | 100.0% | −0.5 | 1.00 | 1.00 |

Every other draw kind carries none of the query term. The texture-layer quad
of each caster canvas shades its full 8,192-pixel footprint and owns
nothing, but the 128 of them cost about 0.5 ms with or without the query. The
early route takes every caster; their fallback (fragment-depth) draws emit
no fragments.

Full per-group tables: `windows-opengl-mechanism/debug/witness/summary.md`.

### Pre vs post timing

`run_matrix.py ab-timing` interleaves both binaries in both variants
(`production`, `queryHierarchy = false`) in every pair. Odd and even pairs
mirror the launch order. Every arm is one quiet-window-guarded, CLEAN
`repeat_profile.py` run, validated against its declared binary and staged
shader set, as in the matrix above. Debug runs all five cases. Release runs
`far-128` only, the case the plan's cut is judged on.

`entityCanvasToFb` composite, five pairs per row. "Change" is post minus
pre over the pre mean, with the one-sided 95% upper bound's share in
brackets; the plan's cut needs that upper bound ≤ −50%. "Query term" is
production minus `queryHierarchy = false` in the same tree.

| Build | Case | Pre ms (min–max) | Post ms (min–max) | Post − pre (95% lower / upper) | Change (upper) | Query term, pre | Query term, post |
|---|---|---:|---:|---:|---:|---:|---:|
| Debug | `far-128` | 65.652 (65.05–66.33) | 48.009 (47.90–48.29) | −17.64 (−18.31 / −16.98) | −26.9% (−25.9%) | +64.66 | +47.03 |
| | `far-128-filler-last` | 50.345 (49.97–50.95) | 40.374 (40.06–40.75) | −9.97 (−10.40 / −9.54) | −19.8% (−18.9%) | +49.21 | +39.11 |
| | `far-128-filler-first` | 47.731 (47.10–48.25) | 38.661 (38.51–38.82) | −9.07 (−9.60 / −8.54) | −19.0% (−17.9%) | +46.58 | +37.30 |
| | `far-128-filler-last-frozen` | 49.823 (49.56–50.29) | 39.811 (39.42–40.25) | −10.01 (−10.50 / −9.53) | −20.1% (−19.1%) | +48.74 | +38.58 |
| | `far-128-filler-first-frozen` | 1.251 (1.09–1.40) | 1.386 (1.28–1.65) | +0.13 (+0.06 / +0.21) | +10.7% (+16.6%) | +0.13 | +0.09 |
| Release | `far-128` | 66.045 (65.76–66.55) | 48.051 (47.88–48.33) | −17.99 (−18.39 / −17.60) | −27.2% (−26.6%) | +65.74 | +47.75 |

**The cut gate is not met in either build.** #4936 takes about 18 ms off
`far-128`, and all of it comes off the query term: the
`queryHierarchy = false` arms move by −0.04 ms (Debug) and −0.004 ms
(Release). The Debug steady frame moves with it (−17.6 ms, 95.9 → 78.3 ms).
The filler frames are CPU-bound near 110 ms and stay inside noise. This
master `far-128` composite (48.0 ms) is lower than the 62.4 ms measured
above at `5d93af97c`. These are different binaries with many changes in
between; the difference is not attributed here.

#4936 does not reduce the caster excess. Before it, the scrambled order
records drew 12,472 caster fragments per frame; after it, 13,568, which is
106 for each of the 128 canvases. The saving is per invocation: the query
term is 64.7 ms over 12,472 invocations before and 47.0 ms over 13,568
after. This run does not attribute that per-invocation difference.

Full tables: `windows-opengl-mechanism/<build>/ab-timing/summary.md`.

### Early-tests control timing

`far-128` on the post binary, five interleaved pairs of `production`,
`queryHierarchy = false` and `early-tests`. `early-tests` stages
`layout(early_fragment_tests) in;` into the runtime
`f_source_face_scatter_early.glsl` only. It is a mechanism control, not a
candidate change: with forced early tests a fragment's depth is written
before the alpha `discard` can drop it.

| Build | Production ms (min–max) | `queryHierarchy = false` ms | Early tests ms (min–max) | Early tests − production (95% lower / upper) | Change (upper) | Early tests − `queryHierarchy = false` |
|---|---:|---:|---:|---:|---:|---:|
| Debug | 47.952 (47.79–48.40) | 0.640 | 2.796 (2.76–2.86) | −45.16 (−45.37 / −44.94) | −94.2% (−93.7%) | +2.16 (+2.05 / +2.26) |
| Release | 48.777 (47.94–49.54) | 0.290 | 2.753 (2.74–2.77) | −46.02 (−46.73 / −45.32) | −94.4% (−92.9%) | +2.46 (+2.45 / +2.48) |

The query is still on under early tests. It now runs once per caster pixel
instead of 128 times, and costs +2.2–2.5 ms over the branch compiled out.
That is the same order as Metal's whole query term.

### Pixel witness on the measured tree

`run_matrix.py diagnose --build debug` on the post binary, with the witness
code compiled in and disarmed. The 25 captures are all CLEAN
(`windows-opengl-mechanism/debug/diagnostic/diagnostic.json`).

| Case | Route | Entered px | Hit px | Closing production vs oracle, receiver crop | Stack crop, z-fight px |
|---|---|---:|---:|---:|---:|
| `far-128` | exact hierarchy | 371 | 234 | 0 | 0 |
| `far-128-filler-last` | exact hierarchy | 371 | 234 | 0 | 0 |
| `far-128-filler-first` | exact hierarchy | 371 | 234 | 0 | 0 |
| `far-128-filler-last-frozen` | exact hierarchy | 371 | 234 | 0 | 0 |
| `far-128-filler-first-frozen` | sampled fallback | 0 | 0 | 0 | 0 |

`production_matches_oracle` is true in all five cases. The entered and hit
counts are the ones in § Positive fire and pixel witness, at `5d93af97c`. The caster stack no
longer z-fights between pristine captures (0 px, against 588–692 there). A
fixed first-drawn owner at every tied pixel is what the witness reports, and
it is consistent with #4936 drawing each face once.

### What this means for the plan

- **Decision 2 bail (no draw ≥ 2 invocations per owned pixel, or excess only
  in texture-layer quads): does not fire.** The caster early draws run at 118
  (pre) and 128 (post), and they carry the query term.
- **Correction 3 bail (majority of hidden invocations cross-canvas): fires.**
  The excess is 99.6–100% cross-canvas, so phase 1 is not built. Lowering the
  term is a frame-scoped tie or ownership question across canvases. One
  option is forcing early tests on the opaque early route, which the control
  above prices at −94%; its cost is alpha-cutout faces. Another is collapsing
  coincident casters. Whether a 128-coincident-caster fixture is worth that
  design is a product call.
- **Correction 4 (re-witness on post):** done. The ratio on master is 128.0,
  and it is all cross-canvas.
- **Correction 1 (stack crop share):** the composite draws no floor; the
  floor is main-canvas GRID content. The composite's query term is the caster
  canvases' own lit faces querying the hierarchy. The caster group carries all
  of it: +58.0 of the +58.0 ms summed query term on master `far-128`.

## Replay

From a checkout at the recorded source revision, with this directory present
(at a revision that predates it, overlay it untracked with
`git archive origin/master docs/perf/sun-overflowed-tile-query | tar -x`; the
measured paths stay clean) and `IRCanvasStress` built in both trees
(`fleet-build --target IRCanvasStress` and `fleet-build --release --target IRCanvasStress`):

```bash
python3 docs/perf/sun-overflowed-tile-query/test_controller.py
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py diagnose --build debug --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py timing --build debug --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py timing --build release --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
```

A replay records its own `provenance.json`. `windows-opengl/` and
`macos-metal/` stay bound to the controller hash that captured them, so `timing` and `diagnose` refuse to
extend it with a different controller.

The mechanism evidence replays from the measured branch commit. Build
`IRCanvasStress` in both build types, then snapshot a tree carrying the
revert, and restore it:

```bash
git revert --no-commit 84d6f7bc9
fleet-build --target IRCanvasStress && fleet-build --release --target IRCanvasStress
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py snapshot --build debug --dest <pre>/debug --label pre --reverts 84d6f7bc9
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py snapshot --build release --dest <pre>/release --label pre --reverts 84d6f7bc9
git revert --quit && git checkout HEAD -- engine scripts/tests
fleet-build --target IRCanvasStress && fleet-build --release --target IRCanvasStress
OUT=docs/perf/sun-overflowed-tile-query/windows-opengl-mechanism
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py witness --build debug --output $OUT --pre-tree <pre>/debug
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py ab-timing --build debug --output $OUT --pre-tree <pre>/debug
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py ab-timing --build release --output $OUT --pre-tree <pre>/release --cases far-128
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py ab-timing --build <build> --output $OUT --name early-tests-timing --trees post --cases far-128 --variants production ablation early-tests
```

`ab-timing` and `witness` resume the same way `timing` does.

`timing` resumes after an interruption. The controller exits after an arm
finds no quiet window in eight attempts, so on a busy host rerun it until it
completes; finished arms are re-validated, never re-run. Run
`run_matrix.py restore --build <build>` if a run was killed hard while a
variant was staged. The controller refuses to stage over a non-pristine
shader set.

## Retained evidence

`windows-opengl/<build>/` and `macos-metal/<build>/` hold the following:

- `provenance.json`, `plan.json` and the `synchronized.lua` preset.
- One directory per arm, `<mode>/<case>/pair-<n>-<variant>/`. Each holds the
  `repeat_profile.py` manifest, `run-1.txt` report and summary, and the
  controller's `arm.json`, which records the declared expectations, the
  parsed metrics and the restored shader-set hash.
- `warmup/`, holding the untimed warm launches.
- `summary.md` and `summary.json`.

`debug/diagnostic/` holds each capture's command, config, frame and log. It
keeps the index CSV of the oracle and closing production captures, plus
`diagnostic.json` with every witness and verdict.

After the runs, every log was gzip-compressed (`*.log.gz`, checked to
round-trip byte for byte). The index CSVs of the three branch-forcing variants
were dropped, because the sampler does not affect the index; their witnesses
are in `diagnostic.json`. Neither step changes anything `summarize` or a
resumed `timing` reads.

`windows-opengl-mechanism/<build>/` holds the mechanism evidence:

- `pre-tree/`: the pre snapshot's `tree.json` and `tree.diff` (the binary
  itself is not retained). `diff_sha256` is the hash of the exact `tree.diff`
  bytes. The copies of `tree.json` inside the runs' `provenance.json`,
  `summary.json` and `witness.json` instead carry `4181baea…`, which is the
  same diff without its final newline; those run records are kept as written.
  Reopening a pre tree (`--pre-tree`) checks its recorded build type, binary,
  diff and shader-directory hashes.
- `witness/` (Debug): `provenance.json`, `witness.json` with every capture's
  command, staged hashes and per-group medians, and one
  `<tree>/<case>/<variant>/` directory per capture holding `witness.csv` and
  `run.log.gz`.
- `ab-timing/` and `early-tests-timing/`: `provenance.json`, `plan.json`,
  one `<case>/pair-<n>-<tree>-<variant>/` directory per arm (laid out like
  the matrix arms above), `warmup/`, `summary.md` and `summary.json`.
- `debug/diagnostic/` and `debug/provenance.json`: the post-binary pixel
  witness, retained as `debug/diagnostic/` is above.

Every log there is gzip-compressed (round trip checked), and the
branch-forcing variants' index CSVs are dropped, as above. Several arms found
no quiet window in eight attempts while other fleet panes held busy leases.
The controller stopped, and a relaunch re-ran only the stopped arm.

Over the session, two arms (Debug `far-128-filler-last-frozen` pair 3
production and Release `far-128-filler-last-frozen` pair 4 ablation) found no
quiet window in eight attempts while another fleet pane was active. The
controller stopped each time. A relaunch re-validated the finished arms and
re-ran only the stopped one, which then passed on its first attempt. Every
retained arm records `quiet_attempts_discarded: 0`. The macOS session had no
stops. Both of its builds completed on their first controller launch, and the
same log compression and index-CSV drop were applied to `macos-metal/`.
