# Sun record loss under pool pressure: stability across frames and launches

**Result: reproduced on Metal.** On a static scene with a static camera, the
set of record-lost sun tiles differs between every pair of captures, within
one run and between launches. When the pool boundary falls among the tiles
under a shadowed receiver, that receiver switches between the exact answer and
the sampled answer from one frame to the next. Its shadow then flickers by 148,
156 or 368 pixels at exact RGB, in 9 of 10 launches. A scene so far past the
ceiling that every occupied tile but a fixed few is lost (`IRPerfGrid` 64³) has
the same lost set in every launch.

The cause is the one read from source. Each face reserves its record with one
atomic add on the pool's header word (`ir_sun_face_index`). Which faces
arrive after the pool is full depends on the order GPU threads reach that
word, and that order is not stable on this GPU.

| | |
|---|---|
| Source | `d5f23eb3f` (clean `engine/` and `creations/demos`, recorded per fixture in `manifest`) |
| Host | macOS 26.5.2 (25F84), Apple M4 Max, Metal, Debug |
| Binary | `IRCanvasStress` `711064fac69d…`; `IRPerfGrid` hash in `perfgrid-64/compare.json` |
| Resolution | 1280×720 game framebuffer, 2560×1440 screenshots |
| OpenGL | **Owed.** Needs a Linux or Windows GL host: `python3 scripts/perf/sun_record_loss_stability.py capture <fixture> --launches N --raw <dir>`, then `compare`. Identical sets on one GPU and driver would not prove determinism on another. The committed Windows/OpenGL captures in [`sun-overflowed-tile-query`](../sun-overflowed-tile-query/README.md) already differ by 118 and 100 lost tiles between two launches of the same frozen case, against 366 and 652 on Metal. |

## Fixtures

The controller is [`scripts/perf/sun_record_loss_stability.py`](../../../scripts/perf/sun_record_loss_stability.py).
Its hermetic suite is `scripts/tests/test_sun_record_loss_stability.py`.

All three CanvasStress fixtures use the historical exhaustion scene from
`sun-overflowed-tile-query`: 128 coincident caster boxes 60 units above a
40-unit grid floor, with the sun straight down. They add a 300×300×1
record-pool filler plate created after the casters, offset `0 62 30`, which
puts it 30 units under the floor. The plate's 90,000 faces overlap the
floor's sun tiles, so a lost plate record marks the receiver's tile lost. The
floor hides the plate from the receiver. With the plate centred under the
receiver (offset `0 0 30`), every receiver tile was lost in every shot, so the
frames were byte-identical. The 62-unit shift puts the receiver on the edge of
the band of tiles whose loss varies. Each launch takes ten identical-camera
shots 30 frames apart (`--sweep-frames 10 30`) and writes one
`--sun-face-index-probe` CSV per shot.

| Fixture | Extra arguments | Records requested / pool | Launches × shots |
|---|---|---:|---:|
| `canvas-unfrozen` | none (the pool grows) | 95,164 / 131,072 | 1 × 10 |
| `canvas-frozen` | `--sun-record-capacity 65536 --sun-record-ceiling 65536` | 95,164 / 65,536 | 10 × 10 |
| `canvas-frozen-overlay` | frozen, plus `--debug-overlay shadow` | 95,164 / 65,536 | 2 × 10 |
| `perfgrid-64` | `IRPerfGrid --mode voxel_set --grid-size 64 --wave-freeze --auto-profile 300 --no-overlay`; one probe after the last profile frame | 786,432 / 131,072 | 5 × 1 |

**Receiver crop.** `(1100,712)-(1460,840)` in the 2560-wide frame. This is the
floor band under the stack, the same crop `sun-overflowed-tile-query`
validated: it excludes the coincident caster stack above it, which z-fights
between launches on its own. Every pixel count below is exact RGB
(`changed_pixels`, any channel differing). For the published pairs it is
cross-checked by `scripts/render-compare.py --per-pixel-tol 0`.

**Lost-tile sets.** Each capture's set is the tiles whose `records_lost` flag
is 1. Retained counts and the order of a tile's list are not compared. A
permutation inside a complete tile changes nothing, and a lost tile answers
from the sampled layers whatever it retained. The *floor window* is the subset
of tiles whose sun-space bounds overlap the floor, `|u|, |v| ≤ 20`.

## Results

### Positive control (criterion 1)

| Fixture | Lost tiles, every shot | Committed CSV |
|---|---:|---|
| `canvas-unfrozen` | 0 (pool grown to 131,072) | `macos-metal/canvas-unfrozen/sun-face-index-0.csv.gz` |
| `canvas-frozen` launch 0 | 3,976 to 4,348 | `macos-metal/canvas-frozen/sun-face-index-0.csv.gz` |

### Lost-tile sets (criteria 2 and 4)

"Differing" is the number of tiles whose lost flag is not the same in every
capture of the group (union minus intersection). "Pairwise" is the symmetric
difference between two captures, as a min–max range over every pair.

| Group | Captures | Distinct sets | Lost tiles | Differing | Pairwise | Floor window: distinct / differing |
|---|---:|---:|---:|---:|---:|---:|
| `canvas-frozen`, in-run, per launch | 10 each | 10 in every launch | 3,976–4,348 | 505–1,145 | 92–523 | 4–8 / 8–71 |
| `canvas-frozen`, first shot of each launch | 10 | 10 | 4,068–4,262 | 852 | 73–433 | — |
| `canvas-frozen`, all captures | 100 | **100** | 3,976–4,348 | **2,403** | 66–565 | 28 / 82 |
| `canvas-frozen-overlay`, all captures | 20 | 20 | 4,097–4,275 | 990 | 75–430 | 9 / 32 |
| `perfgrid-64`, across launches | 5 | **1** | 987 | **0** | 0 | 1 / 0 |

