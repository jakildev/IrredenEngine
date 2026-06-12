# engine/tools/

Engine-level utilities that the fleet, a solo dev, and CI all use to
coordinate the same machine: who owns the CPU right now, who owns the
GPU, who is mid-perf-measurement.

This directory is the new home for tooling that has historically lived
under `scripts/fleet/` (`fleet-build`, `fleet-run`) but is really an
engine concern — the fleet is just the noisiest client. Sub-task 2 of
#1074 migrates those scripts here; this PR (sub-task 1) lands the lock
primitives and host probe they will sit on top of.

## Status

Sub-tasks 1–3 of issue #1074 (T-318, T-329, T-330). Lands:

- `ir-host-probe` — deterministic hardware fingerprint
- `ir-acquire` — CPU / GPU / perf lock primitives + `benchmark` canned mode
- `ir-build` — cmake wrapper, wraps the build in `ir-acquire cpu`
- `ir-run` — exe runner, verb-conditional `ir-acquire` (`gpu` for
  `--auto-screenshot`, `benchmark` for `--auto-profile`)
- `ir-perf-grid` — perf-matrix runner; wraps the matrix in
  `ir-acquire benchmark` and captures `ref_ms` from `ir_ref_bench` so the
  comparator can normalize per-cell measurements when the host was loaded
- `ir_ref_bench` — synthetic-load reference micro-bench (IRMath hot paths),
  deterministic, ~50ms target on the calibration host
- shared library (`lib/concurrency_helpers.sh`) and engine defaults
  (`concurrency.toml`)
- `test/tools/concurrency_test.sh` / `test/tools/calibration_test.sh` /
  `test/tools/normalization_test.sh`

`scripts/fleet/fleet-build` and `scripts/fleet/fleet-run` are one-line
shims that exec into `ir-build` / `ir-run`. Old invocations continue to
work; agents need not rewrite call sites.

Sub-task 4 (doc-only worker-role updates around the acquire-late /
release-early rule) ships as its own follow-up PR.

## Inventory

| Path | Role |
|---|---|
| `bin/ir-host-probe` | Print this host's JSON fingerprint. Cached at `$XDG_CACHE_HOME/irreden/host-fingerprint.json`. |
| `bin/ir-acquire` | Acquire CPU slots, GPU, perf, or all three (`benchmark`). Releases on command exit. |
| `bin/ir-build` | `cmake --build` wrapper, holds `ir-acquire cpu` for the build window. Each worktree builds into its own `build/`. |
| `bin/ir-run` | Run a built executable from its own directory; wraps `--auto-screenshot` in `ir-acquire gpu` and `--auto-profile` in `ir-acquire benchmark`. |
| `bin/ir-perf-grid` | Perf-matrix runner (`ir-perf-grid [matrix-args]`, `ir-perf-grid calibrate`, `ir-perf-grid ref`). Wraps `scripts/perf/perf_grid_matrix.sh` in `ir-acquire benchmark` and splices ref_ms + host fingerprint into `manifest.json`. |
| `bench/ir_ref_bench.cpp` | Deterministic IRMath hot-path mini-bench. Tuned to ~50ms on the calibration host; ref_ms above target means the host was loaded and per-cell measurements should be weighted normalized. |
| `lib/concurrency_helpers.sh` | Sourced by `bin/ir-*`. Lock primitives + 3-layer config resolver. |
| `py/ir_hardware_probe.py` | Python module called by `ir-host-probe`. Linux + macOS. |
| `concurrency.toml` | Committed engine defaults — first layer of the three-layer config. |

## Three-layer config

Highest specificity wins:

1. **Env vars** — `IR_CPU_BUDGET`, `IR_FLEET_WORKERS`, `IR_BUILD_JOBS`,
   `IR_GPU_EXCLUSIVE`, `IR_QUEUE_TIMEOUT`.
2. **Host overrides** at `~/.config/irreden/host.toml` — per-host
   quirks (thermal-throttling box wants `per_build_max = 6`, etc.).
3. **Engine defaults** in this directory's `concurrency.toml`.

The values themselves (CPU budget, per-build cap, GPU exclusivity, perf
target ms) are documented inline in `concurrency.toml`.

## Lock model

Atomic `mkdir`-based locks under `$XDG_RUNTIME_DIR/irreden/locks/` (Linux)
or `/tmp/irreden-$USER/locks/` (macOS / hosts without `$XDG_RUNTIME_DIR`).
Three resource types:

- `cpu/slot-{1..budget}/` — N independent slot dirs, one per core in the
  budget. Acquiring N slots = grabbing N free slot dirs.
- `gpu/lock/` — single exclusive lock dir.
- `perf/lock/` — single exclusive lock dir.

Each lock dir contains a `pid` file with the holding process PID. Stale
locks (holder PID is dead) are reclaimed on the next acquire attempt.
`ir-acquire --info` runs a passive stale-sweep before printing state.

## Acquire-late, release-early

`ir-acquire` wraps a single command and releases on its exit. Don't keep
a perf or GPU lock open across "drafting a PR comment" or "deciding what
to do next" — reclaim per measurement. Letting the wrapping process exit
is the cleanest way to release.

The `optimize` skill, render demos with `--auto-profile`, and
`ir-perf-grid` (sub-task 3) all use `ir-acquire benchmark` to take
`(budget-1)` CPU slots + GPU + perf in one shot for the measurement
window, then release.

## Examples

```sh
# Print the host fingerprint.
ir-host-probe
ir-host-probe --slug                  # just "linux-x86_64-ryzen-7950x-rtx-4080"

# Show what's currently held.
ir-acquire --info

# Take 4 CPU slots for a build.
ir-acquire cpu 4 -- cmake --build build -j4

# Lock the GPU exclusively for a screenshot capture.
ir-acquire gpu -- ./build/IRShapeDebug --auto-screenshot 10

# Heavy-measurement mode — cpu(budget-1) + gpu + perf, all serialized.
ir-acquire benchmark -- ./build/IRPerfGrid --auto-profile 300

# Fail fast on contention instead of waiting.
ir-acquire --nonblock gpu -- ./tool
```

## Tests

```sh
test/tools/concurrency_test.sh     # lock primitives, PID-death recovery
test/tools/calibration_test.sh     # probe JSON + determinism, ir_ref_bench schema
test/tools/normalization_test.sh   # comparator: normalize, fingerprint, gate decisions
```

All three isolate via temp `$XDG_RUNTIME_DIR` / `$XDG_CACHE_HOME` so they
don't conflict with a live fleet on the same host.
