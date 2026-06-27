# Plan: P3 — migrate perf_grid + shape_debug custom flags

- **Issue:** #2060
- **Model:** opus
- **Date:** 2026-06-26
- **Epic:** #2057 — see `.fleet/plans/issue-2057.md`
- **Blocked by:** #2058

This plan mirrors the architect's `## Plan` comment posted on issue #2060;
committed here as the implementer's first commit per #1932.

## Verified current state
- `creations/demos/perf_grid/main.cpp` — a `parseArgs()` strcmp loop over 14
  flags + calls to BOTH `parseAutoScreenshotArgv` and `parseConfigPresetArg`.
- `creations/demos/shape_debug/main.cpp` — inline strcmp parsing of ~18 flags
  + `parseAutoScreenshotArgv`.
Assumes P1 (#2058) landed `IREngine::args()`.

## Sibling reconciliation
P1 (#2058) already removes `parseConfigPresetArg` AND repoints perf_grid's
preset read. So by the time P3 runs, perf_grid no longer calls it — P3 only
removes the remaining strcmp loop + the `parseAutoScreenshotArgv` call and
registers the 14 flags. Re-confirm at impl time that P1 landed the preset
repoint (don't double-edit it).

## Affected files
- `creations/demos/perf_grid/main.cpp`
- `creations/demos/shape_debug/main.cpp`

## Approach — flag→type mapping (preserve defaults exactly)
- counts (`--grid-size`, `--base-subdivisions`, `--worker-threads`) → integer
- `--zoom`, `--yaw`, `--wave-amplitude` → number
- `--auto-profile` (default 300!), `--spin-yaw` → optionalInt
- string-valued (`--mode`, `--subdivision-mode`, `--debug-overlay`,
  `--load-vxs`, `--spin-shape`, `--depth-probe "X,Y"`) → string (demo-side
  parse for the comma pair)
- the rest (`--yaw-ramp`, `--yaw-ramp-crops`, `--occlusion-cull`,
  `--no-overlay`, `--depth-color`, `--checkerboard`, `--gpu-voxel-smoke`,
  `--skin-smoke`, `--pivot-focus-demo`, `--pan-sweep`, `--yaw-sweep`,
  `--pivot-origin`, `--cull-validate`, `--spin-shape-voxel`) → flag

Register custom flags on `IREngine::args()` before `IREngine::init(argc,
argv)`, then read them back via the typed getters; preserve the existing
CLI-override precedence (defaults < config.lua < preset < CLI) by gating each
override on `wasProvided()`.

## Verification
- `perf_grid --mode sdf --grid-size 128 --auto-profile` and `shape_debug
  --yaw-sweep --zoom 4` behave identically to pre-migration.
- `--help` on each lists all flags; `fleet-build` clean.
- `grep -n strcmp creations/demos/{perf_grid,shape_debug}/main.cpp` → empty.