Per-launch rows are in `macos-metal/<fixture>/compare.json`. Every capture's
lost list is in `lost-tiles.json.gz`.

`perfgrid-64` is stable because it is saturated. It loses 655,360 of its
786,432 records, so a late face lands in all but 20 of its 1,007 occupied
tiles, in every launch. Those 20 tiles keep identical counts in all five
launches. The retained counts inside the 987 lost tiles still differ between
launches (799 tiles), but a lost tile answers from the sampled layers
whatever it retained, so that difference cannot reach a pixel. One probe per
launch does not cover frame-to-frame stability for this fixture.

### Pixels (criterion 3)

Changed receiver-crop pixels of shots 1–9 against shot 0 of the same launch:

| Launch | Shots 1–9 vs shot 0 |
|---|---|
| 00 | 0, 0, 148, 156, 156, 156, 0, 0, 0 |
| 01 | 0, 0, 0, 0, 0, 156, 0, 156, 0 |
| 02 | 0, 0, 0, 0, 0, 156, 0, 0, 0 |
| 03 | 156, 156, 156, 0, 156, 0, 368, 0, 156 |
| 04 | 156, 0, 368, 156, 156, 156, 156, 156, 0 |
| 05 | 0, 156, 0, 156, 0, 0, 156, 156, 0 |
| 06 | 0, 0, 156, 0, 0, 0, 0, 156, 156 |
| 07 | 0, 156, 156, 0, 0, 0, 0, 0, 0 |
| 08 | 156, 0, 0, 0, 0, 156, 156, 156, 0 |
| 09 | 0, 0, 0, 0, 0, 0, 0, 0, 0 |
| overlay 00 | 0, 0, 0, 0, 368, 0, 0, 0, 0 |
| overlay 01 | 0, 0, 0, 0, 0, 0, 0, 0, 368 |

Across launches, shot 0 of launches 1–9 differs from launch 0's shot 0 by
0, 0, 156, 156, 0, 0, 0, 156, 0 pixels. Those are values the in-run
comparisons also produce, and the crop holds no z-fighting body, so
launch-to-launch differences inside the crop come from the same mechanism.

Within a run, nothing outside this receiver's shadow changes. Whole-frame
counts are the crop count plus 0, 24 or 80 pixels, and in both published pairs
those extra pixels are rows 706–711 of the same shadow, just above the crop's
top edge. Everywhere else, the lost set changes in every shot without
changing a pixel.

**Published pairs** (media commit `69986672a4b8`):

- Lit, `canvas-frozen` launch 3, shot 0 against shot 7: 368 crop pixels.
  `render-compare --per-pixel-tol 0` reports match 99.2014% of bytes, which is
  368 pixels × 3 channels, with max delta 112.
  [shot 0](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/lit-shot0.png),
  [shot 7](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/lit-shot7.png),
  [crop ×3: shot 0 | shot 7 | changed](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/lit-crop-x3-a-b-diff.png).
  Shot 0 lost cascade-0 row 49 under the receiver. Shot 7 also lost rows 50
  and 51.
- Shadow overlay, `canvas-frozen-overlay` launch 0, shot 0 against shot 5:
  368 crop pixels, max delta 255.
  [shot 0](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/shadow-overlay-shot0.png),
  [shot 5](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/shadow-overlay-shot5.png),
  [crop ×3: shot 0 | shot 5 | changed](https://raw.githubusercontent.com/jakildev/IrredenEngine/69986672a4b8eaf354fc08029cbfb0a9c5cc861a/docs/pr-screenshots/claude/4968-sun-record-loss-stability/shadow-overlay-crop-x3-a-b-diff.png).
  When rows 49–50 under the receiver are lost (shot 0), the cast shadow is
  notched with a lit gap inside it. That is the sampled answer, consistent
  with the lost centre depth that the design doc describes after § "Candidates
  measured".
  When only row 49 is lost (shot 5), the exact answer is a solid rectangle.

## Decision

The lost set is not stable, and a receiver near the pool boundary flickers on
a static scene. Follow-up #5022 makes admission deterministic. Its gate is
this repro reading zero: `canvas-frozen` at 10 launches × 10 shots with 1
distinct lost set and 0 changed receiver-crop pixels in every comparison, on
Metal and on an OpenGL host. That ticket changes which records are kept and
in what order, not the pool ceiling or how a lost tile is answered.
Determinism does not make an exhausted pool correct
([§ Candidates measured](../../design/sun-face-index-capacity.md#candidates-measured)).
It makes the wrong answer the same on every frame.

## Replay

```bash
fleet-build --target IRCanvasStress
fleet-build --target IRPerfGrid
python3 scripts/perf/sun_record_loss_stability.py capture canvas-unfrozen --launches 1 --raw /tmp/loss
python3 scripts/perf/sun_record_loss_stability.py capture canvas-frozen --launches 10 --raw /tmp/loss
python3 scripts/perf/sun_record_loss_stability.py capture canvas-frozen-overlay --launches 2 --raw /tmp/loss
python3 scripts/perf/sun_record_loss_stability.py capture perfgrid-64 --launches 5 --raw /tmp/loss
python3 scripts/perf/sun_record_loss_stability.py compare canvas-frozen --raw /tmp/loss   # and each fixture
```

`capture` launches through `fleet-run`, which takes the GPU lock for
`--auto-screenshot` and the benchmark lock for `--auto-profile`. It keeps the
frames, the full probe CSVs and the gzipped logs under `--raw`. `compare`
rewrites this directory's `<host>/<fixture>/` evidence from them.
