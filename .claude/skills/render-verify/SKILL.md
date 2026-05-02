---
name: render-verify
description: >-
  Automated pass/fail render-regression harness for Irreden Engine demos.
  Wraps build → run → capture → compare → report: builds a demo, runs it with
  `--auto-screenshot`, then compares each shot against a committed reference
  image using a stdlib-only Python comparator. Reports per-shot pass/fail
  with diff images written for mismatches. Use after any render pipeline
  change to catch visual regressions automatically, or as a sanity pass on
  `master` before cutting releases.
---

# Render Verify

Visual regression harness. Complements `render-debug-loop`: where that skill
is the open-ended "render, inspect, iterate" loop for fixing new rendering
work, this skill is the one-shot "did anything regress?" gate.

## When to invoke

- After changing anything in `engine/render/src/shaders/`,
  `engine/render/src/opengl/`, `engine/render/src/metal/`, or any
  `engine/prefabs/irreden/render/systems/`.
- Before opening a PR that touches the render pipeline (spot-check that
  unrelated shots still match).
- On a clean `master` checkout as a smoke test that the harness itself
  still works.

If the skill reports a regression you *expected* (intentional visual
change), re-run with `--update-references` to bless the new output as the
new baseline, then commit the updated PNGs alongside the render change.

## Prerequisites

### Platform

Works on any preset. The harness detects the active backend from
`build/CMakeCache.txt` and looks up reference PNGs under the matching
backend subdirectory:

| Host          | Preset          | References dir                                           |
|---------------|-----------------|----------------------------------------------------------|
| WSL2 Ubuntu   | `linux-debug`   | `creations/demos/<demo>/test/references/linux-debug/`    |
| macOS         | `macos-debug`   | `creations/demos/<demo>/test/references/macos-debug/`    |
| Windows-native| `windows-debug` | `creations/demos/<demo>/test/references/windows-debug/`  |

Backends produce pixel-different output (FP rounding, driver differences).
Each backend keeps its own reference set — references are **not** shared
across backends.

**HiDPI caveat.** Screenshots are captured at framebuffer size, not
window size. On a Retina macOS host the capture is 2× the requested
resolution. References captured on a HiDPI host won't compare cleanly
against captures from a non-HiDPI host even on the same backend — the
comparator early-returns with a shape-mismatch reason. Capture the
reference set on a host representative of where verify will run.

### Demo requirement: `--auto-screenshot`

The target demo must implement `--auto-screenshot [warmup-frames]` (same
contract as `render-debug-loop`). Reference implementation:
`creations/demos/shape_debug/main.cpp` — see `ShotConfig`, `g_shots[]`,
the `AutoScreenshot` system.

### Manifest

Each demo participating in the harness ships a
`test/references/manifest.json` file listing shots in the same order the
demo cycles through them. Example (shape_debug):

```json
{
  "demo": "shape_debug",
  "target": "IRShapeDebug",
  "screenshot_subdir": "save_files/screenshots",
  "shots": ["zoom1_origin", "zoom2_origin", ...],
  "thresholds": {
    "per_pixel_tol": 8,
    "match_pct": 99.9,
    "max_delta": 64,
    "psnr_db": 35.0
  }
}
```

Shots are mapped to screenshots by **order** (Nth shot → Nth numbered
screenshot), so the manifest must list them in the same order the demo's
shot table.

## Running the harness

One command, from the worktree root:

```
python3 scripts/render-verify.py --target IRShapeDebug
```

The driver:

1. Reads `creations/demos/shape_debug/test/references/manifest.json`.
2. Detects the backend from `build/CMakeCache.txt`.
3. Runs `fleet-build --target IRShapeDebug`.
4. Clears `<exe_dir>/save_files/screenshots/`.
5. Runs `fleet-run --timeout 60 IRShapeDebug --auto-screenshot 10`.
6. For each shot, runs `scripts/render-compare.py` against the reference.
7. Prints a pass/fail table and exits non-zero on any failure.

### Typical pass output

```
shot                            result     match%   max_d     psnr
------------------------------------------------------------------
zoom1_origin                    PASS       100.0        0      inf
zoom2_origin                    PASS       100.0        0      inf
zoom4_origin                    PASS       100.0        0      inf
zoom1_odd_offset                PASS       100.0        0      inf
zoom8_origin                    PASS       100.0        0      inf
zoom4_offset_3_5                PASS       100.0        0      inf

[render-verify] all 6 shots PASS
```

### Typical fail output

