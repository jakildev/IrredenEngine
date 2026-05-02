# img_diff

Tiny standalone CLI that highlights per-pixel drift between two PNGs.

```
img_diff <baseline.png> <current.png> <out_diff.png> [--threshold N] [--ignore-alpha]
```

## Output PNG

| Pixel state | What gets written |
|---|---|
| Drifted (any channel `> threshold`) | Solid red `(255, 0, 0, 255)` |
| Unchanged | Baseline pixel desaturated to ~30% luminance |

Drift pops red against quiet visible context, so a single drifted pixel
in a 1280×720 frame is impossible to miss. The desaturated baseline
keeps spatial reference (you can see *where* the drift sits relative to
geometry) without competing with the red highlights.

## Why it exists

Render-debug agents and human reviewers both lose subtle 1-pixel
regressions (cube-edge zigzag, single-pixel parity drift) when looking
at full-frame screenshots. A red-on-grey diff PNG turns "compare these
two images pixel by pixel" into "which pixels are red".

Companion to `scripts/render-compare.py`, which reports aggregate
metrics (PSNR, max delta, match%) and a brightness-delta diff PNG. Use
`render-compare.py` for pass/fail gates and `img_diff` for "show me
the drift".

## Build

The tool builds whenever `IRREDEN_BUILD_TOOLS` is on (default for the
top-level engine build):

```
cmake --preset macos-debug      # or linux-debug / windows-debug
cmake --build build --target img_diff
```

Binary lands at `build/tools/img_diff/img_diff`.

## Exit codes

| Code | Meaning |
|---|---|
| 0 | Zero drift — current matches baseline within `--threshold` |
| 1 | Drift detected — diff PNG written |
| 2 | Argument or I/O error |

## Options

- `--threshold N` — per-channel delta tolerance, default `0`. A pixel
  drifts when its largest channel delta is **strictly greater** than
  the threshold.
- `--ignore-alpha` — compare RGB only; useful when alpha is constant
  but renders differently across PNG encoders.

## Examples

```
# zero drift
img_diff a.png a.png /tmp/diff.png
# img_diff: total=921600 drift=0 (0.0000%) max_delta=0 threshold=0 -> /tmp/diff.png

# show drift between baseline and current shot
img_diff baselines/zoom4_origin.png current/zoom4_origin.png /tmp/zoom4_diff.png

# tolerate small shading differences (tone-mapper jitter, etc.)
img_diff baseline.png current.png /tmp/diff.png --threshold 4
```

## Related

- `scripts/render-compare.py` — aggregate metrics + brightness-delta diff
- `.claude/skills/render-debug-loop/SKILL.md` — agent workflow that
  consumes `img_diff` output
- `.claude/skills/attach-screenshots/SKILL.md` — captures the before/
  after pairs that `img_diff` operates on
