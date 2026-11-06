# Exact overflowed-tile sun query: Windows/OpenGL attribution

This measures how much of the `entityCanvasToFb` composite the exact
overflowed-tile query costs on current master, on Windows/OpenGL, in Debug and
Release. The exact query is the `queryHierarchy` branch of
`sampleCascadeShadow`, in `ir_sun_shadow_sample.glsl` and its Metal twin.
Receivers in a tile that overflowed but kept every record walk the retained
record pool's hierarchy instead of the tile list.

The macOS/Metal replay and the GL/Metal ratio belong to a separate
verification run. That run replays this controller unchanged at the same
source revision and writes its own `macos-metal/` subtree beside
`windows-opengl/`.

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
| Controller | `run_matrix.py` `8b76606c2daa…`, the committed file; both builds ran it unchanged |
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

- **Entered.** Receiver floor pixels that change against the oracle under
  entry-shadow or entry-lit.
- **Hit.** Receiver floor pixels that change under hit-cleared.
- **Correctness.** The closing production capture against the oracle,
  compared as exact RGB on the receiver's floor pixels.

**Statistics.** `summarize` pairs each production run with the ablation run
of the same pair. It reports:

- both arms' means and spreads;
- the paired production-minus-ablation mean and its standard deviation;
- the one-sided 95% lower and upper bounds `mean ∓ t(0.95, n−1)·sd/√n`;
- the share, `delta / production mean`.

## Positive fire and pixel witness (Debug, untimed)

The data is in `windows-opengl/debug/diagnostic/diagnostic.json`. The
receiver crop is `(550,300)-(730,420)`, the historical 1× crop. Pixels are
counted on the floor only, meaning pixels that are neutral grey in both
images: lit (150,152,160) or shadowed (45,46,48). The 128 coincident caster
boxes z-fight differently on every run. That makes 500–700 blue caster pixels
in the crop differ between two pristine captures, and those pixels are
reported separately as `receiver_caster_occlusion_px`.

| Case | Retained records | Ready leaves = leaf count | Entered px (entry-shadow ∪ entry-lit) | Hit px (hit-cleared) | Closing production vs oracle (floor px) |
|---|---:|---|---:|---:|---:|
| `far-128` | 5,164 | 1,024 = 1,024 | 371 | 234 | 0 |
| `far-128-filler-last` | 71,470 | 16,384 = 16,384 | 371 | 234 | 0 |
| `far-128-filler-first` | 71,470 | 16,384 = 16,384 | 371 | 234 | 0 |
| `far-128-filler-last-frozen` | 65,536 | 8,192 = 8,192 | 371 | 234 | 0 |
| `far-128-filler-first-frozen` | 65,536 | 8,192 = 8,192 | **0** | **0** | 0 |

What the table shows:

- **Every receiver shadow pixel is a hierarchy hit.** The receiver's shadow is
  234 floor pixels, and every one of them is a hierarchy hit: clearing the hit
  lights all 234, and so does skipping the branch.
- **The query also covers lit floor.** Forcing entry to shadow darkens another
  137 lit floor pixels that the query certified as unoccluded.
- **The frozen filler-first control never enters the branch.** Its receiver
  tiles lost records, so it answers from the sampled layers.
- **Pristine captures are stable.** All 25 captures exited CLEAN. In every
  case the closing pristine capture matches the opening oracle on every floor
  pixel of the crop.

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

## Replay

From a checkout at the recorded source revision, with this directory present
and `IRCanvasStress` built in both trees
(`fleet-build --target IRCanvasStress` and `fleet-build --release --target IRCanvasStress`):

```bash
python3 docs/perf/sun-overflowed-tile-query/test_controller.py
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py diagnose --build debug --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py timing --build debug --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
python3 docs/perf/sun-overflowed-tile-query/run_matrix.py timing --build release --output docs/perf/sun-overflowed-tile-query/<host>-<backend>
```

`timing` resumes after an interruption. The controller exits after an arm
finds no quiet window in eight attempts, so on a busy host rerun it until it
completes; finished arms are re-validated, never re-run. Run
`run_matrix.py restore --build <build>` if a run was killed hard while a
variant was staged. The controller refuses to stage over a non-pristine
shader set.

## Retained evidence

`windows-opengl/<build>/` holds the following:

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

Over the session, two arms (Debug `far-128-filler-last-frozen` pair 3
production and Release `far-128-filler-last-frozen` pair 4 ablation) found no
quiet window in eight attempts while another fleet pane was active. The
controller stopped each time. A relaunch re-validated the finished arms and
re-ran only the stopped one, which then passed on its first attempt. Every
retained arm records `quiet_attempts_discarded: 0`.