```
shot                            result     match%   max_d     psnr
------------------------------------------------------------------
zoom1_origin                    PASS       100.0        0      inf
zoom2_origin                    FAIL     98.9423      214    24.12
...

[render-verify] 1 of 6 shots FAIL
  - zoom2_origin: max_delta 214 > 64; match_pct 98.942% < 99.9%;
    psnr 24.12dB < 35.0dB  diff=<exe>/save_files/screenshots/diffs/zoom2_origin.diff.png
```

The diff PNG is an 8-bit-per-channel visualization of the per-byte delta,
boosted 4x for contrast. Bright pixels = regression hotspots.

## Updating references

When a render change intentionally alters output (new lighting pass,
shader tweak, AA change), the references need to move with it:

```
python3 scripts/render-verify.py --target IRShapeDebug --update-references
```

The driver prompts for confirmation before overwriting. Pass `--force` to
skip the prompt (use sparingly — blind reference updates hide real
regressions):

```
python3 scripts/render-verify.py --target IRShapeDebug --update-references --force
```

After the update, inspect the new PNGs visually before committing them. A
reference update is a *decision*, not a cleanup.

## Bootstrapping a new backend

If no references exist for the current backend yet (fresh fleet host, new
platform, etc.), the skill exits with:

```
[render-verify] no references found for backend 'linux-debug' at
creations/demos/shape_debug/test/references/linux-debug. Run with
--update-references to capture them.
```

Run once with `--update-references --force` to seed the set, then commit
the captured PNGs.

## Adding a new demo

1. Implement `--auto-screenshot` in the demo's `main.cpp` with a shot
   table (follow `shape_debug/main.cpp`).
2. Create `creations/demos/<demo>/test/references/manifest.json` listing
   the shots in the same order, plus thresholds.
3. Build and run the demo to capture a reference set:
   `python3 scripts/render-verify.py --target IR<Demo> --update-references --force`
4. Commit the `test/references/<backend>/` PNGs alongside the manifest.
5. A Linux fleet agent captures the `linux-debug/` set on its host; a
   macOS agent captures the `macos-debug/` set. Each backend's set is
   committed independently.

## Comparator internals

`scripts/render-compare.py` is a standalone tool. Three metrics:

| Metric         | Meaning                                          | Pass threshold |
|----------------|--------------------------------------------------|----------------|
| `match_pct`    | % of bytes within `per_pixel_tol` of reference   | ≥ 99.9         |
| `max_delta`    | largest single-byte delta anywhere               | ≤ 64           |
| `psnr_db`      | peak signal-to-noise ratio, dB                   | ≥ 35.0         |

All three must pass for an overall PASS. The combo absorbs FP rounding
jitter while still catching localized regressions (a handful of
catastrophic pixels fails `max_delta` even if `match_pct` stays high).

Thresholds are overridable per-demo in the manifest's `"thresholds"`
block. Tighten them when a demo's output is deterministic enough to
warrant it; loosen when it isn't (moving scenes, animated content,
random seeds).

**No external dependencies.** The comparator uses only the Python
standard library (`zlib`, `struct`, `array`) — specifically chosen so the
fleet doesn't need `pip install Pillow` on every new host.

## Anti-patterns

- ❌ Updating references without visually inspecting the new PNGs. Blind
  `--update-references --force` masks real regressions.
- ❌ Committing references for a backend you can't build. Each backend's
  set must be captured on a host that can run that preset.
- ❌ Loosening thresholds to make a failing shot pass. If the shot is
  legitimately flakey, fix the demo's determinism or exclude the shot
  from the manifest.
- ❌ Adding the harness to a demo whose output is intentionally random or
  time-dependent. The demo must be fully deterministic across runs for
  the harness to mean anything.
- ❌ Sharing references across backends. OpenGL and Metal produce
  pixel-different output; treating one as the canonical reference will
  produce false failures on the other.

## Related

- `.claude/skills/render-debug-loop/SKILL.md` — iterative render-debug
  loop (build → run → inspect → fix → repeat). Use that when chasing
  a new render bug; use `render-verify` when the suspect is "did this
  change break anything I wasn't looking at?".
- `.claude/skills/attach-screenshots/SKILL.md` — engine rendering PRs
  attach before/after PNGs from the debug loop; `render-verify` is the
  automated version that doesn't need a human to eyeball the result.
- `scripts/render-compare.py` — the comparator CLI. Use directly for
  one-off diffs outside the full harness flow.
