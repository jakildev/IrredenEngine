---
name: render-verify
description: >-
  Automated pass/fail render-regression harness for Irreden Engine demos:
  builds a demo, runs it with `--auto-screenshot`, compares each shot against
  a committed reference image with a stdlib-only Python comparator, and
  reports per-shot pass/fail with diff images for mismatches. Use after any
  render pipeline change to catch visual regressions, or as a sanity pass on
  `master` before cutting a release.
---

# Render Verify

`render-verify` is the "did this change break anything I wasn't looking at?"
gate; `render-debug-loop` is the iterative build → run → inspect loop for
chasing a known bug, and `attach-screenshots` produces the PR-body before/after
pair. `scripts/render-compare.py` is the comparator on its own for one-off
diffs. Validator index: [`docs/agents/VALIDATION.md`](../../../docs/agents/VALIDATION.md).

## Prerequisites

- Any preset ([`docs/agents/BUILD.md`](../../../docs/agents/BUILD.md)). The
  harness reads the active backend from `build/CMakeCache.txt` and looks up
  references under `creations/demos/<demo>/test/references/<preset>/`. Backends
  produce pixel-different output, so each keeps its own reference set — never
  shared, and never committed from a host that cannot build that preset.
- Screenshots are framebuffer-sized: a Retina host captures at 2× and fails
  a non-HiDPI reference on shape mismatch. Capture references on a host
  representative of where verify runs.
- The demo implements `--auto-screenshot [warmup-frames]`
  ([`engine/video/CLAUDE.md`](../../../engine/video/CLAUDE.md) §"Auto-screenshot
  helper") and is deterministic across runs — random or time-dependent output
  makes the harness meaningless.

### Manifest

`creations/demos/<demo>/test/references/manifest.json` lists the shots in the
demo's shot-table order (the Nth shot maps to the Nth numbered screenshot):

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

`"target"` is authoritative for `--target` → demo-directory resolution: the
harness scans every manifest and matches on it, falling back to inferring the
directory from the target name (strip `IR`, snake_case) only when no manifest
declares it. A manifest without `target` is invisible to `--all` and to the
lookup.

## Running

```
python3 scripts/render-verify.py --target IRShapeDebug
```

Resolves the demo via its manifest, detects the backend, runs
`fleet-build --target <T>`, clears `<exe_dir>/save_files/screenshots/`, runs
`fleet-run <T> --auto-screenshot 10`, compares each shot with
`scripts/render-compare.py`, prints a pass/fail table, and exits non-zero on
any failure. A run ending in `ir-run: RESULT=CRASH` fails regardless of how
many shots saved ([`docs/agents/FLEET.md`](../../../docs/agents/FLEET.md)
§"Clean-exit policy").

```
shot                            result     match%   max_d     psnr
------------------------------------------------------------------
zoom1_origin                    PASS       100.0        0      inf
zoom2_origin                    FAIL     98.9423      214    24.12

[render-verify] 1 of 6 shots FAIL
  - zoom2_origin: max_delta 214 > 64; match_pct 98.942% < 99.9%;
    psnr 24.12dB < 35.0dB  diff=<exe>/save_files/screenshots/diffs/zoom2_origin.diff.png
```

The diff PNG is the per-byte delta boosted 4× — bright pixels are the
regression.

### Whole reference set

```
python3 scripts/render-verify.py --all
```

Runs every demo whose manifest declares `target` and prints one aggregate
tally (`total: N checks across M demos, K FAIL, S skipped`). A demo that
produced no checks counts as **ERROR**, is named on stderr, and fails the run,
so a shrunken total cannot pass as a complete sweep — use `--all`, not a shell
loop over target names. `--all` ignores `--target` and rejects `--demo`.

## Updating references

```
python3 scripts/render-verify.py --target IRShapeDebug --update-references
```

Prompts before overwriting; `--force` skips the prompt. A reference update is a
decision: inspect the new PNGs before committing them alongside the render
change. Never loosen thresholds to pass a failing shot — fix the demo's
determinism or drop the shot from the manifest.

### Bootstrapping a backend

With no references for the current backend the harness exits with
`[render-verify] no references found for backend '<preset>' at <dir>. Run with
--update-references to capture them.` Seed with `--update-references --force`
and commit the PNGs.

### Adding a demo

1. Implement `--auto-screenshot` with a shot table (`shape_debug/main.cpp`).
2. Add `test/references/manifest.json` with `target`, ordered `shots`, and
   thresholds.
3. `python3 scripts/render-verify.py --target IR<Demo> --update-references --force`
4. Commit `test/references/<preset>/` with the manifest. Each backend's set is
   captured on its own host and committed independently.

## Comparator

`scripts/render-compare.py` — Python standard library only (`zlib`, `struct`,
`array`), no Pillow. All three metrics must pass:

| Metric | Meaning | Pass |
|---|---|---|
| `match_pct` | % of bytes within `per_pixel_tol` of the reference | ≥ 99.9 |
| `max_delta` | largest single-byte delta | ≤ 64 |
| `psnr_db` | peak signal-to-noise ratio | ≥ 35.0 |

The combination absorbs FP-rounding jitter while a handful of catastrophic
pixels still fails `max_delta`. Override per demo in the manifest's
`thresholds` — tighter when output is fully deterministic, looser for moving
or animated scenes.
