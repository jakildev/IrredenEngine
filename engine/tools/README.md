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
| `bin/ir-run` | Run a built executable from its own directory; wraps `--auto-screenshot` in `ir-acquire gpu` and `--auto-profile` in `ir-acquire benchmark`. On native Windows every run takes the gpu lock (`IR_RUN_GL_EXCLUSIVE=0` opts out) — a second live GL context stalls other engine processes' shader links for minutes. |
| `bin/ir-perf-grid` | Perf-matrix runner (`ir-perf-grid [matrix-args]`, `ir-perf-grid calibrate`, `ir-perf-grid ref`). Wraps `scripts/perf/perf_grid_matrix.sh` in `ir-acquire benchmark` and splices ref_ms + host fingerprint into `manifest.json`; each cell also records `load_ref_ms` readings taken inside the hold: `before`, the median of three back-to-back samples (kept in `before_samples`), and one `after`. `check_regression.py` grades on `before` only, and only for the host classes in `scripts/perf/README.md`. |
| `bench/ir_ref_bench.cpp` | Deterministic IRMath hot-path mini-bench. Tuned to ~50ms on the calibration host; ref_ms above target means the host was loaded and per-cell measurements should be weighted normalized. |
| `lib/concurrency_helpers.sh` | Sourced by `bin/ir-*`. Lock primitives + 3-layer config resolver. |
| `py/ir_hardware_probe.py` | Python module called by `ir-host-probe`. Linux, macOS, and Windows. |
| `concurrency.toml` | Committed engine defaults — first layer of the three-layer config. |

## Three-layer config

Highest specificity wins:

1. **Env vars** — `IR_CPU_BUDGET`, `IR_FLEET_WORKERS`, `IR_BUILD_JOBS`,
   `IR_GPU_EXCLUSIVE`, `IR_QUEUE_TIMEOUT`, and the `IR_QUIET_*` tuning values.
2. **Host overrides** at `~/.config/irreden/host.toml` — per-host
   quirks (thermal-throttling box wants `per_build_max = 6`, etc.).
3. **Engine defaults** in this directory's `concurrency.toml`.

The values themselves (CPU budget, per-build cap, GPU exclusivity, perf
target ms) are documented inline in `concurrency.toml`.

## Lock model

Lock dirs under `$XDG_RUNTIME_DIR/irreden/locks/` (Linux) or
`/tmp/irreden-$USER/locks/` (macOS / hosts without `$XDG_RUNTIME_DIR`).
Three resource types, plus the gate that serializes CPU slot walks:

- `cpu/slot-{1..budget}/` — N independent slot dirs, one per core in the
  budget. Acquiring N slots = grabbing N free slot dirs.
- `cpu/walk/` — exclusive gate over one slot walk. A requester grabs free
  slots and, when it comes up short, hands the partial set back, all under
  the gate; a held gate counts as a failed attempt and is retried. Two
  ungated walks that overlap leapfrog each other into a livelock on a
  many-core host. `ir-acquire --info` prints `cpu walk gate: held by …`
  while it is held.
- `gpu/lock/` — single exclusive lock dir.
- `perf/lock/` — single exclusive lock dir.

Holding a lock means owning its `owner` file: the holder pid, its owner
token (below), and a nonce. The record is written whole beside the lock dir
and hard-linked in, so exactly one caller's `ln` wins. A dead holder's lock
is transferred, not removed: a contender links a `reap.<nonce>` takeover
marker for the dead record, then swaps its own record into `owner`. A
contender that finds the marker taken by another dead process walks on to
that process's marker, so two reclaimers never both win. Release renames the
lock dir aside and deletes the copy; nothing removes a lock dir in place.
`ir-acquire --info` runs a passive stale-sweep, through the same transfer,
before printing state, and reads the holder's age from the nonce.

The holder also writes single-line `pid`, `winpid` and `acquired_at`
mirrors, so a helper copy older than `owner` (another worktree's branch, a
shell that sourced the file before a pull) still reads a live lock as held.
New code adopts such a copy's lock only when its `pid` holder is dead.

On native Windows the owner token is the holder's Windows pid, because MSYS2
bash and Git for Windows bash share the lock root through `TEMP` but not a
pid table, and `kill -0` from one runtime reports every holder in the other
dead. Release and the stale-sweep require the token to match as well as the
pid, and the per-holder ledger under `.held/` is keyed `<pid>.<winpid>`
there, so two holders sharing a pid number never release each other's locks.

Locks are not re-entrant. The wrapped command inherits
`IR_ACQUIRE_HOLDER_PID` / `IR_ACQUIRE_HELD_VERB`, and a nested `ir-run` whose
verb the ancestor still holds runs inside that hold instead of queueing on it.
On native Windows it also inherits `IR_ACQUIRE_HOLDER_WINPID`, which must
match the owner token on the lock: the other runtime's holder can carry the
same pid number.

A `gpu` or `benchmark` hold can lend its gpu lock to a child run
(`IREngine.handOff`). The wrapped command gets `IR_RUN_LOCK_TOKEN` and
`IR_ACQUIRE_TOOL`; `ir-acquire --lease-release <token>` frees the lock and
`ir-acquire --lease-reacquire <token>` waits for it and restamps it to the
original holder. The lease record lives under `leases/<token>/`, and the
holder's exit trap drops it. Contract:
[`docs/design/engine-hand-off.md`](../../docs/design/engine-hand-off.md).

`ir-acquire benchmark` also raises a quiet window under the lock root. Every
dispatched fleet session holds a lease; the dispatcher stops launching, tool
hooks and queued lock users park, and the benchmark waits until every live
lease has a quiet process tree. Exit 75 means the drain deadline expired and
the command did not run. Exit 76 means activity resumed during the hold and
the measurement is contaminated. `ir-run` reports these as
`RESULT=HOST-NOT-QUIET` / `RESULT=CONTAMINATED` and prints
`QUIET=guarded|unguarded|breach|refused` for every benchmark run.

The `[quiet]` values in `concurrency.toml` are stamped on each request so all
readers sharing the lock root agree. `ir-acquire --quiet-disable [reason]`
writes the host-wide switch and makes subsequent runs explicitly unguarded;
`--quiet-enable` restores the guard, and `--quiet-status [--json]` reports it.
On native Windows a lease's process tree is the Win32 creator links overlaid
with each runtime's own process table (`<runtime>/usr/bin/ps.exe` for the
lease holder's runtime and the parker's). An MSYS `exec` leaves the new image
under a Windows creator that has already exited, and only the runtime's table
still links it to its shell. A table that cannot be read counts as activity.
The guarantee excludes interactive processes without leases, fleet daemons,
non-fleet work, double-forked descendants, the measurement harness, and the
CPU a native-Windows process spends in a sample interval it exits during,
because Windows process time never absorbs an exited child.

## Signals

The EXIT trap releases every held lock. INT, TERM and HUP end `ir-acquire`
with 130, 143 and 129: a requester still queued never runs its command, and
the no-command hold (`ir-acquire gpu`) exits as soon as the signal lands.
Signals are not forwarded to the wrapped command. A signal that arrives while
a foreground child runs (the command, a retry sleep) takes effect when that
child returns, so a process-group signal or a kill of the command itself is
what stops a running command.

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
don't conflict with a live fleet on the same host. An exported
`IR_LOCK_ROOT` outranks that temp dir and an exported `IR_QUIET_OWNER` parks
every acquisition, so run them from a fleet pane as
`env -u IR_LOCK_ROOT -u IR_QUIET_OWNER bash test/tools/concurrency_test.sh`.

The lock races themselves (two reclaimers, an unstamped creator, the stale
sweep, helper copies of two versions on one lock root) run under FIFO
barriers in `scripts/fleet/tests/test_ir_lock_transfer.sh`, which CI runs
through `bash scripts/fleet/tests/run_all.sh`.
